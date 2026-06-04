idf_component_register(SRCS "MultiplexerQ.cpp" 
                            "HardwareSpecific.cpp"
                            "Sliceable.cpp"
                            "HiResTimer.cpp"
                            "IntervalTimer.cpp"
                            "MultiplexerQ.cpp"
                    INCLUDE_DIRS "."
                    PRIV_REQUIRES esp_timer)
