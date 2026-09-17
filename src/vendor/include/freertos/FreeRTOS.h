#ifndef FREERTOS_H
#define FREERTOS_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef uint32_t TickType_t;
typedef uint32_t UBaseType_t;
typedef int32_t BaseType_t;

#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define pdFAIL 0
#define portMAX_DELAY 0xffffffffUL
#define portTICK_PERIOD_MS 1U

#endif /* FREERTOS_H */
