/*
 * ARDUINO UNO <-> NodeMCU ESP8266 (UART)
 * Capteurs : ultrason + LDR | Actionneurs : buzzer + 3 LEDs | Valeurs simulees
 *
 * LES 3 LEDs SONT COMMANDEES PAR LA PLATEFORME (commandes recues du NodeMCU sur D12) :
 *   "MARCHE"   -> systeme en marche : LED VERTE allumee, LED rouge eteinte
 *   "ARRET"    -> systeme a l'arret : LED ROUGE allumee, LED verte eteinte
 *   "ACQ"      -> acquittement      : LED JAUNE allumee 2 s + buzzer coupe pour l'alarme en cours
 *   "TOUCHE:n" -> touche n (1 a 9) du clavier de la plateforme, renvoyee dans la trame
 * Au demarrage, le systeme est a l'ARRET (LED rouge).
 * L'alarme DANGER (objet a 10 cm ou moins) fait sonner le buzzer tant qu'elle n'est pas acquittee.
 *
 * ===================== CABLAGE =====================
 *  HC-SR04 : Trig -> D2 | Echo -> D4      LDR : point milieu -> A0      Buzzer : + -> D8
 *  LEDs (patte longue -> 220 ohms -> broche ; patte courte -> GND) :
 *     ROUGE -> D6 | VERTE -> D7 | JAUNE -> D11
 *  NodeMCU : Arduino D13 -> pont diviseur -> NodeMCU D5   (donnees vers la plateforme)
 *            NodeMCU D6  -> Arduino D12                   (commandes de la plateforme)
 *            Arduino GND -> NodeMCU G
 * ===================================================
 */

#include <SoftwareSerial.h>

// ================== REGLAGES A ADAPTER ==================
const bool BUZZER_ACTIF       = false;  // true = buzzer actif, false = passif
const int  SEUIL_SOMBRE       = 150;
const float SEUIL_DANGER      = 10.0;   // cm
const float SEUIL_ATTENTION   = 30.0;   // cm
const unsigned long DUREE_JAUNE = 2000; // ms d'allumage de la LED jaune apres acquittement
// ========================================================

const int PIN_TRIG   = 2;
const int PIN_ECHO   = 4;
const int PIN_LDR    = A0;
const int PIN_BUZZER = 8;
const int LED_ROUGE  = 6;
const int LED_VERTE  = 7;
const int LED_JAUNE  = 11;
SoftwareSerial espSerial(12, 13);   // RX = D12, TX = D13 -> NodeMCU

const unsigned long PERIODE_ENVOI = 1000;
unsigned long dernierEnvoi = 0;
unsigned long dernierBip   = 0;
bool etatBip = false;

// ---------------- Valeurs simulees ----------------
float temperature = 25.0;
float humidite    = 55.0;
float courantPV   = 1.5;
float courantSys  = 0.8;
float tensionSys  = 3.5;

// ---------------- Etat ----------------
float distance = -1;
int luminosite = 0;
bool sombre = false;
bool danger = false;
const char* alarme = "OK";

bool systemeEnMarche = false;        // false = ARRET (rouge), true = MARCHE (vert)
bool acquitte = false;               // alarme en cours acquittee (buzzer coupe)
unsigned long jauneJusqua = 0;       // instant d'extinction de la LED jaune
int derniereTouche = 0;              // derniere touche du clavier (0 = aucune)
String commande = "";

float aleatoire(float a, float b) { return a + (b - a) * random(0, 10001) / 10000.0; }
float borner(float x, float mn, float mx) { return x < mn ? mn : (x > mx ? mx : x); }

bool jauneAllumee() { return millis() < jauneJusqua; }

void buzzer(bool on) {
  if (BUZZER_ACTIF) digitalWrite(PIN_BUZZER, on ? HIGH : LOW);
  else if (on)      tone(PIN_BUZZER, 2000);
  else              noTone(PIN_BUZZER);
}

// Applique l'etat des 3 LEDs
void majLeds() {
  digitalWrite(LED_VERTE, systemeEnMarche ? HIGH : LOW);
  digitalWrite(LED_ROUGE, systemeEnMarche ? LOW : HIGH);
  digitalWrite(LED_JAUNE, jauneAllumee() ? HIGH : LOW);
}

// Execute une commande recue de la plateforme
void executer(String c) {
  if (c == "MARCHE") {
    systemeEnMarche = true;
  } else if (c == "ARRET") {
    systemeEnMarche = false;
  } else if (c == "ACQ") {
    jauneJusqua = millis() + DUREE_JAUNE;
    if (danger) acquitte = true;            // coupe le buzzer pour l'alarme en cours
  } else if (c.startsWith("TOUCHE:")) {
    int n = c.substring(7).toInt();
    if (n >= 1 && n <= 9) derniereTouche = n;
  } else {
    return;                                  // commande inconnue : ignoree
  }
  Serial.print("Commande recue : ");
  Serial.println(c);
}

// Lecture des commandes du NodeMCU (une commande par ligne)
void lireCommandes() {
  while (espSerial.available()) {
    char ch = espSerial.read();
    if (ch == '\n') {
      commande.trim();
      if (commande.length() > 0) executer(commande);
      commande = "";
    } else if (ch != '\r') {
      commande += ch;
      if (commande.length() > 30) commande = "";
    }
  }
}

float mesurerDistance() {
  digitalWrite(PIN_TRIG, LOW);  delayMicroseconds(2);
  digitalWrite(PIN_TRIG, HIGH); delayMicroseconds(10);
  digitalWrite(PIN_TRIG, LOW);
  unsigned long duree = pulseIn(PIN_ECHO, HIGH, 30000UL);
  if (duree == 0) return -1;
  return duree * 0.0343 / 2.0;
}

void simulerValeurs() {
  temperature = borner(temperature + aleatoire(-0.5, 0.5), 18.0, 40.0);
  humidite    = borner(humidite    + aleatoire(-1.5, 1.5), 30.0, 80.0);
  courantPV   = borner(courantPV   + aleatoire(-0.20, 0.20), 0.0, 3.0);
  courantSys  = borner(courantSys  + aleatoire(-0.10, 0.10), 0.2, 1.5);
  tensionSys  = tensionSys + 0.3 * (5.0 - tensionSys) + aleatoire(-0.05, 0.05);
}

void envoyerTrame(Stream &s) {
  s.print("{\"distance\":");     s.print(distance, 1);
  s.print(",\"luminosite\":");   s.print(luminosite);
  s.print(",\"sombre\":");       s.print(sombre ? 1 : 0);
  s.print(",\"alarme\":\"");     s.print(alarme);  s.print("\"");
  s.print(",\"buzzer\":");       s.print(danger && !acquitte ? 1 : 0);
  s.print(",\"acquitte\":");     s.print(acquitte ? 1 : 0);
  s.print(",\"systeme\":\"");    s.print(systemeEnMarche ? "MARCHE" : "ARRET");  s.print("\"");
  s.print(",\"led_rouge\":");    s.print(systemeEnMarche ? 0 : 1);
  s.print(",\"led_verte\":");    s.print(systemeEnMarche ? 1 : 0);
  s.print(",\"led_jaune\":");    s.print(jauneAllumee() ? 1 : 0);
  s.print(",\"touche\":");       s.print(derniereTouche);
  s.print(",\"temperature\":");  s.print(temperature, 1);
  s.print(",\"humidite\":");     s.print(humidite, 1);
  s.print(",\"I_PV\":");         s.print(courantPV, 2);
  s.print(",\"I_sys\":");        s.print(courantSys, 2);
  s.print(",\"V_sys\":");        s.print(tensionSys, 2);
  s.println("}");
}

void setup() {
  Serial.begin(9600);
  espSerial.begin(9600);
  pinMode(PIN_TRIG, OUTPUT);
  pinMode(PIN_ECHO, INPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(LED_ROUGE, OUTPUT);
  pinMode(LED_JAUNE, OUTPUT);
  pinMode(LED_VERTE, OUTPUT);
  buzzer(false);

  // Test des LEDs au demarrage : rouge, jaune, verte
  digitalWrite(LED_ROUGE, HIGH); delay(700); digitalWrite(LED_ROUGE, LOW);
  digitalWrite(LED_JAUNE, HIGH); delay(700); digitalWrite(LED_JAUNE, LOW);
  digitalWrite(LED_VERTE, HIGH); delay(700); digitalWrite(LED_VERTE, LOW);
  randomSeed(analogRead(A5));
}

void loop() {
  lireCommandes();

  distance   = mesurerDistance();
  luminosite = analogRead(PIN_LDR);
  sombre     = (luminosite < SEUIL_SOMBRE);

  danger         = (distance > 0 && distance <= SEUIL_DANGER);
  bool attention = (distance > SEUIL_DANGER && distance <= SEUIL_ATTENTION);
  alarme = danger ? "DANGER" : (attention ? "ATTENTION" : "OK");

  // Buzzer : sonne en DANGER tant que l'alarme n'est pas acquittee
  if (!danger) acquitte = false;             // fin de l'alarme : prochain danger non acquitte
  if (danger && !acquitte) {
    if (millis() - dernierBip >= 200) {
      dernierBip = millis();
      etatBip = !etatBip;
      buzzer(etatBip);
    }
  } else {
    buzzer(false);
    etatBip = false;
  }

  majLeds();

  if (millis() - dernierEnvoi >= PERIODE_ENVOI) {
    dernierEnvoi = millis();
    simulerValeurs();
    envoyerTrame(Serial);      // vers le PC
    envoyerTrame(espSerial);   // vers le NodeMCU
  }

  delay(60);
}
