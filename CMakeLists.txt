idf_component_register(SRCS "WiFiStation.cpp"
                            "tasks/CUDPOut.cpp"
                            "tasks/CUDPInTask.cpp"
                            "tasks/CTCPClientTask.cpp"
                    INCLUDE_DIRS "include"
                    REQUIRES task nvs_flash esp_wifi lwip dataformat)
