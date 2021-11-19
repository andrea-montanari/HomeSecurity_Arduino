#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#define configUSE_PREEMPTION                    1
#define configUSE_TIME_SLICING                  1
#define configIDLE_SHOULD_YIELD                 1
#define configMINIMAL_STACK_SIZE                128
#define configCPU_CLOCK_HZ                      16000000
#define configTICK_RATE_HZ                      100
#define configTOTAL_HEAP_SIZE                   10240
#define configMAX_PRIORITIES                    3

#define configUSE_IDLE_HOOK                     0
#define configUSE_TICK_HOOK                     0

#define configUSE_16_BIT_TICKS                  1


#endif /* FREERTOS_CONFIG_H */
