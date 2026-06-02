idf_component_register(SRCS "sen66_i2c.c" "sensirion_common.c" "sensirion_i2c_hal.c" "sensirion_i2c.c"
                    INCLUDE_DIRS "."
                    PRIV_REQUIRES esp_driver_i2c)
