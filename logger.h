/**
 * @file logger.h
 * @brief Gestion des logs avec niveaux et stockage persistant pour TinyBMS-Victron Bridge
 * @version 1.0
 *
 * Fournit une classe Logger pour gérer les logs avec différents niveaux (ERROR, WARNING, INFO, DEBUG),
 * stocker les logs sur SPIFFS, et permettre leur récupération via l'interface web.
 */

#ifndef LOGGER_H
#define LOGGER_H

#include <Arduino.h>
#include <FS.h>
#include <SPIFFS.h>
#include "rtos_tasks.h"
#include "config_manager.h"

enum LogLevel {
    LOG_ERROR = 0,   // Erreurs critiques (ex. : échec UART)
    LOG_WARNING = 1, // Avertissements (ex. : feed watchdog tardif)
    LOG_INFO = 2,    // Informations générales (ex. : initialisation)
    LOG_DEBUG = 3    // Débogage détaillé (ex. : surveillance pile)
};

class Logger {
public:
    Logger();
    ~Logger();

    /**
     * @brief Initialise le logger avec la configuration
     * @param config Référence à ConfigManager pour lire le niveau de log
     * @return true si l'initialisation réussit, false sinon
     */
    bool begin(const ConfigManager& config);

    /**
     * @brief Enregistre un message avec le niveau spécifié
     * @param level Niveau du log (ERROR, WARNING, INFO, DEBUG)
     * @param message Message à enregistrer
     */
    void log(LogLevel level, const String& message);

    /**
     * @brief Modifie le niveau de journalisation
     * @param level Nouveau niveau (ERROR, WARNING, INFO, DEBUG)
     */
    void setLogLevel(LogLevel level);

    /**
     * @brief Récupère les logs stockés dans /logs.txt
     * @return String contenant les logs
     */
    String getLogs();

private:
    LogLevel current_level_;      // Niveau de journalisation actuel
    File log_file_;               // Fichier de logs sur SPIFFS
    SemaphoreHandle_t log_mutex_; // Mutex pour protéger l'accès au fichier
    bool initialized_;            // État de l'initialisation

    /**
     * @brief Ouvre le fichier de logs (/logs.txt) en mode append
     * @return true si l'ouverture réussit, false sinon
     */
    bool openLogFile();

    /**
     * @brief Gère la rotation du fichier de logs si sa taille dépasse 100 Ko
     */
    void rotateLogFile();
};

#endif