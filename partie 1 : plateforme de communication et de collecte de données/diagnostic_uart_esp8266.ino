/*
 * OUTIL DE DIAGNOSTIC - liaison UART Arduino -> NodeMCU ESP8266 (broche D5)
 * Affiche chaque seconde (moniteur serie 115200 bauds) :
 *   - le pourcentage de temps ou D5 est a l'etat HAUT (liaison au repos ~ 90-100 %)
 *   - le nombre total d'octets recus et le texte qui arrive
 * D5 a 0 %  -> aucune tension n'arrive : verifier le fil D13 -> pont diviseur -> D5 et le GND commun
 */
#include <SoftwareSerial.h>

SoftwareSerial arduinoSerial(D5, D6);   // RX = D5, TX = D6
unsigned long octets = 0;
unsigned long dernier = 0;
long nbHaut = 0, nbBas = 0;

void setup() {
  Serial.begin(115200);
  arduinoSerial.begin(9600);
  Serial.println();
  Serial.println("Diagnostic D5 demarre");
}

void loop() {
  while (arduinoSerial.available()) {
    char c = arduinoSerial.read();
    octets++;
    Serial.print(c);
  }
  if (digitalRead(D5) == HIGH) nbHaut++; else nbBas++;

  if (millis() - dernier >= 1000) {
    dernier = millis();
    Serial.println();
    Serial.print(">>> D5 a l'etat HAUT : ");
    Serial.print(100L * nbHaut / (nbHaut + nbBas));
    Serial.print(" %   | octets recus au total : ");
    Serial.println(octets);
    nbHaut = 0; nbBas = 0;
  }
}
