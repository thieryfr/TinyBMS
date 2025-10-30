#pragma once

// Phase 3: Dual WebServer Support
#ifdef USE_ESP_IDF_WEBSERVER
    #include "esp_http_server_wrapper.h"
    using WebServerType = tinybms::web::HttpServerIDF;
#else
    class AsyncWebServer;
    using WebServerType = AsyncWebServer;
#endif

void setupAPIRoutes(WebServerType& server);
void setupTinyBMSConfigRoutes(WebServerType& server);
