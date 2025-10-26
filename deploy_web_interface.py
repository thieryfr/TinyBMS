#!/usr/bin/env python3
"""
TinyBMS-Victron Bridge - SPIFFS Deployment Script
Automatise la copie et l'upload des fichiers web vers ESP32
"""

import os
import sys
import shutil
from pathlib import Path

# Configuration
PROJECT_ROOT = Path(__file__).parent.parent
DATA_SOURCE = PROJECT_ROOT / "web-interface" / "data"
DATA_TARGET = PROJECT_ROOT / "data"

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
    
    return True

def copy_web_files():
    """Copie les fichiers web dans le dossier data/"""
    print_header("Copie des fichiers web")
    
    if not DATA_SOURCE.exists():
        print(f"❌ Dossier source non trouvé: {DATA_SOURCE}")
        return False
    
    # Créer le dossier data/ s'il n'existe pas
    DATA_TARGET.mkdir(exist_ok=True)
    
    # Copier tous les fichiers
    files_copied = 0
    for file in DATA_SOURCE.glob("*.*"):
        target_file = DATA_TARGET / file.name
        shutil.copy2(file, target_file)
        print(f"✅ Copié: {file.name}")
        files_copied += 1
    
    print(f"\n📦 {files_copied} fichiers copiés dans {DATA_TARGET}")
    return True

def check_spiffs_config():
    """Vérifie la configuration SPIFFS dans platformio.ini"""
    print_header("Vérification configuration SPIFFS")
    
    platformio_ini = PROJECT_ROOT / "platformio.ini"
    content = platformio_ini.read_text()
    
    if "board_build.filesystem = spiffs" in content:
        print("✅ SPIFFS configuré dans platformio.ini")
        return True
    else:
        print("⚠️  SPIFFS non configuré")
        print("\nAjouter dans platformio.ini:")
        print("  board_build.filesystem = spiffs")
        
        response = input("\nVoulez-vous que je l'ajoute automatiquement ? (o/n): ")
        if response.lower() == 'o':
            with open(platformio_ini, 'a') as f:
                f.write("\nboard_build.filesystem = spiffs\n")
            print("✅ Configuration ajoutée")
            return True
        return False

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
    print("  TinyBMS-Victron Bridge - Déploiement Interface Web")
    print("🔋" * 35)
    
    # 1. Vérifier prérequis
    if not check_prerequisites():
        sys.exit(1)
    
    # 2. Copier fichiers
    if not copy_web_files():
        sys.exit(1)
    
    # 3. Vérifier config SPIFFS
    if not check_spiffs_config():
        print("\n⚠️  Configuration SPIFFS requise")
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
    print("\n💡 Commande pour voir les logs:")
    print("     pio device monitor\n")

if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\n\n⚠️  Déploiement interrompu par l'utilisateur")
        sys.exit(1)
    except Exception as e:
        print(f"\n❌ Erreur: {e}")
        sys.exit(1)
