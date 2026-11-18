/*
 * NodeMCU ESP8266 : passerelle Arduino -> WiFi
 * - recoit les trames JSON de l'Arduino par UART logiciel (D5 = RX, D6 = TX, 9600 bauds)
 * - se connecte au WiFi et affiche son adresse IP dans le moniteur serie (115200 bauds)
 * - serveur web sur le port 8081 :
 *     http://<IP>:8081/       -> page de test
 *     http://<IP>:8081/data   -> derniere trame JSON (lue par l'interface NiceGUI)
 *     http://<IP>:8081/cmd?c=MARCHE (ARRET, ACQ, TOUCHE:1..9) -> commande transmise a l'Arduino par D6
 * - rappelle son adresse IP dans le moniteur serie toutes les 10 s
 *
 * Carte a choisir dans l'IDE : "NodeMCU 1.0 (ESP-12E Module)"
 */

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <SoftwareSerial.h>

// ================== A MODIFIER ==================
const char* WIFI_SSID = "NOM_DU_WIFI";        // <-- a remplacer (2,4 GHz)
const char* WIFI_PASS = "MOT_DE_PASSE_WIFI";  // <-- a remplacer
const int   PORT_WEB  = 8081;
// ================================================

SoftwareSerial arduinoSerial(D5, D6);   // RX = D5, TX = D6
ESP8266WebServer server(PORT_WEB);

String tampon = "";
String derniereTrame = "";
unsigned long derniereReception = 0;
unsigned long nbTrames = 0;
unsigned long dernierRappel = 0;

// Lecture non bloquante de l'UART : une trame = une ligne terminee par '\n'
void lireArduino() {
  while (arduinoSerial.available()) {
    char c = arduinoSerial.read();
    if (c == '\n') {
      tampon.trim();
      if (tampon.startsWith("{") && tampon.endsWith("}")) {   // trame complete
        derniereTrame = tampon;
        derniereReception = millis();
        nbTrames++;
        Serial.print("Recu : ");
        Serial.println(derniereTrame);
      }
      tampon = "";
    } else if (c != '\r') {
      tampon += c;
      if (tampon.length() > 400) tampon = "";   // protection contre une ligne corrompue
    }
  }
}

// Affiche l'adresse a recopier dans l'interface NiceGUI
void afficherAdresse() {
  Serial.print("Adresse a mettre dans NiceGUI : ");
  Serial.print(WiFi.localIP());
  Serial.print(":");
  Serial.println(PORT_WEB);
}

// GET /data -> derniere trame + age (ms) de la trame
void handleData() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  if (derniereTrame.length() == 0) {
    server.send(200, "application/json", "{\"age_ms\":-1}");
    return;
  }
  String rep = derniereTrame.substring(0, derniereTrame.length() - 1);
  rep += ",\"age_ms\":";
  rep += String(millis() - derniereReception);
  rep += "}";
  server.send(200, "application/json", rep);
}

// GET /cmd?c=MARCHE | ARRET | ACQ | TOUCHE:1 ... TOUCHE:9
// -> transmet la commande a l'Arduino par D6 (une commande par ligne)
void handleCmd() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  String c = server.arg("c");
  bool valide = (c == "MARCHE" || c == "ARRET" || c == "ACQ");
  if (c.startsWith("TOUCHE:") && c.length() == 8 && c.charAt(7) >= '1' && c.charAt(7) <= '9') valide = true;
  if (!valide) {
    server.send(400, "application/json", "{\"ok\":false,\"erreur\":\"commande inconnue\"}");
    return;
  }
  arduinoSerial.println(c);
  Serial.print("Commande envoyee a l'Arduino : ");
  Serial.println(c);
  server.send(200, "application/json", "{\"ok\":true,\"commande\":\"" + c + "\"}");
}

// GET / -> page de test
void handleRoot() {
  String page = "<html><head><meta charset='utf-8'><meta http-equiv='refresh' content='2'></head><body>";
  page += "<h2>NodeMCU ESP8266 - passerelle Arduino</h2>";
  page += "<p>Trames recues : " + String(nbTrames) + "</p>";
  page += "<p>Derniere trame :</p><pre>" + (derniereTrame.length() ? derniereTrame : String("(aucune)")) + "</pre>";
  page += "<p>Donnees JSON : <a href='/data'>/data</a></p></body></html>";
  server.send(200, "text/html", page);
}

void setup() {
  Serial.begin(115200);
  arduinoSerial.begin(9600);
  Serial.println();
  Serial.print("Connexion au WiFi ");
  Serial.print(WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.println("WiFi connecte !");
  afficherAdresse();

  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.on("/cmd", handleCmd);
  server.begin();
  Serial.print("Serveur web demarre (port ");
  Serial.print(PORT_WEB);
  Serial.println(")");
}

void loop() {
  lireArduino();
  server.handleClient();

  // Rappel de l'adresse toutes les 10 s (utile si on ouvre le moniteur plus tard)
  if (millis() - dernierRappel >= 10000) {
    dernierRappel = millis();
    if (WiFi.status() == WL_CONNECTED) afficherAdresse();
    else Serial.println("WiFi deconnecte, reconnexion en cours...");
  }
}
