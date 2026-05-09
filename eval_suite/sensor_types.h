/*
 * sensor_types.h — shared type definitions for sensor driver
 */
#ifndef SENSOR_TYPES_H
#define SENSOR_TYPES_H

#include <stdint.h>

/* BUG [MEM-005]: packed struct will be passed to DMA.
 * __attribute__((packed)) removes alignment padding — fields may sit at
 * unaligned addresses. A 32-bit DMA transfer to an unaligned address causes
 * BusFault on Cortex-M4.
 */
typedef struct __attribute__((packed)) {
    uint8_t  sensor_id;     /* offset 0 */
    uint32_t timestamp_ms;  /* offset 1 — misaligned! */
    uint16_t raw_adc;       /* offset 5 — misaligned! */
    uint8_t  status;        /* offset 7 */
} SensorFrame_t;            /* sizeof = 8, but fields are misaligned */

#endif /* SENSOR_TYPES_H */
