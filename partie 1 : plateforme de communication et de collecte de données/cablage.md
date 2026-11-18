# Câblage

![Schéma de câblage DeliveryBot - partie 1](cablage.png)

> Version vectorielle (zoom sans perte) : [cablage.svg](cablage.svg)

## Arduino Uno — capteurs et actionneurs

| Composant | Broche Arduino | Remarque |
|---|---|---|
| HC-SR04 — Trig | D2 | VCC → 5V, GND → GND |
| HC-SR04 — Echo | D4 | |
| LDR (point milieu du pont) | A0 | 5V → LDR → A0 → résistance → GND |
| Buzzer (+) | D8 | (−) → GND |
| LED rouge | D6 | via résistance 220 Ω, cathode → GND |
| LED verte | D7 | via résistance 220 Ω, cathode → GND |
| LED jaune | D11 | via résistance 220 Ω, cathode → GND |

## Liaison Arduino Uno ↔ NodeMCU ESP8266 (UART logiciel, 9600 bauds)

| Arduino Uno | Passe par | NodeMCU | Sens |
|---|---|---|---|
| D13 (TX) | pont diviseur 5 V → 3,3 V | D5 (RX) | données capteurs → plateforme |
| D12 (RX) | fil direct | D6 (TX) | commandes plateforme → Arduino |
| GND | fil direct | G | masse commune (obligatoire) |

Pont diviseur (3 résistances identiques, 1 kΩ ou 220 Ω) :

```
Arduino D13 ──[R1]──●──[R2]──[R3]── GND
                    │
                    └── NodeMCU D5   (≈ 3,3 V)
```

- Ne pas utiliser D0/D1 de l'Arduino (réservées à l'USB), ni RX/TX, D3, D4, D8 du NodeMCU.
- Chaque carte reste alimentée par son propre câble USB ; ne pas relier le 5V de l'Arduino au NodeMCU.

## Dépannage rapide

| Symptôme | Cause probable |
|---|---|
| L'interface affiche « ESP injoignable » | PC et NodeMCU pas sur le même WiFi, ou IP changée (voir le moniteur du NodeMCU) |
| « ESP connecté, mais aucune donnée reçue de l'Arduino » | Liaison D13 → D5 coupée : lancer `outils/diagnostic_uart_esp8266` |
| Le NodeMCU reste sur « Connexion au WiFi.... » | Réseau en 5 GHz ou WPA3 : passer en 2,4 GHz / WPA2 |
| Distance toujours à -1 | Pas d'écho : Trig/Echo inversés ou capteur défectueux |
