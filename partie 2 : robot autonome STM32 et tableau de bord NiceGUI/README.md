# DeliveryBot V2 - Part 2 : acquisition des données et navigation entre les tables

DeliveryBot est un robot de livraison pour restaurant, alimenté par batterie et panneau solaire, supervisé en IoT.
Né lors d'un projet tutoré à l'ENIG, il évolue étape par étape vers un vrai robot autonome.

**Cette étape (Part 2)** : le robot, piloté par une **STM32 Nucleo-F439ZI**, lit ses capteurs, se rend à l'une des **9 tables** choisie au clavier en circulant dans les allées, évite les obstacles, puis revient à sa base.
Toutes les mesures partent en **WiFi (ESP8266, HTTP)** vers une plateforme web **NiceGUI** qui les affiche en temps réel et les enregistre dans un fichier CSV : c'est la base de données de la prochaine étape (modèle TinyML embarqué).

<p align="center">
  <img src="photos/robot_vue_ensemble.png" width="48%" alt="DeliveryBot - vue d'ensemble">
  <img src="photos/robot_vue_dessus.png" width="48%" alt="DeliveryBot - vue de dessus">
</p>

---

## Fonctionnalités

- **Navigation vers 9 tables** : une touche (1 à 9) et le robot part. Il longe les allées entre les tables, s'arrête 2 s devant la table (livraison) puis rentre à la base.
- **Changement de destination en route** : un autre chiffre pendant la livraison, et le robot repart vers la nouvelle table depuis sa position.
- **Évitement d'obstacles** : l'ultrason, monté sur un servomoteur, balaie devant le robot. Obstacle à moins de 30 cm : arrêt, regard à gauche et à droite, contournement du côté libre.
- **Mode radar à l'arrêt** : le servo balaie de 10° à 170° et la LED rouge signale un obstacle proche.
- **Mode manuel** au clavier (avancer, reculer, tourner, stop).
- **Gestion de l'énergie (EMS, logique de décision)** : choix de la source entre batterie seule, hybride PV + batterie, PV seul, ou recharge à la base, selon l'éclairement et la mission.
- **Sécurités** : arrêt si la température du pack dépasse 50 °C, retour à la base si le SoC passe sous 20 %.
- **Supervision web NiceGUI** : état du robot, capteurs, courbes, carte de la salle avec la route prévue et le trajet effectué, export CSV.

---

## Architecture

```
Capteurs (LDR, LM35, DHT11, HC-SR04)          Clavier 4x3
              \                                  /
               ---->  STM32 Nucleo-F439ZI  <-----
                      |  (decision, navigation, EMS)
        L298N + 2 moteurs TT, servo SG90
                      |
                 UART 115200 bauds (JSON, 1 mesure/s)
                      |
                ESP8266 NodeMCU  --- WiFi / HTTP POST --->  PC : NiceGUI (port 8081)
                                                              - tableau de bord temps reel
                                                              - carte + route
                                                              - deliverybot_donnees.csv
```

---

## Matériel

| Composant | Rôle |
|---|---|
| STM32 Nucleo-F439ZI | Cerveau du robot (Arduino IDE, STM32duino) |
| ESP8266 NodeMCU | Passerelle WiFi -> HTTP |
| L298N + 2 moteurs TT, châssis 2WD | Propulsion |
| HC-SR04 + servo SG90 | Détection d'obstacles (balayage) |
| LM35 | Température du pack batterie |
| DHT11 | Température et humidité ambiantes |
| LDR | Éclairement (estimation de la production PV) |
| Clavier matriciel 4x3 | Choix de la table / commandes |
| 2 x Li-ion 18650 (2S) + BMS 2S 8A | Batterie 7,4 V |
| Panneau solaire 3,5 W / 6 V | Source d'énergie d'appoint |
| Régulateur LM2596 | 5 V pour l'électronique |

---

## Câblage

![Schéma de câblage](docs/cablage.png)

| Composant | Broche du composant | Broche STM32 |
|---|---|---|
| LDR | diviseur 3,3 V - LDR - **A0** - 10 kΩ - GND | A0 |
| LM35 | Vout (+Vs sur 5 V) | A1 |
| DHT11 | DATA | D3 |
| HC-SR04 | Trig / Echo (via diviseur 1k/2k) | A5 / D8 |
| Servo SG90 | Signal | D9 |
| L298N | IN1 (sens G) / IN2 (PWM G) / IN3 (PWM D) / IN4 (sens D) | D7 / D6 / D5 / D4 |
| Clavier 4x3 | Lignes 1 à 4 / Colonnes 1 à 3 | D10, D2, D13, D14 / A4, A2, A3 |
| ESP8266 | RX / TX | D1 (TX) / D0 (RX) |
| LED obstacle | LED rouge intégrée à la carte | LED_RED |

**Alimentation** : batterie 2S -> BMS -> **P+** vers le **+12V (VS) du L298N** et, en parallèle, vers le régulateur LM2596 (5 V pour capteurs, servo et ESP). Toutes les masses sont communes.

> Le L298N ne doit pas être alimenté par le régulateur 5 V : il perd 2 à 3 V et les moteurs n'auraient plus assez de tension pour démarrer.

---

## Utilisation du clavier

| Touche | Action |
|---|---|
| **1 à 9** | Livrer la table correspondante (départ immédiat) |
| **#** | Retour à la base |
| **\*** | Arrêt d'urgence |
| **0** | Mode manuel : **2** avancer, **8** reculer, **4** gauche, **6** droite, **5** stop (0 à nouveau pour quitter) |

Le robot démarre **à la base, tourné vers les tables**. La salle de référence fait 6 x 6 m ; pour tester dans une petite pièce, réduire la carte avec `#define ECHELLE 0.3` dans le code STM32.

---

## Installation

### 1. STM32 (`firmware/stm32/DeliveryBot_STM32`)
- Arduino IDE + paquet **STM32duino** (carte *Nucleo-144 -> Nucleo F439ZI*).
- Téléverser `DeliveryBot_STM32.ino`. Moniteur série : **115200 bauds** (une ligne d'état par seconde).

### 2. ESP8266 (`firmware/esp8266/DeliveryBot_ESP8266`)
- Carte *NodeMCU 1.0 (ESP-12E Module)*.
- Renseigner `WIFI_NOM`, `WIFI_MDP` (réseau **2,4 GHz**, sécurité **WPA2**) et `URL_SERVEUR` (IP du PC, obtenue avec `ipconfig`).
- Débrancher TX/RX avant le téléversement, puis les rebrancher.

### 3. Supervision NiceGUI (`supervision/`)
```bash
pip install -r requirements.txt
python deliverybot_nicegui.py
```
- Au démarrage, le programme affiche l'adresse exacte à copier dans `URL_SERVEUR`.
- Interface : **http://localhost:8081**.
- Sous Windows, autoriser le port 8081 si l'ESP n'arrive pas à joindre le PC :
  `netsh advfirewall firewall add rule name="NiceGUI DeliveryBot" dir=in action=allow protocol=TCP localport=8081`

### 4. Tests unitaires (`tests/`)
- `Test_Moteurs` : avancer / gauche / droite / arrêt.
- `Test_Servo_Ultrason` : balayage radar et mesure de distance.

---

## Résultats

<p align="center">
  <img src="photos/supervision_tableau.jpg" width="90%" alt="Tableau de bord NiceGUI">
</p>
<p align="center">
  <img src="photos/supervision_carte.jpg" width="90%" alt="Carte de la salle : allées, route prévue et trajet effectué">
</p>

Exemple ci-dessus : livraison à la table 8, puis à la table 3, puis retour à la base, en passant uniquement par les allées entre les tables.

---

## Données enregistrées

Chaque seconde, une ligne est ajoutée à `supervision/deliverybot_donnees.csv` :
distances (avant, gauche, droite), angle du servo, LDR, LM35, DHT11, courants PV et système, SoC et tension estimés, régime énergétique, source EMS, mission, PWM des moteurs, table, position estimée, distance et erreur de cap vers la cible, action décidée, mode.

**Limites actuelles, à connaître avant d'utiliser les données :**
- Les **courants** sont pour l'instant **modélisés** à partir des fiches techniques des composants (colonne `courant_simule = 1`), en attendant la calibration des capteurs de courant.
- La **position** est estimée par odométrie à l'estime (sans encodeurs) : `V_MAX` et `VOIE` doivent être calibrés sur le robot réel.
- Le **SoC** est estimé par comptage de coulombs depuis une valeur initiale (`SOC_INITIAL`).

---

## Ce que nous avons appris

- Un moteur qui siffle sans tourner : presque toujours un problème d'alimentation, pas de code.
- Une mesure qui « suit » un autre capteur : souvent un fil de masse sur la mauvaise rangée de la plaque d'essai.
- Sur STM32duino : `pulseIn` remplacé par une mesure avec `micros()`, une broche passée en `analogWrite` doit rester en `analogWrite`, `Uart` au lieu de `HardwareSerial`.
- Quand rien ne marche : tester chaque composant seul. C'est ce qui nous a débloqués à chaque fois.

---

## Prochaine étape - Part 3

Remplacer la logique de décision codée à la main par un **modèle d'intelligence artificielle embarqué sur la STM32** (TinyML) :
réseau de neurones entraîné sous **TensorFlow**, puis **apprentissage par renforcement**, pour décider de la commande des moteurs (vitesse, direction) et de la source d'énergie.
Les règles de sécurité resteront codées en dur dans le firmware.

---

## Équipe

- **Malek Chihi** - Génie Électrique & Automatique, ENIG - [LinkedIn](https://www.linkedin.com/in/malekchihi)
- **Ilyes Harrathi** - [LinkedIn](https://www.linkedin.com/in/ilyes-harrathi)
- **Wissal Ghazouani** - [LinkedIn](https://www.linkedin.com/in/wissal-ghazouani-3a5035362)

Projet réalisé à l'École Nationale d'Ingénieurs de Gabès (ENIG).
