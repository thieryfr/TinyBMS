---

# 📚 `readme_cvl.md`: Logique de Contrôle CVL Dynamique

Ce module (`cvl_logic`) implémente la logique du **Charge Voltage Limit (CVL)** et du **Charge Current Limit (CCL)** pour le système BMS, en particulier pour communiquer avec les équipements Victron via CAN-bus (ou un protocole similaire).

La logique combine une approche de charge basée sur l'**État de Charge (SOC)** et une approche de **protection critique** basée sur la tension maximale des cellules ($\text{Vcell}_{\text{max}}$).

---

## 📂 Structure des Fichiers

| Fichier | Rôle | Description |
| :--- | :--- | :--- |
| **`cvl_types.h`** | Définition des États | Contient l'énumération `CVLState` pour la machine à états de la stratégie de charge. |
| **`cvl_logic.h`** | Interfaces et Structures | Définit les structures d'entrée (`CVLInputs`), de configuration (`CVLConfigSnapshot`) et de sortie (`CVLComputationResult`). **Contient les nouvelles entrées pour la protection.** |
| **`cvl_logic.cpp`** | Implémentation de la Logique | Contient la fonction principale `computeCvlLimits` et l'algorithme de **Contrôle Proportionnel (P-Control)** pour la protection cellulaire. |

---

## ⚙️ Dépendances du TinyBMS (Registres d'Entrée)

Le module de calcul du CVL nécessite que le sous-système de communication lise et mette à jour les valeurs de plusieurs registres du TinyBMS, en particulier :

| Registre TinyBMS | ID Rég. (Déc.) | Nom du Champ (`CVLInputs`) | Description | Conversion |
| :--- | :--- | :--- | :--- | :--- |
| **Max Cell Voltage** | **41** | `max_cell_voltage_v` | Tension de la cellule la plus chargée. **Essentiel pour la protection critique.** | $\text{Valeur} / 1000.0$ (mV $\rightarrow$ V) |
| **Num. Series Cells** | **307** | `series_cell_count` | Nombre de cellules en série (N-S). **Essentiel pour les calculs Pack (V).** | $\text{Valeur}$ (sans conversion) |
| *Over-Voltage Cutoff* | *315* | *(Utilisé pour déterminer la constante $\text{V}_{\text{absmax}}$)* | Tension de coupure OVP configurée. | $\text{Valeur} / 1000.0$ (mV $\rightarrow$ V) |

---

## 🛡️ Algorithme de Protection Critique (P-Control)

Pour garantir la sécurité de la batterie (notamment la vôtre, spécifiée à $\mathbf{3.65 \text{ V}}$ max), une formule de **Contrôle Proportionnel (P-Control)** est utilisée pour **surpasser** la logique de charge basée sur le SOC dès que la tension d'une seule cellule approche le danger.

### 1. Constantes Clés (`cvl_logic.cpp`)

| Constante | Valeur Ajustée (LiFePO4 3.65V) | Rôle |
| :--- | :--- | :--- |
| `VCELL_CUTOFF_V` | $\mathbf{3.65 \text{ V}}$ | Tension maximale absolue de la cellule. |
| **`VCELL_SAFETY_THRESHOLD`** | $\mathbf{3.50 \text{ V}}$ | Seuil $\text{Vcell}_{\text{max}}$ où la **réduction du CVL commence** (marge de $\mathbf{0.15 \text{ V}}$ avant la coupure). |
| **`KP_GAIN`** | $\mathbf{150.0}$ | **Gain Proportionnel.** Détermine l'agressivité de la réduction du CVL. Une valeur élevée est nécessaire pour réagir rapidement sur une petite fenêtre. |
| `VCELL_MIN_FLOAT` | $\mathbf{3.20 \text{ V}}$ | Tension minimale par cellule pour définir la limite basse du CVL du pack (évite la décharge par le chargeur/onduleur). |

### 2. Formule Dynamique (Implémentée dans `compute_cell_protection_cvl`)

Le CVL de protection est calculé en fonction de l'erreur ($\text{Erreur}_{\text{V}}$) par rapport au seuil de sécurité.

$$\text{Erreur}_{\text{V}} = \text{max\_cell\_voltage\_v} - \text{VCELL\_SAFETY\_THRESHOLD}$$

$$\text{CVL}_{\text{protection}} = \begin{cases} \text{VCELL\_CUTOFF\_V} \times \text{N}_{\text{cells}} & \text{si } \text{Erreur}_{\text{V}} \le 0 \text{ (Charge normale)} \\ \text{V}_{\text{absmax}} - (\text{K}_{\text{p}} \times \text{Erreur}_{\text{V}}) & \text{si } \text{Erreur}_{\text{V}} > 0 \text{ (Réduction du CVL)} \end{cases}$$

### 3. Logique de Fusion

À la fin de la fonction `computeCvlLimits`, la limite CVL calculée par la machine à états SOC (`result.cvl_voltage_v`) est comparée à la limite calculée par la protection (`protection_cvl`).

$$\text{CVL}_{\text{final}} = \min(\text{CVL}_{\text{soc\_based}}, \text{CVL}_{\text{protection}})$$

Ceci assure que la limite de tension la plus restrictive (la plus basse) est toujours transmise au chargeur.

---

## 🔌 Dépendances (Non-Code)

Pour l'intégration, vous devez :

1.  **Module de Communication :** Mettre en place la lecture périodique des registres TinyBMS **41** et **307**.
2.  **Event Bus :** Configurer le bus d'événements pour qu'il injecte les valeurs converties de ces registres dans la structure `CVLInputs` avant chaque appel à `computeCvlLimits`.
