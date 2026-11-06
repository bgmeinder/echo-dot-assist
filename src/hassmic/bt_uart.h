/* A Bluetooth controller on a UART (bt_uart.c): the line, the Broadcom patch, then H4 as from /dev/stpbt */
#ifndef BT_UART_H
#define BT_UART_H
#include <stdint.h>

/* tty: the UART; hcd: the firmware patch (NULL: none); baud: the speed to run at (115200 if it does not hold);
 * addr: the Bluetooth address, most significant byte first, or NULL.  The descriptor, or -1 (logged) */
int bt_uart_open(const char *tty, const char *hcd, int baud, const uint8_t addr[6]);
#endif
