idf_component_register(SRCS "BleConnection.cpp" "YRShellEsp32.cpp" "main.cpp" "AppManager.cpp" "LedStripDriver.cpp" "Utilities.cpp" "WifiConnection.cpp" "TelnetServer.cpp" "YRShellEsp32.cpp" "BleConnection.cpp"
                       PRIV_REQUIRES nvs_flash spi_flash esp_driver_rmt esp_wifi esp_driver_gpio espUtilities yrShell
                       INCLUDE_DIRS ".")
