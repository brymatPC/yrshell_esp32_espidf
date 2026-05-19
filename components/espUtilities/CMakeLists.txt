idf_component_register(SRCS "HardwareSpecific.cpp" "Sliceable.cpp" "HiResTimer.cpp" "IntervalTimer.cpp"
                    INCLUDE_DIRS "."
                    PRIV_REQUIRES esp_timer)
