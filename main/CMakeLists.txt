idf_component_register(SRCS "app_main.cpp" "bridge.cpp" "config.cpp" "diagnostics.cpp"
                        INCLUDE_DIRS "." "../include"
                        REQUIRES mqtt)
