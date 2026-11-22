// =====================================================
//  DeliveryBot V2 - ESP8266 : passerelle STM32 -> HTTP -> NiceGUI
//  Carte : NodeMCU 1.0 (ESP-12E Module)
//  RX <- D1 de la STM32 | TX -> D0 de la STM32 | GND commun
//  Rappel : debrancher TX/RX avant de televerser ce code.
//  Wi-Fi du telephone : bande 2,4 GHz + securite WPA2
// =====================================================
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>

// ---------------- A REMPLIR ----------------
const char* WIFI_NOM    = "NOM_DU_WIFI";        // reseau 2,4 GHz, WPA2
const char* WIFI_MDP    = "MOT_DE_PASSE_WIFI";
const char* URL_SERVEUR = "http://192.168.X.X:8081/deliverybot";   // IP du PC (ipconfig) qui fait tourner NiceGUI
// -------------------------------------------

WiFiClient client;

void clignoter(int fois, int duree) {
  for (int i = 0; i < fois; i++) {
    digitalWrite(LED_BUILTIN, LOW);  delay(duree);   // LED bleue ON (logique inversee)
    digitalWrite(LED_BUILTIN, HIGH); delay(duree);
  }
}

void connecterWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_NOM, WIFI_MDP);
  Serial.println();
  Serial.print("Connexion a : ");
  Serial.println(WIFI_NOM);
  int essais = 0;
  while (WiFi.status() != WL_CONNECTED) {
    clignoter(1, 250);                       // clignotement lent = connexion en cours
    Serial.print(".");
    if (++essais % 20 == 0) {                // toutes les ~10 s : cause du probleme
      Serial.println();
      int s = WiFi.status();
      if (s == WL_NO_SSID_AVAIL)        Serial.println("Reseau introuvable (nom faux, 5 GHz ou trop loin)");
      else if (s == WL_WRONG_PASSWORD)  Serial.println("Mot de passe incorrect (ou securite WPA3)");
      else if (s == WL_CONNECT_FAILED)  Serial.println("Echec de connexion (verifier WPA2)");
      else { Serial.print("Statut : "); Serial.println(s); }
    }
  }
  Serial.println();
  Serial.print("WIFI OK ");
  Serial.println(WiFi.localIP());
}

void setup() {
  Serial.begin(115200);              // meme vitesse que la STM32
  Serial.setTimeout(200);
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);
  connecterWiFi();
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) connecterWiFi();   // reconnexion automatique

  if (Serial.available()) {
    String ligne = Serial.readStringUntil('\n');
    ligne.trim();
    if (ligne.startsWith("{") && ligne.endsWith("}")) {  // seulement les JSON valides
      HTTPClient http;
      http.begin(client, URL_SERVEUR);
      http.addHeader("Content-Type", "application/json");
      int code = http.POST(ligne);
      http.end();
      Serial.print("HTTP ");
      Serial.println(code);
      if (code == 200) clignoter(1, 30);   // flash court = envoi reussi
    }
  }
}
