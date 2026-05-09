/*
 * eval_suite/08_uart_bugs.c
 *
 * Platform: TI CC2652R7, SimpleLink SDK
 */

#include <stdint.h>
#include <stddef.h>
#include <ti/drivers/UART.h>
#include <driverlib/uart.h>
#include <driverlib/sysctl.h>

static UART_Handle g_uartHandle;

void initUART(void)
{
    SysCtrlPeripheralEnable(SYSCTL_PERIPH_UART0);

    UARTConfigSetExpClk(UART0_BASE, SysCtrlClockGet(), 115200,
                        UART_CONFIG_WLEN_8 |
                        UART_CONFIG_STOP_ONE |
                        UART_CONFIG_PAR_NONE);

    UARTEnable(UART0_BASE);
    UARTIntEnable(UART0_BASE, UART_INT_RX | UART_INT_RT);
}

static uint8_t g_statusBuf[32];

/* ClockP SWI callback — runs in SWI context */
void statusReportClkFxn(uintptr_t arg)
{
    size_t len = buildStatusPacket(g_statusBuf, sizeof(g_statusBuf));
    UART_write(g_uartHandle, g_statusBuf, len);
}
