#pragma once

#include <Arduino.h>
#include <cstdint>
#include "tiny_rw_mapping.h"
#include "tiny_read_mapping.h"
#include "shared_data.h"

namespace mqtt {

struct BrokerSettings {
    String uri;                 ///< URI du broker (ex. mqtt://192.168.1.10)
    uint16_t port = 1883;       ///< Port TCP du broker
    String client_id;           ///< Identifiant client MQTT
    String username;            ///< Nom d'utilisateur pour l'authentification
    String password;            ///< Mot de passe pour l'authentification
    String root_topic;          ///< Préfixe racine pour les topics publiés
    bool clean_session = true;  ///< Indique si la session doit être nettoyée à la connexion
    bool use_tls = false;       ///< Active la connexion TLS si true
    String server_certificate;  ///< Certificat CA optionnel pour TLS
    uint16_t keepalive_seconds = 30; ///< Intervalle keepalive MQTT
    uint32_t reconnect_interval_ms = 5000; ///< Attente entre deux tentatives de reconnexion
    uint8_t default_qos = 0;    ///< QoS par défaut pour les publications (0, 1 ou 2)
    bool retain_by_default = false; ///< Flag retain par défaut pour les publications
};

struct RegisterValue {
    uint16_t address = 0;                       ///< Adresse primaire TinyBMS
    String key;                                 ///< Nom symbolique du registre
    String label;                               ///< Libellé utilisateur
    String unit;                                ///< Unité physique
    String comment;                             ///< Commentaire/documentation
    TinyRegisterValueClass value_class = TinyRegisterValueClass::Unknown;
    TinyRegisterValueType wire_type = TinyRegisterValueType::Unknown;
    bool has_numeric_value = false;             ///< true si numeric_value est valide
    float numeric_value = 0.0f;                 ///< Valeur mise à l'échelle (unité utilisateur)
    int32_t raw_value = 0;                      ///< Valeur brute (premier mot ou recomposition)
    uint8_t raw_word_count = 0;                 ///< Nombre de mots Modbus associés
    uint16_t raw_words[TINY_REGISTER_MAX_WORDS] = {0}; ///< Buffer brut complet
    bool has_text_value = false;                ///< true si text_value contient une donnée
    String text_value;                          ///< Valeur texte pour les registres chaîne
    float scale = 1.0f;                         ///< Échelle appliquée côté TinyBMS
    float offset = 0.0f;                        ///< Offset appliqué côté TinyBMS
    uint8_t precision = 0;                      ///< Précision suggérée pour l'affichage
    float default_value = 0.0f;                 ///< Valeur par défaut issue du mapping RW
    String topic_suffix;                        ///< Segment de topic conseillé (sans racine)
    uint32_t timestamp_ms = 0;                  ///< Timestamp de capture (ms)
};

class Publisher {
public:
    virtual ~Publisher() = default;

    virtual void configure(const BrokerSettings& settings) = 0;
    virtual bool connect() = 0;
    virtual void disconnect() = 0;
    virtual void loop() = 0;
    virtual bool publishRegister(const RegisterValue& value, uint8_t qos_override = 255, bool retain_override = false) = 0;
    virtual bool isConnected() const = 0;
};

bool buildRegisterValue(const TinyRegisterRuntimeBinding& binding,
                        int32_t raw_value,
                        float scaled_value,
                        const String* text_value,
                        const uint16_t* raw_words,
                        uint32_t timestamp_ms,
                        RegisterValue& out);

} // namespace mqtt

