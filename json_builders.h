/**
 * @file json_builders.h
 * @brief JSON response builders for API endpoints
 * @version 1.0
 */

#ifndef JSON_BUILDERS_H
#define JSON_BUILDERS_H

#include <Arduino.h>

/**
 * @brief Build complete status JSON including live data, stats, and watchdog
 */
String getStatusJSON();

/**
 * @brief Build TinyBMS configuration JSON
 */
String getConfigJSON();

/**
 * @brief Build system configuration JSON
 */
String getSystemConfigJSON();

#endif // JSON_BUILDERS_H
