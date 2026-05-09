/*
 * eval_suite/06_packed_struct_dma.c
 *
 * Planted bugs:
 *   MEM-005 — packed struct passed directly to DMA transfer.
 *             SensorFrame_t is defined with __attribute__((packed)) in
 *             sensor_types.h — the bug is only visible with the header.
 *
 * Platform: TI CC2652R7
 */

#include <stdint.h>
#include "sensor_types.h"   /* defines SensorFrame_t — packed struct */

#include <ti/drivers/dma/UDMACC26XX.h>

#define SENSOR_DMA_CH   4
#define SENSOR_BASE     0x40090000UL

static SensorFrame_t dmaRxBuf;   /* static — HW-001 is clean */

/*
 * BUG [MEM-005]: dmaRxBuf is a SensorFrame_t, which is declared with
 * __attribute__((packed)) in sensor_types.h. Packed structs have no
 * alignment guarantee. Passing &dmaRxBuf as the DMA destination for a
 * 32-bit transfer will cause a BusFault if timestamp_ms (uint32_t at
 * offset 1) is the transfer target — Cortex-M4 requires 4-byte alignment
 * for 32-bit DMA.
 *
 * Fix: remove __attribute__((packed)) from SensorFrame_t and add explicit
 * padding, or use a plain uint8_t[8] DMA buffer and deserialise manually.
 */
void Sensor_StartDmaRead(void)
{
    uDMAChannelTransferSet(
        SENSOR_DMA_CH,
        UDMA_MODE_BASIC,
        (void *)SENSOR_BASE,
        &dmaRxBuf,          /* line 36 — packed struct as DMA destination */
        sizeof(SensorFrame_t)
    );
    uDMAChannelEnable(SENSOR_DMA_CH);
}
