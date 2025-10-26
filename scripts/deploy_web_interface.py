#!/usr/bin/env python3
"""
TinyBMS-Victron Bridge - SPIFFS Deployment Script
Automatise l'upload des fichiers du dossier data/ vers ESP32
"""

import os
import sys
import shutil
from pathlib import Path

# Configuration
PROJECT_ROOT = Path(__file__).parent.parent
DATA_DIR = PROJECT_ROOT / "data"

def print_header(text):
    """Affiche un en-tête formaté"""
    print("\n" + "=" * 70)
    print(f"  {text}")
    print("=" * 70)

def check_prerequisites():
    """Vérifie que les prérequis sont présents"""
    print_header("Vérification des prérequis")

    # Vérifier PlatformIO
    if shutil.which("pio") is None:
        print("❌ PlatformIO CLI non trouvé")
        print("   Installer avec: pip install platformio")
        return False
    print("✅ PlatformIO CLI trouvé")

    # Vérifier platformio.ini
    if not (PROJECT_ROOT / "platformio.ini").exists():
        print("❌ platformio.ini non trouvé")
        print("   Assurez-vous d'être dans le dossier du projet")
        return False
    print("✅ platformio.ini trouvé")

    # Vérifier dossier data/
    if not DATA_DIR.exists():
        print(f"❌ Dossier data/ non trouvé: {DATA_DIR}")
        return False
    print(f"✅ Dossier data/ trouvé")

    return True

def check_data_files():
    """Vérifie le contenu du dossier data/"""
    print_header("Vérification du contenu de data/")

    files = list(DATA_DIR.glob("*.*"))

    if not files:
        print("⚠️  Aucun fichier trouvé dans data/")
        return False

    print(f"📦 {len(files)} fichiers trouvés dans data/:\n")

    total_size = 0
    for file in sorted(files):
        size = file.stat().st_size
        total_size += size
        print(f"   • {file.name:<30} {size:>8,} bytes")

    print(f"\n📊 Taille totale: {total_size:,} bytes ({total_size/1024:.1f} KB)")

    # Vérifier la taille SPIFFS
    partitions_csv = PROJECT_ROOT / "partitions.csv"
    if partitions_csv.exists():
        content = partitions_csv.read_text()
        # Chercher la ligne spiffs (taille = 0x100000 = 1 MB = 1048576 bytes)
        if "spiffs" in content:
            spiffs_size = 1048576  # 1 MB
            usage_percent = (total_size / spiffs_size) * 100
            print(f"💾 SPIFFS allocation: {spiffs_size:,} bytes (1 MB)")
            print(f"📈 Utilisation: {usage_percent:.1f}%")

            if total_size > spiffs_size:
                print("❌ ERREUR: Les fichiers dépassent la taille SPIFFS !")
                return False
            elif usage_percent > 90:
                print("⚠️  ATTENTION: Utilisation SPIFFS > 90%")

    return True

def check_spiffs_config():
    """Vérifie la configuration SPIFFS dans platformio.ini"""
    print_header("Vérification configuration SPIFFS")

    platformio_ini = PROJECT_ROOT / "platformio.ini"
    content = platformio_ini.read_text()

    checks = [
        ("board_build.filesystem = spiffs", "Filesystem SPIFFS"),
        ("data_dir = data", "Data directory"),
        ("board_build.partitions", "Partitions personnalisées")
    ]

    all_ok = True
    for check_str, description in checks:
        if check_str in content:
            print(f"✅ {description}")
        else:
            print(f"❌ {description} manquant")
            all_ok = False

    return all_ok

def upload_spiffs():
    """Upload les fichiers vers l'ESP32"""
    print_header("Upload SPIFFS vers ESP32")

    print("📤 Lancement de l'upload SPIFFS...")
    print("   (Cela peut prendre 1-2 minutes)\n")

    result = os.system("pio run --target uploadfs")

    if result == 0:
        print("\n✅ Upload SPIFFS réussi!")
        return True
    else:
        print("\n❌ Erreur lors de l'upload SPIFFS")
        print("   Vérifier que l'ESP32 est connecté")
        return False

def upload_firmware():
    """Upload le firmware vers l'ESP32"""
    print_header("Upload Firmware vers ESP32")

    response = input("\nVoulez-vous aussi uploader le firmware ? (o/n): ")
    if response.lower() != 'o':
        print("⏭️  Upload firmware ignoré")
        return True

    print("\n📤 Lancement de l'upload firmware...")
    result = os.system("pio run --target upload")

    if result == 0:
        print("\n✅ Upload firmware réussi!")
        return True
    else:
        print("\n❌ Erreur lors de l'upload firmware")
        return False

def main():
    """Fonction principale"""
    print("\n" + "🔋" * 35)
    print("  TinyBMS-Victron Bridge - Déploiement SPIFFS")
    print("🔋" * 35)

    # 1. Vérifier prérequis
    if not check_prerequisites():
        sys.exit(1)

    # 2. Vérifier contenu data/
    if not check_data_files():
        sys.exit(1)

    # 3. Vérifier config SPIFFS
    if not check_spiffs_config():
        print("\n⚠️  Configuration SPIFFS incomplète")
        sys.exit(1)

    # 4. Upload SPIFFS
    if not upload_spiffs():
        sys.exit(1)

    # 5. Upload firmware (optionnel)
    upload_firmware()

    # Résumé final
    print_header("✅ DÉPLOIEMENT TERMINÉ")
    print("\n📝 Prochaines étapes:")
    print("  1. Redémarrer l'ESP32")
    print("  2. Vérifier l'IP dans les logs série")
    print("  3. Accéder à http://[IP-ESP32]/")
    print("\n💡 Commandes utiles:")
    print("     pio device monitor          # Voir les logs série")
    print("     pio run --target uploadfs   # Re-upload SPIFFS")
    print("     pio run --target upload     # Re-upload firmware\n")

if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\n\n⚠️  Déploiement interrompu par l'utilisateur")
        sys.exit(1)
    except Exception as e:
        print(f"\n❌ Erreur: {e}")
        sys.exit(1)
