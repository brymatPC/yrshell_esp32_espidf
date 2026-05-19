#ifndef HardwareSpecific_h
#define HardwareSpecific_h

#include <stdint.h>

uint32_t HW_getSysticks( void);
uint32_t HW_getMicros( void);
uint32_t HW_getMillis( void);

uint32_t HW_getSysTicksPerSecond( void);

#endif
