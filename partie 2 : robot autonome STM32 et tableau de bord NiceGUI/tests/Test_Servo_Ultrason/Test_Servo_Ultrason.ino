// =====================================================
//  Test servo + ultrason (radar) - STM32 Nucleo-F439ZI
//  Trig A5 | Echo D8 (diviseur 1k/2k) | Servo D9
//  Moniteur serie : 115200 bauds
// =====================================================
#include <Servo.h>

#define TRIG       A5
#define ECHO       D8
#define PIN_SERVO  D9

Servo servo;

float lireDistance() {                      // mesure sans pulseIn
  digitalWrite(TRIG, LOW);  delayMicroseconds(2);
  digitalWrite(TRIG, HIGH); delayMicroseconds(10);
  digitalWrite(TRIG, LOW);
  unsigned long t0 = micros();
  while (digitalRead(ECHO) == LOW)  { if (micros() - t0 > 30000) return -1; }
  unsigned long debut = micros();
  while (digitalRead(ECHO) == HIGH) { if (micros() - debut > 30000) return -1; }
  return (micros() - debut) * 0.0343 / 2.0;
}

void setup() {
  Serial.begin(115200);
  pinMode(TRIG, OUTPUT);
  pinMode(ECHO, INPUT);
  servo.attach(PIN_SERVO);
  servo.write(90);
  delay(1000);
  Serial.println("=== Test servo + ultrason ===");
}

void loop() {
  for (int a = 10; a <= 170; a += 20) {
    servo.write(a);
    delay(400);
    float d = lireDistance();
    Serial.print("Angle "); Serial.print(a); Serial.print(" deg -> ");
    if (d < 0) Serial.println("pas d'echo");
    else { Serial.print(d, 1); Serial.println(" cm"); }
  }
}
