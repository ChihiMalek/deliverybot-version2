# =====================================================================
#  DeliveryBot V2 - Supervision NiceGUI (reception HTTP depuis l'ESP8266)
#  L'ESP8266 envoie un POST JSON sur  http://IP_DU_PC:8081/deliverybot
#  Interface web :                    http://localhost:8081
#  Installation :  pip install nicegui
#  Lancement    :  python deliverybot_nicegui.py
#  Toutes les mesures sont enregistrees dans deliverybot_donnees.csv
# =====================================================================
import csv
import os
import socket
import time
from collections import deque
from datetime import datetime

from fastapi import Request
from nicegui import app, ui

FICHIER_CSV = 'deliverybot_donnees.csv'
HISTORIQUE = deque(maxlen=120)          # 2 minutes de donnees (1 mesure / s)
dernier = {}                            # derniere mesure recue
etat = {'t_reception': 0.0, 'nb_recus': 0}

COLONNES = ['horodatage', 'dist_us_cm', 'dist_gauche_cm', 'dist_droite_cm', 'servo_deg',
            'ldr_adc', 'lm35_c', 'dht_temp_c', 'dht_hum_pct', 'i_pv_ma', 'i_sys_ma',
            'soc_est_pct', 'v_bat_v', 'regime_energie', 'source_active', 'mission', 'pwm_g', 'pwm_d',
            'table_cible', 'pos_x', 'pos_y', 'dist_cible_cm', 'erreur_cap_deg', 'a_la_base',
            'derniere_action', 'action', 'source', 'mode', 'courant_simule']

NOM_MODE = {0: 'ARRET', 1: 'AUTO', 2: 'MANUEL'}
NOM_MISSION = {0: 'LIVRAISON', 1: 'RETOUR_BASE', 2: 'RECHARGE_BASE', 3: 'ATTENTE'}
NOM_REGIME = {0: 'NOMINAL', 1: 'DEGRADE', 2: 'SURVIE'}

# Carte : base + 9 tables (meme carte que le programme STM32)
BASE = [0.5, 0.75]
TABLES = [[1.5 + 1.5 * (t % 3), 1.5 + 1.5 * (t // 3)] for t in range(9)]
ALLEES = [0.75, 2.25, 3.75, 5.25]       # allees libres entre les tables (x et y)


# ------------------------- Reception HTTP -------------------------
def enregistrer_csv(mesure: dict) -> None:
    nouveau = not os.path.exists(FICHIER_CSV)
    with open(FICHIER_CSV, 'a', newline='', encoding='utf-8') as f:
        w = csv.DictWriter(f, fieldnames=COLONNES, extrasaction='ignore')
        if nouveau:
            w.writeheader()
        w.writerow(mesure)


@app.post('/deliverybot')
async def recevoir(request: Request):
    try:
        mesure = await request.json()
    except Exception:
        return {'ok': False, 'erreur': 'JSON invalide'}
    mesure['horodatage'] = datetime.now().isoformat(timespec='seconds')
    dernier.clear()
    dernier.update(mesure)
    HISTORIQUE.append(mesure)
    etat['t_reception'] = time.time()
    etat['nb_recus'] += 1
    enregistrer_csv(mesure)
    print(f"[{mesure['horodatage'][11:]}] recu de l'ESP : action={mesure.get('action')} "
          f"dist={mesure.get('dist_us_cm')} cm  SoC={mesure.get('soc_est_pct')} %")
    return {'ok': True}


# ------------------------- Outils d'affichage -------------------------
def val(cle, decimales=1, unite=''):
    v = dernier.get(cle)
    if v is None:
        return '--'
    if isinstance(v, (int, float)):
        return f'{v:.{decimales}f} {unite}'.strip()
    return str(v)


def serie(cle):
    return [m.get(cle) for m in HISTORIQUE]


def graphique(titre, series, unite=''):
    return ui.echart({
        'title': {'text': titre, 'textStyle': {'fontSize': 13}},
        'tooltip': {'trigger': 'axis'},
        'legend': {'top': 0, 'right': 0},
        'grid': {'left': 45, 'right': 15, 'top': 40, 'bottom': 25},
        'xAxis': {'type': 'category', 'data': []},
        'yAxis': {'type': 'value', 'name': unite},
        'series': [{'name': n, 'type': 'line', 'showSymbol': False, 'data': []} for n in series],
    }).classes('w-full h-56')


# ------------------------- Page web -------------------------
@ui.page('/')
def page():
    ui.label('DeliveryBot V2 - Supervision').classes('text-2xl font-medium')
    connexion = ui.label().classes('text-sm')

    # --- Etat du robot ---
    with ui.row().classes('w-full gap-4'):
        cartes = {}
        for titre, cle in [('Mode', 'mode'), ('Mission', 'mission'), ('Action', 'action'),
                           ('Table', 'table_cible'), ('Source EMS', 'source'), ('Regime', 'regime_energie')]:
            with ui.card().classes('min-w-[150px]'):
                ui.label(titre).classes('text-xs text-gray-500')
                cartes[cle] = ui.label('--').classes('text-lg font-medium')

    # --- Mesures ---
    with ui.row().classes('w-full gap-4'):
        mesures = {}
        for titre, cle, dec, unite in [
                ('Distance avant', 'dist_us_cm', 0, 'cm'), ('Distance gauche', 'dist_gauche_cm', 0, 'cm'),
                ('Distance droite', 'dist_droite_cm', 0, 'cm'), ('Servo', 'servo_deg', 0, 'deg'),
                ('LM35 (pack)', 'lm35_c', 1, 'C'), ('DHT11 temp.', 'dht_temp_c', 1, 'C'),
                ('DHT11 humidite', 'dht_hum_pct', 0, '%'), ('LDR', 'ldr_adc', 0, ''),
                ('Courant PV', 'i_pv_ma', 0, 'mA'), ('Courant systeme', 'i_sys_ma', 0, 'mA'),
                ('SoC estime', 'soc_est_pct', 1, '%'), ('Tension pack', 'v_bat_v', 2, 'V'),
                ('PWM G / D', 'pwm_g', 0, '%')]:
            with ui.card().classes('min-w-[150px]'):
                ui.label(titre).classes('text-xs text-gray-500')
                mesures[cle] = (ui.label('--').classes('text-lg font-medium'), dec, unite)

    # --- Graphiques ---
    with ui.grid(columns=2).classes('w-full'):
        g_dist = graphique('Distance ultrason', ['dist_us_cm'], 'cm')
        g_temp = graphique('Temperatures', ['lm35_c', 'dht_temp_c'], 'C')
        g_cour = graphique('Courants', ['i_pv_ma', 'i_sys_ma'], 'mA')
        g_soc = graphique('Etat de charge', ['soc_est_pct'], '%')

    # --- Carte de la salle ---
    carte = ui.echart({
        'title': {'text': 'Position du robot (salle 6 x 6 m)', 'textStyle': {'fontSize': 13}},
        'grid': {'left': 40, 'right': 20, 'top': 40, 'bottom': 30},
        'xAxis': {'min': 0, 'max': 6, 'name': 'x (m)'},
        'yAxis': {'min': 0, 'max': 6, 'name': 'y (m)'},
        'tooltip': {'trigger': 'item'},
        'series': [
            {'name': 'Tables', 'type': 'scatter', 'symbol': 'rect', 'symbolSize': 22,
             'data': TABLES, 'label': {'show': True, 'formatter': '{@[2]}'}},
            {'name': 'Base', 'type': 'scatter', 'symbol': 'diamond', 'symbolSize': 18, 'data': [BASE]},
            {'name': 'Trajet', 'type': 'line', 'showSymbol': False, 'data': []},
            {'name': 'Robot', 'type': 'scatter', 'symbolSize': 16, 'data': []},
            {'name': 'Route prevue', 'type': 'line', 'symbolSize': 6, 'data': [],
             'lineStyle': {'type': 'dashed', 'width': 2}},
        ] + [
            {'name': 'Allee', 'type': 'line', 'silent': True, 'showSymbol': False,
             'lineStyle': {'color': '#d0d0d0', 'width': 1, 'type': 'dotted'},
             'data': ([[a, 0.5], [a, 5.5]] if vertical else [[0.5, a], [5.5, a]])}
            for a in ALLEES for vertical in (True, False)
        ],
    }).classes('w-full h-96')
    carte.options['series'][0]['data'] = [[x, y, i + 1] for i, (x, y) in enumerate(TABLES)]

    ui.label('Pointilles gris = allees entre les tables | tirets = route prevue | ligne = trajet effectue') \
        .classes('text-xs text-gray-500')
    with ui.row():
        ui.button('Telecharger le CSV', on_click=lambda: ui.download(FICHIER_CSV)
                  if os.path.exists(FICHIER_CSV) else ui.notify('Aucune donnee enregistree'))
        nb = ui.label()

    # --- Rafraichissement toutes les secondes ---
    def rafraichir():
        age = time.time() - etat['t_reception']
        if etat['t_reception'] == 0:
            connexion.set_text('En attente des donnees de l\'ESP8266...')
        elif age < 3:
            connexion.set_text(f'Connecte - derniere mesure il y a {age:.1f} s')
        else:
            connexion.set_text(f'Pas de donnees depuis {age:.0f} s (verifier ESP8266 / Wi-Fi)')
        nb.set_text(f'{etat["nb_recus"]} mesures recues')
        if not dernier:
            return

        cartes['mode'].set_text(NOM_MODE.get(dernier.get('mode'), '--'))
        cartes['mission'].set_text(NOM_MISSION.get(dernier.get('mission'), '--'))
        cartes['action'].set_text(str(dernier.get('action', '--')))
        cartes['table_cible'].set_text(str(dernier.get('table_cible', '--')))
        cartes['source'].set_text(str(dernier.get('source', '--')))
        cartes['regime_energie'].set_text(NOM_REGIME.get(dernier.get('regime_energie'), '--'))

        for cle, (label, dec, unite) in mesures.items():
            label.set_text(val(cle, dec, unite))
        mesures['pwm_g'][0].set_text(f'{dernier.get("pwm_g", 0)} / {dernier.get("pwm_d", 0)} %')

        heures = [m['horodatage'][11:] for m in HISTORIQUE]
        for g in (g_dist, g_temp, g_cour, g_soc):
            g.options['xAxis']['data'] = heures
            for s in g.options['series']:
                s['data'] = serie(s['name'])
            g.update()

        carte.options['series'][2]['data'] = [[m.get('pos_x'), m.get('pos_y')] for m in HISTORIQUE
                                              if m.get('pos_x') is not None]
        if dernier.get('pos_x') is not None:
            carte.options['series'][3]['data'] = [[dernier['pos_x'], dernier['pos_y']]]
            route = dernier.get('route') or []
            carte.options['series'][4]['data'] = ([[dernier['pos_x'], dernier['pos_y']]] + route) if route else []
        carte.update()

    ui.timer(1.0, rafraichir)


def ip_du_pc():
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(('8.8.8.8', 80))          # aucune donnee envoyee : sert juste a trouver l'IP Wi-Fi
        ip = s.getsockname()[0]
        s.close()
        return ip
    except OSError:
        return 'inconnue (taper ipconfig)'


PORT = 8081
print('=' * 60)
print(f'  Adresse a mettre dans URL_SERVEUR de l\'ESP8266 :')
print(f'  http://{ip_du_pc()}:{PORT}/deliverybot')
print(f'  Interface web : http://localhost:{PORT}')
print('=' * 60)
ui.run(host='0.0.0.0', port=PORT, title='DeliveryBot V2', reload=False)
