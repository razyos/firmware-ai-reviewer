/*
 * eval_suite/08_uart_bugs.c
 *
 * Planted bugs:
 *   UART-001 (line 23): UARTFIFOEnable() not called — interrupt-driven RX at 115200 baud
 *                        fires one interrupt per byte (interrupt storm)
 *   UART-004 (line 47): UART_write (blocking) called from a ClockP SWI callback context
 *
 * Platform: TI CC2652R7, SimpleLink SDK
 */

#include <stdint.h>
#include <stddef.h>
#include <ti/drivers/UART.h>
#include <ti/drivers/utils/RingBuf.h>
#include <ti/sysbios/knl/Clock.h>
#include <driverlib/uart.h>
#include <driverlib/sysctl.h>

static UART_Handle g_uartHandle;

/* --- UART-001: FIFO never enabled ---
 * UARTFIFOEnable() is missing. With interrupt-driven RX and no FIFO,
 * each received byte at 115200 baud fires a separate CPU interrupt.
 * At high throughput this saturates the interrupt controller.
 */
void initUART(void)
{
    SysCtrlPeripheralEnable(SYSCTL_PERIPH_UART0);

    UARTConfigSetExpClk(UART0_BASE, SysCtrlClockGet(), 115200,
                        UART_CONFIG_WLEN_8 |
                        UART_CONFIG_STOP_ONE |
                        UART_CONFIG_PAR_NONE);

    /* BUG UART-001: UARTFIFOEnable(UART0_BASE) is missing here */  /* line 23 */

    UARTEnable(UART0_BASE);
    UARTIntEnable(UART0_BASE, UART_INT_RX | UART_INT_RT);
}

static uint8_t g_statusBuf[32];

/* --- UART-004: blocking UART_write called from SWI/ClockP callback context ---
 * statusReportClk is a ClockP SWI callback — it runs in SWI context, not task context.
 * UART_write acquires an internal semaphore; calling it from SWI deadlocks the system
 * because semaphores cannot block in SWI context.
 */
void statusReportClkFxn(uintptr_t arg)   /* ClockP SWI callback */
{
    size_t len = buildStatusPacket(g_statusBuf, sizeof(g_statusBuf));

    /* BUG UART-004: UART_write is blocking; must not be called from SWI/ClockP context */
    UART_write(g_uartHandle, g_statusBuf, len);   /* line 47 */
}
