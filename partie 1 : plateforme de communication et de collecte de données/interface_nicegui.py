"""
Plateforme de supervision et de commande NiceGUI - Arduino + NodeMCU ESP8266

- Connexion avec deux modes :
    * UTILISATEUR   : supervision seulement (lecture des mesures et de l'etat des LEDs)
    * ADMINISTRATEUR : supervision + commandes (Marche, Arret, Acquittement, clavier 1-9)
- Lecture des donnees chaque seconde : http://<IP>:8081/data
- Envoi des commandes                : http://<IP>:8081/cmd?c=MARCHE (ARRET, ACQ, TOUCHE:n)

Installation :  python -m pip install nicegui httpx
Lancement   :  python interface_nicegui.py
Puis ouvrir :  http://localhost:8090
"""

from datetime import datetime

import httpx
from nicegui import app, ui

# ================== A MODIFIER ==================
ESP_IP_PAR_DEFAUT = "192.168.1.50:8081"      # <-- IP:port affiches par le moniteur serie du NodeMCU
COMPTES = {                                  # identifiant : (mot de passe, role)
    "admin": ("admin123", "admin"),
    "user": ("user123", "utilisateur"),
}
# ================================================

NB_POINTS = 60
COULEURS_ALARME = {"DANGER": "negative", "ATTENTION": "warning", "OK": "positive"}
historique = {"t": [], "V_sys": [], "I_PV": [], "I_sys": [], "temperature": [], "humidite": []}


def carte(titre: str, unite: str = ""):
    """Petite carte avec un titre et une valeur ; renvoie le label de la valeur."""
    with ui.card().classes("w-44 items-center"):
        ui.label(titre).classes("text-sm text-gray-500")
        valeur = ui.label("--").classes("text-2xl font-medium")
        if unite:
            ui.label(unite).classes("text-xs text-gray-400")
    return valeur


def pastille():
    return ui.element("div").style(
        "width:32px;height:32px;border-radius:50%;background:#9e9e9e;border:1px solid #bbb"
    )


# ------------------------------------------------------------------
# Page de connexion
# ------------------------------------------------------------------
@ui.page("/login")
def page_login():
    ui.page_title("Connexion - Supervision")

    def se_connecter():
        compte = COMPTES.get(identifiant.value.strip())
        if compte and compte[0] == mot_de_passe.value:
            app.storage.user.update({"identifiant": identifiant.value.strip(), "role": compte[1]})
            ui.navigate.to("/")
        else:
            erreur.set_text("Identifiant ou mot de passe incorrect")

    with ui.card().classes("absolute-center w-80 items-stretch"):
        ui.label("Plateforme de supervision").classes("text-lg font-medium")
        ui.label("Connexion").classes("text-sm text-gray-500")
        identifiant = ui.input("Identifiant").on("keydown.enter", se_connecter)
        mot_de_passe = ui.input("Mot de passe", password=True, password_toggle_button=True).on(
            "keydown.enter", se_connecter
        )
        erreur = ui.label("").classes("text-sm text-red-600")
        ui.button("Se connecter", on_click=se_connecter)
        ui.label("admin = commandes | user = supervision").classes("text-xs text-gray-400")


# ------------------------------------------------------------------
# Page principale
# ------------------------------------------------------------------
@ui.page("/")
def page():
    role = app.storage.user.get("role")
    if role not in ("admin", "utilisateur"):
        ui.navigate.to("/login")
        return
    est_admin = (role == "admin")

    ui.page_title("Supervision Arduino + ESP8266")
    etat_page = {"derniere_alarme": None, "saisie": ""}

    def deconnexion():
        app.storage.user.clear()
        ui.navigate.to("/login")

    with ui.header().classes("items-center justify-between"):
        ui.label("Plateforme de supervision et de commande - Arduino + ESP8266").classes("text-lg")
        with ui.row().classes("items-center"):
            ui.badge("ADMINISTRATEUR" if est_admin else "UTILISATEUR",
                     color="orange" if est_admin else "teal").classes("text-sm px-2 py-1")
            ui.label(app.storage.user.get("identifiant", ""))
            ui.button("Deconnexion", on_click=deconnexion).props("flat color=white")

    with ui.row().classes("items-center w-full"):
        ip_input = ui.input("Adresse IP:port de l'ESP", value=ESP_IP_PAR_DEFAUT).classes("w-56")
        statut = ui.label("En attente...").classes("text-gray-500")

    bandeau = ui.label("").classes("w-full text-center text-white bg-red-600 rounded p-2 text-base")
    bandeau.visible = False

    # ---------------- Capteurs ----------------
    ui.label("Capteurs reels").classes("text-base font-medium mt-2")
    with ui.row():
        lbl_distance = carte("Distance", "cm")
        lbl_lum = carte("Luminosite", "0 - 1023")
        with ui.card().classes("w-44 items-center"):
            ui.label("Alarme").classes("text-sm text-gray-500")
            badge_alarme = ui.badge("--", color="grey").classes("text-lg px-3 py-1")
            lbl_buzzer = ui.label("Buzzer : --").classes("text-xs text-gray-400")
        lbl_sombre = carte("Sombre")

    # ---------------- Etat du systeme et des LEDs ----------------
    ui.label("Etat du systeme").classes("text-base font-medium mt-2")
    with ui.row():
        with ui.card().classes("w-44 items-center"):
            ui.label("Systeme").classes("text-sm text-gray-500")
            badge_systeme = ui.badge("--", color="grey").classes("text-lg px-3 py-1")
        leds = {}
        for nom, titre in (("led_verte", "LED verte"), ("led_rouge", "LED rouge"), ("led_jaune", "LED jaune")):
            with ui.card().classes("w-36 items-center"):
                ui.label(titre).classes("text-sm text-gray-500")
                leds[nom] = (pastille(), ui.label("--").classes("text-xs text-gray-400"))
        lbl_touche_arduino = carte("Touche recue", "par l'Arduino")
    COULEUR_ON = {"led_verte": "#43a047", "led_rouge": "#e53935", "led_jaune": "#fdd835"}

    # ---------------- Commandes (administrateur seulement) ----------------
    async def envoyer(commande: str, message: str):
        ip = ip_input.value.strip()
        try:
            async with httpx.AsyncClient(timeout=2.0, trust_env=False) as client:
                r = await client.get(f"http://{ip}/cmd", params={"c": commande})
            if r.status_code == 200 and r.json().get("ok"):
                ui.notify(message, type="positive")
            else:
                ui.notify(f"Refus de l'ESP : {r.text}", type="warning")
        except Exception as e:
            print(f"[ERREUR] commande {commande} -> {type(e).__name__}: {e}")
            ui.notify(f"Commande non envoyee : ESP injoignable ({type(e).__name__})", type="negative")

    ui.label("Commandes").classes("text-base font-medium mt-2")
    if est_admin:
        with ui.row().classes("items-start"):
            with ui.card().classes("items-center"):
                ui.label("Mode de fonctionnement").classes("text-sm text-gray-500")
                with ui.row():
                    ui.button("Marche systeme",
                              on_click=lambda: envoyer("MARCHE", "Systeme en MARCHE (LED verte)")).props("color=positive")
                    ui.button("Arret systeme",
                              on_click=lambda: envoyer("ARRET", "Systeme a l'ARRET (LED rouge)")).props("color=negative")
                    ui.button("Acquittement",
                              on_click=lambda: envoyer("ACQ", "Acquittement envoye (LED jaune)")).props("color=warning")

            with ui.card().classes("items-center"):
                ui.label("Clavier").classes("text-sm text-gray-500")
                ecran = ui.label("-").classes(
                    "w-40 text-center text-2xl font-mono bg-gray-900 text-green-400 rounded p-2"
                )

                async def touche(n: int):
                    etat_page["saisie"] = (etat_page["saisie"] + str(n))[-8:]
                    ecran.set_text(etat_page["saisie"])
                    await envoyer(f"TOUCHE:{n}", f"Touche {n} envoyee")

                def effacer():
                    etat_page["saisie"] = ""
                    ecran.set_text("-")

                with ui.grid(columns=3).classes("gap-2"):
                    for n in range(1, 10):
                        ui.button(str(n), on_click=lambda n=n: touche(n)).classes("w-12 h-12 text-lg")
                ui.button("Effacer", on_click=effacer).props("flat")
    else:
        with ui.card():
            ui.label("Mode utilisateur : les commandes sont reservees a l'administrateur.").classes(
                "text-sm text-gray-500"
            )

    # ---------------- Valeurs simulees et courbes ----------------
    ui.label("Valeurs simulees").classes("text-base font-medium mt-2")
    with ui.row():
        lbl_temp = carte("Temperature", "degC")
        lbl_hum = carte("Humidite", "%")
        lbl_ipv = carte("Courant PV", "A")
        lbl_isys = carte("Courant systeme", "A")
        lbl_vsys = carte("Tension systeme", "V")

    def options_courbe(titre, series):
        return {
            "title": {"text": titre, "textStyle": {"fontSize": 14}},
            "tooltip": {"trigger": "axis"},
            "legend": {"top": 24},
            "grid": {"top": 60, "left": 50, "right": 20, "bottom": 30},
            "xAxis": {"type": "category", "data": []},
            "yAxis": {"type": "value", "scale": True},
            "series": [{"name": s, "type": "line", "showSymbol": False, "data": []} for s in series],
        }

    with ui.row().classes("w-full"):
        courbe_elec = ui.echart(options_courbe("Tension et courants", ["V_sys", "I_PV", "I_sys"])).classes("w-[48%] h-72")
        courbe_env = ui.echart(options_courbe("Temperature et humidite", ["temperature", "humidite"])).classes("w-[48%] h-72")

    # ---------------- Mise a jour chaque seconde ----------------
    async def rafraichir():
        ip = ip_input.value.strip()
        try:
            async with httpx.AsyncClient(timeout=2.0, trust_env=False) as client:
                r = await client.get(f"http://{ip}/data")
            d = r.json()
        except Exception as e:
            print(f"[ERREUR] http://{ip}/data -> {type(e).__name__}: {e}")
            statut.set_text(f"ESP injoignable a l'adresse {ip} ({type(e).__name__})")
            statut.classes(replace="text-red-600")
            return

        age = d.get("age_ms", -1)
        if age < 0:
            statut.set_text("ESP connecte, mais aucune donnee recue de l'Arduino")
            statut.classes(replace="text-orange-600")
            return
        if age > 3000:
            statut.set_text(f"ESP connecte, derniere trame Arduino il y a {age / 1000:.0f} s")
            statut.classes(replace="text-orange-600")
        else:
            statut.set_text(f"Connecte a {ip} - mise a jour {datetime.now():%H:%M:%S}")
            statut.classes(replace="text-green-700")

        dist = d.get("distance", -1)
        lbl_distance.set_text("pas d'echo" if dist < 0 else f"{dist:.1f}")
        lbl_lum.set_text(str(d.get("luminosite", "--")))
        lbl_sombre.set_text("OUI" if d.get("sombre") else "NON")

        alarme = d.get("alarme", "--")
        badge_alarme.set_text(alarme)
        badge_alarme.props(f"color={COULEURS_ALARME.get(alarme, 'grey')}")
        lbl_buzzer.set_text("Buzzer : " + ("ON" if d.get("buzzer") else "OFF"))

        # Alerte DANGER
        acquitte = d.get("acquitte", 0)
        if alarme == "DANGER":
            bandeau.set_text("DANGER : obstacle a moins de 10 cm"
                             + (" - alarme acquittee" if acquitte else
                                (" - cliquez sur Acquittement" if est_admin else " - prevenez l'administrateur")))
            bandeau.visible = True
        else:
            bandeau.visible = False
        if alarme == "DANGER" and etat_page["derniere_alarme"] != "DANGER":
            ui.notify("DANGER detecte !", type="negative")
        etat_page["derniere_alarme"] = alarme

        # Etat du systeme et des LEDs
        systeme = d.get("systeme", "--")
        badge_systeme.set_text(systeme)
        badge_systeme.props(f"color={'positive' if systeme == 'MARCHE' else 'negative'}")
        for nom, (p, lbl) in leds.items():
            allumee = bool(d.get(nom, 0))
            p.style(f"background:{COULEUR_ON[nom] if allumee else '#9e9e9e'}")
            lbl.set_text("ALLUMEE" if allumee else "ETEINTE")
        t = d.get("touche", 0)
        lbl_touche_arduino.set_text(str(t) if t else "-")

        lbl_temp.set_text(f"{d.get('temperature', 0):.1f}")
        lbl_hum.set_text(f"{d.get('humidite', 0):.1f}")
        lbl_ipv.set_text(f"{d.get('I_PV', 0):.2f}")
        lbl_isys.set_text(f"{d.get('I_sys', 0):.2f}")
        lbl_vsys.set_text(f"{d.get('V_sys', 0):.2f}")

        historique["t"].append(datetime.now().strftime("%H:%M:%S"))
        for cle in ("V_sys", "I_PV", "I_sys", "temperature", "humidite"):
            historique[cle].append(d.get(cle))
        for cle in historique:
            del historique[cle][:-NB_POINTS]

        courbe_elec.options["xAxis"]["data"] = historique["t"]
        for serie in courbe_elec.options["series"]:
            serie["data"] = historique[serie["name"]]
        courbe_elec.update()

        courbe_env.options["xAxis"]["data"] = historique["t"]
        for serie in courbe_env.options["series"]:
            serie["data"] = historique[serie["name"]]
        courbe_env.update()

    ui.timer(1.0, rafraichir)


if __name__ in {"__main__", "__mp_main__"}:
    ui.run(title="Supervision Arduino + ESP8266", port=8090, reload=False, show=True,
           storage_secret="supervision-enig-2026")
