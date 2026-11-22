// =====================================================================
//  DeliveryBot V2 - Programme complet STM32 Nucleo-F439ZI (IDE Arduino)
//  Capteurs + pont H L298N + clavier + EMS + navigation vers 9 tables
//  + envoi JSON a l'ESP8266 (meme format que la base de donnees)
//  Moniteur serie : 115200 bauds
// =====================================================================
//  CABLAGE
//  LDR A0 | LM35 A1 | DHT11 D3 | Ultrason Trig A5, Echo D8 (diviseur 1k/2k)
//  Servo D9 | L298N : IN1 D7 (sens G), IN2 D6 (PWM G), IN3 D5 (PWM D), IN4 D4 (sens D)
//  Cavaliers ENA/ENB en place | LED obstacle = LED rouge integree (LED_RED)
//  Clavier : lignes D10, D2, D13, D14 | colonnes A4, A2, A3
//  Courant PV PB1 | Courant systeme D12 (simules si AVEC_COURANT 0) | ESP8266 : D1 -> RX, D0 <- TX
// =====================================================================
//  CLAVIER
//  1..9 : le robot PART DIRECTEMENT vers cette table (mode AUTO)
//  #    : retour immediat a la base  *  : ARRET d'urgence
//  0    : mode MANUEL (2 avant, 8 arriere, 4 gauche, 6 droite, 5 stop)
// =====================================================================
#include <Servo.h>
#include <math.h>

// ------------------------- Broches -------------------------
#define PIN_LDR      A0
#define PIN_LM35     A1
#define PIN_DHT      D3
#define TRIG         A5       // Trig de l'ultrason
#define ECHO         D8
#define PIN_SERVO    D9
#define G_SENS       D7       // IN1
#define G_PWM        D6       // IN2
#define D_PWM        D5       // IN3
#define D_SENS       D4       // IN4
#define INVERSER_DROIT 1      // 1 = moteur droit inverse (il tournait en arriere)
#define PIN_I_PV     PB1      // capteur de courant 1 (panneau) - CN10 ADC_IN (A6)
#define PIN_I_SYS    D12      // capteur de courant 2 (systeme complet)

#define AVEC_COURANT 0        // 0 = courants SIMULES (modele des datasheets de nos composants)
                              // 1 = vrais capteurs (apres calibration, sortie < 3,3 V)

Uart SerialESP(D0, D1);       // (RX, TX)
Servo servo;

const int lignes[4]   = {D10, D2, D13, D14};
const int colonnes[3] = {A4, A2, A3};
const char touches[4][3] = {{'1','2','3'}, {'4','5','6'}, {'7','8','9'}, {'*','0','#'}};

// ------------------------- Parametres -------------------------
#define DT_MS            250      // pas de la boucle de decision
#define ANGLE_MIN        10
#define ANGLE_MAX        170
#define SEUIL_FRONTAL    30.0     // cm
#define SEUIL_LATERAL    22.0     // cm
#define T_PACK_MAX       50.0     // C (LM35 sur le pack)
#define SOC_NOMINAL      50.0     // %
#define SOC_SURVIE       20.0     // %
#define V_MAX            0.50     // m/s a 100 % (a calibrer)
#define ZONE_MORTE       30.0     // % de PWM sans mouvement (a calibrer)
#define VOIE             0.14     // m, ecartement des roues (a mesurer)
#define C_BAT_AH         1.5      // capacite du pack
#define SOC_INITIAL      100.0    // % batteries chargees a 4,2 V (a regler avant chaque essai)

// Vitesses des moteurs (%) - PWM a 200 Hz, a ajuster si le robot cale ou va trop vite
#define VIT_AVANCE       80       // marche avant normale
#define VIT_ECO          65       // marche avant economique / regime degrade
#define VIT_PIVOT        70       // rotation sur place
#define VIT_RECUL        65       // marche arriere (mode manuel)

// Calibration des capteurs de courant : I(mA) = (V - V0) * SENSIBILITE
// A REMPLACER par les valeurs mesurees (voir etape de calibration)
#define I_PV_V0          0.000    // V a courant nul
#define I_PV_SENS        1000.0   // mA par volt
#define I_SYS_V0         0.000
#define I_SYS_SENS       1000.0

// ------------------------- Carte (salle 6 x 6 m) -------------------------
// Tables en (1,5 / 3 / 4,5 m) x (1,5 / 3 / 4,5 m). Allees libres (entre les tables) :
//   allees horizontales y = 0,75 / 2,25 / 3,75 / 5,25   allees verticales x = 0,75 / 2,25 / 3,75 / 5,25
// ECHELLE : 1.0 = vraie salle 6 x 6 m ; 0.3 = carte reduite a 1,8 x 1,8 m pour tester dans une piece
#define ECHELLE          1.0
const float BASE_X = 0.50, BASE_Y = 0.75;
float tableX(int t) { return 1.5 + 1.5 * ((t - 1) % 3); }
float tableY(int t) { return 1.5 + 1.5 * ((t - 1) / 3); }

// ------------------------- Actions / sources / missions -------------------------
enum Action  { ARRET = 0, AVANCER, AVANCER_ECO, PIVOT_GAUCHE, PIVOT_DROITE, SCAN };
enum Source  { BATTERIE_SEULE = 0, HYBRIDE_PV_BAT, PV_SEUL, CHARGE_CONNECTEUR };
enum Mission { LIVRAISON = 0, RETOUR_BASE, RECHARGE_BASE, ATTENTE };
enum Mode    { MODE_ARRET = 0, MODE_AUTO, MODE_MANUEL };
const char* NOM_ACTION[]  = {"ARRET", "AVANCER", "AVANCER_ECO", "PIVOT_GAUCHE", "PIVOT_DROITE", "SCAN"};
const char* NOM_SOURCE[]  = {"BATTERIE_SEULE", "HYBRIDE_PV_BAT", "PV_SEUL", "CHARGE_CONNECTEUR"};
const char* NOM_MISSION[] = {"LIVRAISON", "RETOUR_BASE", "RECHARGE_BASE", "ATTENTE"};

// ------------------------- Etat du robot -------------------------
Mode    mode = MODE_ARRET;
Mission mission = ATTENTE;
int     tableCible = 1;
char    cmdManuelle = '5';
int     action = ARRET, derniereAction = ARRET, source = BATTERIE_SEULE;
int     pwmG = 0, pwmD = 0, angleServo = 90, balayage = 0;
float   distUS = -1, distGauche = -1, distDroite = -1;
float   tempDHT = 0, humDHT = 0; bool dhtOK = false;
float   lm35 = 25, iPV = 0, iSYS = 0, socEst = SOC_INITIAL, vBat = 8.4;
int     pwmGprec = 0, pwmDprec = 0, servoPrec = 90;
int     ldr = 0;
float   xe = BASE_X, ye = BASE_Y, cap = M_PI / 2;   // position estimee (odometrie a l'estime)
float   wpX[6], wpY[6]; int nbWp = 0, wpI = 0;       // points de passage (dans les allees)
int     eviteDir = 0, evitePas = 0, avancePas = 0, attente = 0;
unsigned long tBoucle = 0, tDHT = 0, tJSON = 0;
bool    lm35OK = true;
int     echecsUS = 0;
bool    pivotCap = false;      // pivot de correction de cap (dose en duree)

// =====================================================================
//  ACTIONNEURS
// =====================================================================
// vitesse -100..+100 % ; 1 broche de sens + 1 broche PWM par moteur
void moteur(int pinSens, int pinPwm, int vitesse) {
  vitesse = constrain(vitesse, -100, 100);
  int pwm = abs(vitesse) * 255 / 100;
  if (vitesse > 0)      { digitalWrite(pinSens, LOW);  analogWrite(pinPwm, pwm); }
  else if (vitesse < 0) { digitalWrite(pinSens, HIGH); analogWrite(pinPwm, 255 - pwm); }
  else                  { digitalWrite(pinSens, LOW);  analogWrite(pinPwm, 0); }
}

void moteurs(int g, int d) {
  pwmG = g; pwmD = d;
  moteur(G_SENS, G_PWM, g);
  moteur(D_SENS, D_PWM, INVERSER_DROIT ? -d : d);   // moteur droit monte a l'envers
}

void tournerServo(int a) {
  angleServo = constrain(a, ANGLE_MIN, ANGLE_MAX);
  servo.write(angleServo);
}

// =====================================================================
//  CAPTEURS
// =====================================================================
float tensionMoyenne(int pin, int n) {
  analogRead(pin); delayMicroseconds(100);   // lecture fantome : vide le condensateur de l'ADC
  analogRead(pin); delayMicroseconds(100);   // (evite que la voie precedente fausse la mesure)
  long s = 0;
  for (int i = 0; i < n; i++) { s += analogRead(pin); delayMicroseconds(200); }
  return (s / (float)n) * 3.3 / 4095.0;
}

float lireDistance() {                     // sans pulseIn
  digitalWrite(TRIG, LOW);  delayMicroseconds(2);
  digitalWrite(TRIG, HIGH); delayMicroseconds(10);
  digitalWrite(TRIG, LOW);
  unsigned long t0 = micros();
  while (digitalRead(ECHO) == LOW)  { if (micros() - t0 > 30000) return -1; }
  unsigned long debut = micros();
  while (digitalRead(ECHO) == HIGH) { if (micros() - debut > 30000) return -1; }
  return (micros() - debut) * 0.0343 / 2.0;
}

long attendreDHT(int niveau, unsigned long timeout) {
  unsigned long t0 = micros();
  while (digitalRead(PIN_DHT) != niveau) { if (micros() - t0 > timeout) return -1; }
  return micros() - t0;
}

bool lireDHT11() {                         // sans bibliotheque
  pinMode(PIN_DHT, OUTPUT);
  digitalWrite(PIN_DHT, LOW);  delay(20);
  digitalWrite(PIN_DHT, HIGH); delayMicroseconds(30);
  pinMode(PIN_DHT, INPUT_PULLUP);
  if (attendreDHT(LOW, 100) < 0 || attendreDHT(HIGH, 120) < 0 || attendreDHT(LOW, 120) < 0) return false;
  byte d[5] = {0, 0, 0, 0, 0};
  for (int i = 0; i < 40; i++) {
    if (attendreDHT(HIGH, 80) < 0) return false;
    long duree = attendreDHT(LOW, 100);
    if (duree < 0) return false;
    d[i / 8] <<= 1;
    if (duree > 40) d[i / 8] |= 1;
  }
  if ((byte)(d[0] + d[1] + d[2] + d[3]) != d[4]) return false;
  humDHT = d[0];
  tempDHT = d[2] + d[3] / 10.0;
  return true;
}

void lireCapteurs() {
  ldr  = (int)(tensionMoyenne(PIN_LDR, 5) * 4095.0 / 3.3);
  lm35 = tensionMoyenne(PIN_LM35, 10) * 100.0;
#if AVEC_COURANT
  iPV  = (tensionMoyenne(PIN_I_PV, 10)  - I_PV_V0)  * I_PV_SENS;  if (iPV  < 0) iPV  = 0;
  iSYS = (tensionMoyenne(PIN_I_SYS, 10) - I_SYS_V0) * I_SYS_SENS;  if (iSYS < 0) iSYS = 0;
#else
  // ---- Valeurs SIMULEES a partir des datasheets de NOS composants ----
  // Tension du pack 2S Li-ion 18650 (6,0 V vide -> 8,4 V plein)
  vBat = 6.0 + 2.4 * socEst / 100.0;

  // 1) Moteurs TT (3-6 V) via L298N : ~60 mA a vide + charge, pivot = plus de couple
  float iMot = 0;
  int pg = abs(pwmG), pd = abs(pwmD);
  bool pivot = (pwmG * pwmD < 0);
  if (pg > 0) iMot += 60.0 + 2.2 * pg;
  if (pd > 0) iMot += 60.0 + 2.2 * pd;
  if (pivot) iMot *= 1.3;
  if ((pg > 0 && pwmGprec == 0) || (pd > 0 && pwmDprec == 0)) iMot *= 2.5;   // pic de demarrage
  pwmGprec = pwmG; pwmDprec = pwmD;

  // 2) Electronique en 5 V (via regulateur LM2596, rendement ~85 %)
  float i5V = 120.0                              // STM32 Nucleo-F439ZI
            + 75.0                               // ESP8266 (Wi-Fi actif, moyenne)
            + (random(0, 4) == 0 ? 100.0 : 0.0)  // pic d'emission Wi-Fi (~170 mA)
            + 15.0                               // HC-SR04
            + 2.5                                // DHT11 + LM35 + diviseur LDR
            + 36.0                               // logique du L298N
            + (angleServo != servoPrec ? 180.0 : 8.0);   // SG90 en mouvement / au repos
  servoPrec = angleServo;
  float i5Vbat = i5V * 5.0 / (vBat * 0.85);      // courant vu cote batterie

  iSYS = iMot + i5Vbat;

  // 3) Panneau 3,5 W / 6 V : Imp ~580 mA a 1000 W/m2 ; eclairement estime par la LDR
  float g = constrain((ldr - 100) / 3900.0, 0.0, 1.0) * 1000.0;   // W/m2
  iPV = 583.0 * g / 1000.0;

  // bruit de mesure +/- 3 %
  iSYS *= 1.0 + random(-30, 31) / 1000.0;
  iPV  *= 1.0 + random(-30, 31) / 1000.0;
#endif
  lm35OK = (lm35 > -5.0 && lm35 < 80.0);          // hors plage = capteur en defaut
  distUS = lireDistance();
  if (distUS < 0) echecsUS++; else echecsUS = 0;
}

// =====================================================================
//  CLAVIER
// =====================================================================
char lireClavier() {
  static char derniere = 0;
  char t = 0;
  for (int l = 0; l < 4; l++) {
    digitalWrite(lignes[l], LOW);
    delayMicroseconds(10);
    for (int c = 0; c < 3; c++) if (digitalRead(colonnes[c]) == LOW) t = touches[l][c];
    digitalWrite(lignes[l], HIGH);
  }
  if (t && t != derniere) { derniere = t; return t; }
  if (!t) derniere = 0;
  return 0;
}

// =====================================================================
//  PLANIFICATION (points de passage dans les allees)
// =====================================================================
// Itineraire dans les allees, depuis la position actuelle jusqu'a (xDest, yDest),
// en passant par l'allee verticale xAllee : on ne traverse jamais une table.
void ajouterWp(float x, float y) {
  float px = nbWp ? wpX[nbWp - 1] : xe, py = nbWp ? wpY[nbWp - 1] : ye;
  if (hypot(x - px, y - py) < 0.05) return;        // point inutile (deja dessus)
  wpX[nbWp] = x; wpY[nbWp] = y; nbWp++;
}

void planifierItineraire(float xAllee, float xDest, float yDest) {
  nbWp = 0; wpI = 0;
  float yRangee = 0.75 + 1.5 * round((ye - 0.75) / 1.5);   // allee horizontale la plus proche
  yRangee = constrain(yRangee, 0.75f, 5.25f);
  ajouterWp(xe, yRangee);          // 1) rejoindre l'allee horizontale
  ajouterWp(xAllee, yRangee);      // 2) longer l'allee jusqu'a l'allee verticale
  ajouterWp(xAllee, yDest);        // 3) monter / descendre entre les tables
  ajouterWp(xDest, yDest);         // 4) aller au point d'arrivee
  if (nbWp == 0) ajouterWp(xDest + 0.001, yDest);   // deja arrive
}

void planifierVersTable(int t) {             // point de livraison : 0,75 m au sud de la table
  planifierItineraire(tableX(t) - 0.75, tableX(t), tableY(t) - 0.75);
}

void planifierRetour() {                     // retour a la base par les allees
  planifierItineraire(0.75, BASE_X, BASE_Y);
}

float cibleX() { return wpX[nbWp - 1]; }
float cibleY() { return wpY[nbWp - 1]; }
float distCibleCm() { return nbWp ? hypot(cibleX() - xe, cibleY() - ye) * 100.0 : 0; }
float erreurCapDeg() {
  if (!nbWp) return 0;
  float e = atan2(wpY[wpI] - ye, wpX[wpI] - xe) - cap;
  while (e > M_PI) e -= 2 * M_PI;
  while (e < -M_PI) e += 2 * M_PI;
  return e * 180.0 / M_PI;
}
bool aLaBase() { return hypot(BASE_X - xe, BASE_Y - ye) < 0.15; }
int regime() { return socEst >= SOC_NOMINAL ? 0 : (socEst >= SOC_SURVIE ? 1 : 2); }

// =====================================================================
//  POLITIQUE (meme logique que l'expert de la base de donnees)
//  -> sera remplacee par le modele ML embarque
// =====================================================================
int decisionNavigation() {
  pivotCap = false;
  float d = distUS > 0 ? distUS : 400;
  if (lm35OK && lm35 >= T_PACK_MAX) return ARRET;             // securite thermique
  if (mission == RECHARGE_BASE || mission == ATTENTE) return ARRET;
  if (distCibleCm() < 12) return ARRET;                       // arrivee
  int avance = (regime() == 0 && mission == LIVRAISON) ? AVANCER : AVANCER_ECO;
  if (angleServo == 90 && d < SEUIL_FRONTAL) {                // obstacle frontal
    if (derniereAction != SCAN && evitePas == 0) return SCAN;
    if (evitePas == 0) {
      float g = distGauche > 0 ? distGauche : 400, dr = distDroite > 0 ? distDroite : 400;
      eviteDir = g > dr ? PIVOT_GAUCHE : PIVOT_DROITE;
      evitePas = 3;
    }
    evitePas = max(evitePas - 1, 0); avancePas = 6;
    return eviteDir;
  }
  if (angleServo == 115 && d < SEUIL_LATERAL) { avancePas = 3; return PIVOT_DROITE; }
  if (angleServo == 65  && d < SEUIL_LATERAL) { avancePas = 3; return PIVOT_GAUCHE; }
  if (evitePas > 0) { evitePas--; avancePas = 6; return eviteDir; }
  if (avancePas > 0) { avancePas--; return avance; }
  float e = erreurCapDeg();
  if (e > 20)  { pivotCap = true; return PIVOT_GAUCHE; }
  if (e < -20) { pivotCap = true; return PIVOT_DROITE; }
  return avance;
}

int decisionSource(int a) {                                   // EMS
  if (aLaBase() && (mission == RETOUR_BASE || mission == RECHARGE_BASE) && socEst < 95)
    return CHARGE_CONNECTEUR;
  bool lumiere = (ldr >= 3850) || (iPV >= 50);
  if (!lumiere) return BATTERIE_SEULE;
  if (a == ARRET && iPV >= 400) return PV_SEUL;
  return HYBRIDE_PV_BAT;
}

float vRoue(int p);

void appliquerAction(int a) {
  // Correction de cap : on tourne seulement le temps necessaire (evite de depasser le cap)
  if (pivotCap && (a == PIVOT_GAUCHE || a == PIVOT_DROITE)) {
    float w = 2.0 * vRoue(VIT_PIVOT) / VOIE;                  // rad/s estime en pivot
    float tNec = fabs(erreurCapDeg()) * M_PI / 180.0 / w;     // s
    if (w > 0 && tNec < DT_MS / 1000.0) {
      if (a == PIVOT_GAUCHE) moteurs(-VIT_PIVOT, VIT_PIVOT); else moteurs(VIT_PIVOT, -VIT_PIVOT);
      tournerServo(90);
      delay((int)(tNec * 1000));
      moteurs(0, 0);
      cap += (a == PIVOT_GAUCHE ? 1 : -1) * w * tNec;        // rotation effectuee
      return;
    }
  }
  int v = (a == AVANCER && regime() >= 1) ? VIT_ECO : VIT_AVANCE;   // regime degrade
  switch (a) {
    case AVANCER:      moteurs(v, v);                    break;
    case AVANCER_ECO:  moteurs(VIT_ECO, VIT_ECO);        break;
    case PIVOT_GAUCHE: moteurs(-VIT_PIVOT, VIT_PIVOT);   break;
    case PIVOT_DROITE: moteurs(VIT_PIVOT, -VIT_PIVOT);   break;
    default:           moteurs(0, 0);                    break;
  }
  digitalWrite(LED_RED, (a == SCAN || (distUS > 0 && distUS < SEUIL_FRONTAL)) ? HIGH : LOW);
  if (a == SCAN) {                                            // regard gauche puis droite
    tournerServo(170); delay(500); distGauche = lireDistance();
    tournerServo(10);  delay(500); distDroite = lireDistance();
    tournerServo(90);  delay(300);
  } else if (a == AVANCER || a == AVANCER_ECO) {              // balayage 90-115-90-65
    balayage = (balayage + 1) % 4;
    const int seq[4] = {90, 115, 90, 65};
    tournerServo(seq[balayage]);
  } else {
    balayage = 0; tournerServo(90);
  }
}

// vitesse de roue (m/s) a partir de la consigne PWM (%)
float vRoue(int p) {
  float a = abs(p);
  if (a <= ZONE_MORTE) return 0;
  return (p > 0 ? 1 : -1) * V_MAX * (a - ZONE_MORTE) / (100.0 - ZONE_MORTE);
}

void mettreAJourOdometrie(float dt) {
  float vg = vRoue(pwmG), vd = vRoue(pwmD);
  float v = 0.5 * (vg + vd), w = (vd - vg) / VOIE;
  xe += v * cos(cap) * dt / ECHELLE;        // deplacement reel converti en metres de la carte
  ye += v * sin(cap) * dt / ECHELLE;
  cap += w * dt;
  if (nbWp && wpI < nbWp - 1 && hypot(wpX[wpI] - xe, wpY[wpI] - ye) < 0.15) wpI++;
}

void mettreAJourSoC(float dt) {                               // comptage de coulombs
  float iNet;
  if (source == CHARGE_CONNECTEUR) iNet = -500.0;
  else iNet = iSYS - (source == BATTERIE_SEULE ? 0 : iPV * 6.0 * 0.90 / vBat);   // apport PV ramene a la tension pack
  socEst -= iNet / 1000.0 * dt / 3600.0 / C_BAT_AH * 100.0;
  socEst = constrain(socEst, 0.0f, 100.0f);
}

void gererMission() {
  if (mission == LIVRAISON && regime() == 2) {               // survie -> retour base
    mission = RETOUR_BASE; planifierRetour();
    Serial.println(">>> SoC < 20 % : RETOUR_BASE");
  }
  if (mission == LIVRAISON && distCibleCm() < 12) {          // livraison : attente 2 s
    if (++attente >= 8) {
      Serial.println(">>> Livraison terminee, retour a la base");
      mission = RETOUR_BASE; planifierRetour(); attente = 0;
    }
  }
  if (mission == RETOUR_BASE && aLaBase()) {
    mission = (socEst < 95 && regime() >= 1) ? RECHARGE_BASE : ATTENTE;
    mode = (mission == ATTENTE) ? MODE_ARRET : mode;
    Serial.print(">>> Arrive a la base : "); Serial.println(NOM_MISSION[mission]);
  }
}

// Mode ARRET : le servo balaie 10 -> 170 -> 10 (radar) et l'ultrason mesure a chaque angle
void modeRadar() {
  static int sensRadar = 1;
  moteurs(0, 0);
  int a = angleServo + 20 * sensRadar;
  if (a > ANGLE_MAX) { a = ANGLE_MAX; sensRadar = -1; }
  if (a < ANGLE_MIN) { a = ANGLE_MIN; sensRadar = 1; }
  tournerServo(a);
  digitalWrite(LED_RED, (distUS > 0 && distUS < SEUIL_FRONTAL) ? HIGH : LOW);
}

// =====================================================================
//  CLAVIER -> COMMANDES
// =====================================================================
void demarrerLivraison(int t) {
  if (regime() == 2) { mission = RECHARGE_BASE; mode = MODE_AUTO; Serial.println(">>> SoC trop faible : recharge"); return; }
  tableCible = t;
  if (mode != MODE_AUTO || mission == ATTENTE || mission == RECHARGE_BASE) {
    xe = BASE_X; ye = BASE_Y; cap = M_PI / 2;                 // depart de la base, face au nord
  }                                                           // sinon : nouvelle table depuis la position actuelle
  planifierVersTable(tableCible);
  tournerServo(90); delay(300);                               // regard vers l'avant avant de partir
  echecsUS = 0; evitePas = 0; avancePas = 0; attente = 0;
  mission = LIVRAISON; mode = MODE_AUTO;
  Serial.print(">>> DEPART vers la table "); Serial.println(tableCible);
}

void traiterTouche(char t) {
  Serial.print("Touche : "); Serial.println(t);
  if (t == '*') { mode = MODE_ARRET; mission = ATTENTE; moteurs(0, 0); Serial.println(">>> ARRET D'URGENCE"); return; }
  if (t == '0') { mode = (mode == MODE_MANUEL) ? MODE_ARRET : MODE_MANUEL; cmdManuelle = '5';
                  Serial.println(mode == MODE_MANUEL ? ">>> MODE MANUEL" : ">>> ARRET"); return; }
  if (mode == MODE_MANUEL) { cmdManuelle = t; return; }
  if (t >= '1' && t <= '9') { demarrerLivraison(t - '0'); return; }   // une touche = une livraison
  if (t == '#' && mode == MODE_AUTO && (mission == LIVRAISON || mission == RETOUR_BASE)) {  // rappel a la base
    mission = RETOUR_BASE; planifierRetour();
    Serial.println(">>> RETOUR A LA BASE demande");
  }
}

void modeManuel() {
  int a = ARRET;
  if (cmdManuelle == '2') a = AVANCER;
  if (cmdManuelle == '4') a = PIVOT_GAUCHE;
  if (cmdManuelle == '6') a = PIVOT_DROITE;
  if (a == AVANCER && distUS > 0 && distUS < SEUIL_FRONTAL) a = ARRET;   // securite obstacle
  if (cmdManuelle == '8') { moteurs(-VIT_RECUL, -VIT_RECUL); action = ARRET; return; }  // marche arriere
  action = a;
  appliquerAction(a);
}

// =====================================================================
//  JSON (meme nom de colonnes que la base de donnees)
// =====================================================================
void envoyerJSON() {
  String j = "{";
  j += "\"dist_us_cm\":"     + String(distUS, 1);
  j += ",\"dist_gauche_cm\":" + String(distGauche, 1);
  j += ",\"dist_droite_cm\":" + String(distDroite, 1);
  j += ",\"servo_deg\":"      + String(angleServo);
  j += ",\"ldr_adc\":"        + String(ldr);
  j += ",\"lm35_c\":"         + String(lm35, 1);
  j += ",\"dht_temp_c\":"     + (dhtOK ? String(tempDHT, 1) : String("null"));
  j += ",\"dht_hum_pct\":"    + (dhtOK ? String(humDHT, 0) : String("null"));
  j += ",\"i_pv_ma\":"        + String(iPV, 1);
  j += ",\"i_sys_ma\":"       + String(iSYS, 1);
  j += ",\"soc_est_pct\":"    + String(socEst, 1);
  j += ",\"v_bat_v\":"        + String(vBat, 2);
  j += ",\"regime_energie\":" + String(regime());
  j += ",\"source_active\":"  + String(source);
  j += ",\"mission\":"        + String(mission);
  j += ",\"pwm_g\":"          + String(pwmG);
  j += ",\"pwm_d\":"          + String(pwmD);
  j += ",\"table_cible\":"    + String(tableCible);
  j += ",\"pos_x\":"          + String(xe, 2);
  j += ",\"pos_y\":"          + String(ye, 2);
  j += ",\"dist_cible_cm\":"  + String(distCibleCm(), 1);
  j += ",\"erreur_cap_deg\":" + String(erreurCapDeg(), 1);
  j += ",\"a_la_base\":"      + String(aLaBase() ? 1 : 0);
  j += ",\"derniere_action\":" + String(derniereAction);
  j += ",\"action\":\""       + String(NOM_ACTION[action]) + "\"";
  j += ",\"source\":\""       + String(NOM_SOURCE[source]) + "\"";
  j += ",\"mode\":"           + String(mode);
  j += ",\"courant_simule\":"  + String(AVEC_COURANT ? 0 : 1);
  j += ",\"route\":[";                                        // itineraire prevu restant
  for (int i = wpI; i < nbWp && mode == MODE_AUTO; i++) {
    if (i > wpI) j += ",";
    j += "[" + String(wpX[i], 2) + "," + String(wpY[i], 2) + "]";
  }
  j += "]";
  j += "}";
  SerialESP.println(j);          // JSON -> ESP8266 -> NiceGUI
}

// Ligne lisible pour le moniteur serie (USB)
void afficherEtat() {
  const char* NOM_MODE[] = {"ARRET", "AUTO", "MANUEL"};
  Serial.print("Mode ");      Serial.print(NOM_MODE[mode]);
  Serial.print(" | ");        Serial.print(NOM_MISSION[mission]);
  Serial.print(" T");         Serial.print(tableCible);
  Serial.print(" | Action "); Serial.print(NOM_ACTION[action]);
  Serial.print(" | PWM G=");  Serial.print(pwmG);
  Serial.print(" D=");        Serial.print(pwmD);
  Serial.print(" | Dist ");   Serial.print(distUS, 0);
  Serial.print(" cm Servo "); Serial.print(angleServo);
  Serial.print(" | Pos (");   Serial.print(xe, 2); Serial.print(","); Serial.print(ye, 2);
  Serial.print(") cible ");   Serial.print(distCibleCm(), 0);
  Serial.print(" cm | LM35 "); Serial.print(lm35, 1);
  Serial.print(" DHT ");      Serial.print(tempDHT, 1); Serial.print("C/"); Serial.print(humDHT, 0);
  Serial.print("% LDR ");     Serial.print(ldr);
  Serial.print(" | I_sys ");  Serial.print(iSYS, 0);
  Serial.print(" mA I_pv ");  Serial.print(iPV, 0);
  Serial.print(" mA | Vbat "); Serial.print(vBat, 2);
  Serial.print(" V SoC ");    Serial.print(socEst, 1);
  Serial.print(" | ");        Serial.println(NOM_SOURCE[source]);
  if (echecsUS >= 20) Serial.println("  !! ULTRASON : pas d'echo depuis 5 s (rien devant ou fil debranche)");
  if (!lm35OK)       Serial.println("  !! LM35 hors plage (verifier fil / GND)");
}

// =====================================================================
//  SETUP / LOOP
// =====================================================================
void setup() {
  Serial.begin(115200);
  randomSeed(analogRead(PIN_LDR));
  SerialESP.begin(115200);    // meme vitesse que l'ESP8266
  analogReadResolution(12);
  analogWriteFrequency(200);  // PWM lent = plus de couple (moteurs TT + L298N)

  pinMode(G_SENS, OUTPUT); pinMode(G_PWM, OUTPUT);
  pinMode(D_SENS, OUTPUT); pinMode(D_PWM, OUTPUT);
  pinMode(TRIG, OUTPUT);   pinMode(ECHO, INPUT);
  pinMode(LED_RED, OUTPUT);
  pinMode(PIN_DHT, INPUT_PULLUP);
  for (int l = 0; l < 4; l++) { pinMode(lignes[l], OUTPUT); digitalWrite(lignes[l], HIGH); }
  for (int c = 0; c < 3; c++) pinMode(colonnes[c], INPUT_PULLUP);

  servo.attach(PIN_SERVO);
  moteurs(0, 0);
  // Demarrage : test du servo (balayage complet 10 -> 170 -> 90)
  Serial.println("Demarrage du servomoteur...");
  for (int a = ANGLE_MIN; a <= ANGLE_MAX; a += 10) { tournerServo(a); delay(60); }
  for (int a = ANGLE_MAX; a >= 90; a -= 10)        { tournerServo(a); delay(60); }
  delay(1000);
  Serial.println("=== DeliveryBot V2 ===");
  Serial.println("1..9 : livrer la table | # : retour base | * : arret | 0 : manuel (2/8/4/6/5)");
  Serial.println("Test des moteurs : 0 puis 2 (avancer) / 4 / 6 / 8 / 5 (stop)");
}

void loop() {
  char t = lireClavier();                      // le clavier est lu en continu
  if (t) traiterTouche(t);

  if (millis() - tBoucle < DT_MS) return;      // boucle de decision toutes les 250 ms
  float dt = (millis() - tBoucle) / 1000.0;
  tBoucle = millis();

  lireCapteurs();
  if (millis() - tDHT >= 2000) { tDHT = millis(); dhtOK = lireDHT11(); }

  if (mode == MODE_AUTO) {
    gererMission();
    action = decisionNavigation();
    source = decisionSource(action);
    appliquerAction(action);
  } else if (mode == MODE_MANUEL) {
    modeManuel();
    source = decisionSource(action);
  } else {
    action = ARRET;
    source = decisionSource(action);
    modeRadar();                               // robot arrete : servo en balayage radar
  }
  mettreAJourOdometrie(dt);
  mettreAJourSoC(dt);
  derniereAction = action;

  if (millis() - tJSON >= 1000) { tJSON = millis(); envoyerJSON(); afficherEtat(); }
}
