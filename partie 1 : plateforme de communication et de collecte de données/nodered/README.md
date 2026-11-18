# Node-RED — essai de collecte de données

Premier essai de collecte : Node-RED lit directement les trames JSON envoyées par l'Arduino sur le port USB, trace la distance en temps réel et enregistre les mesures dans un fichier CSV (lisible dans Excel).

> Essai en cours : ce flux est indépendant de la plateforme NiceGUI. Il sert à tester l'enregistrement des données.

## Le flux

```
serial in (COM, 9600 bauds) → json → function → ui_chart (courbe de la distance)
                                            └→ csv (séparateur ;) → file (data.csv)
```

| Nœud | Rôle |
|---|---|
| `serial in` | Lit le port série de l'Arduino (9600 bauds, une trame par ligne `\n`) |
| `json` | Convertit la ligne texte en objet JavaScript |
| `function` | Ignore les mesures invalides (distance = -1), ajoute la date et l'heure, prépare deux sorties |
| `ui_chart` | Courbe de la distance sur les 2 dernières minutes (dashboard Node-RED, onglet « Obstacle ») |
| `csv` + `file` | Ajoute une ligne `Date;Heure;Distance_cm;Obstacle` au fichier `data.csv` (en-tête écrit une seule fois) |

## Utilisation

1. Installer les modules Node-RED : `node-red-node-serialport` et `node-red-dashboard` (Menu → Gérer la palette).
2. Menu → **Importer** → choisir `flows.json`.
3. Ouvrir le nœud de configuration du port série et choisir le **port COM de l'Arduino**.
4. Modifier le chemin du nœud `file` si besoin (par défaut `C:\deliverybot\data.csv`, dossier créé automatiquement).
5. **Déployer**, puis ouvrir le dashboard : http://localhost:1880/ui

Un port COM ne peut être ouvert que par un seul programme : fermer le moniteur série de l'IDE Arduino avant de déployer le flux.
