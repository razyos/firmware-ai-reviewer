/*
 * eval_suite/06_packed_struct_dma.c
 *
 * Platform: TI CC2652R7
 */

#include <stdint.h>
#include "sensor_types.h"

#include <ti/drivers/dma/UDMACC26XX.h>

#define SENSOR_DMA_CH   4
#define SENSOR_BASE     0x40090000UL

static SensorFrame_t dmaRxBuf;

void Sensor_StartDmaRead(void)
{
    uDMAChannelTransferSet(
        SENSOR_DMA_CH,
        UDMA_MODE_BASIC,
        (void *)SENSOR_BASE,
        &dmaRxBuf,
        sizeof(SensorFrame_t)
    );
    uDMAChannelEnable(SENSOR_DMA_CH);
}
