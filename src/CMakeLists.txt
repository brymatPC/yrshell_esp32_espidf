idf_component_register( SRCS 
                            "VictronDevice.cpp"
                            "UploadDataClient.cpp"
                            "SdLogger.cpp"
                            "BleConnection.cpp"
                            "YRShellEsp32.cpp"
                            "main.cpp"
                            "AppManager.cpp"
                            "LedStripDriver.cpp"
                            "Utilities.cpp"
                            "WifiConnection.cpp"
                            "TelnetServer.cpp"
                            "SystemStatus.cpp"
                        PRIV_REQUIRES
                            nvs_flash
                            spi_flash
                            esp_driver_rmt
                            esp_wifi
                            esp_driver_gpio
                            fatfs
                            sdmmc
                            mbedtls
                            espUtilities
                            yrShell
                       INCLUDE_DIRS ".")
