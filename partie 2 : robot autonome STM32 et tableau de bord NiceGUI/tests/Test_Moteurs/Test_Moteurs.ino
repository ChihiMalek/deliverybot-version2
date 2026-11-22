// =====================================================
//  Test moteurs - STM32 Nucleo-F439ZI + L298N
//  IN1 D7 (sens G) | IN2 D6 (PWM G) | IN3 D5 (PWM D) | IN4 D4 (sens D)
//  Cavaliers ENA/ENB en place | Moniteur serie : 115200 bauds
//  Sequence : avancer 2 s, arret, gauche 2 s, arret, droite 2 s, arret
// =====================================================
#define G_SENS  D7
#define G_PWM   D6
#define D_PWM   D5
#define D_SENS  D4

#define VITESSE         80   // %
#define INVERSER_GAUCHE 0    // 1 si la roue gauche tourne a l'envers
#define INVERSER_DROIT  1    // 1 si la roue droite tourne a l'envers

void moteur(int pinSens, int pinPwm, int vitesse) {
  int pwm = abs(vitesse) * 255 / 100;
  if (vitesse > 0)      { digitalWrite(pinSens, LOW);  analogWrite(pinPwm, pwm); }
  else if (vitesse < 0) { digitalWrite(pinSens, HIGH); analogWrite(pinPwm, 255 - pwm); }
  else                  { digitalWrite(pinSens, LOW);  analogWrite(pinPwm, 0); }
}

void moteurs(int g, int d) {
  moteur(G_SENS, G_PWM, INVERSER_GAUCHE ? -g : g);
  moteur(D_SENS, D_PWM, INVERSER_DROIT  ? -d : d);
}

void setup() {
  Serial.begin(115200);
  analogWriteFrequency(200);          // PWM lent = plus de couple au demarrage
  pinMode(G_SENS, OUTPUT); pinMode(G_PWM, OUTPUT);
  pinMode(D_SENS, OUTPUT); pinMode(D_PWM, OUTPUT);
  moteurs(0, 0);
  delay(2000);
  Serial.println("=== Test moteurs ===");
}

void loop() {
  Serial.println("AVANCER");  moteurs(VITESSE, VITESSE);   delay(2000);
  Serial.println("ARRET");    moteurs(0, 0);               delay(1000);
  Serial.println("GAUCHE");   moteurs(-VITESSE, VITESSE);  delay(2000);
  Serial.println("ARRET");    moteurs(0, 0);               delay(1000);
  Serial.println("DROITE");   moteurs(VITESSE, -VITESSE);  delay(2000);
  Serial.println("ARRET");    moteurs(0, 0);               delay(1000);
}
