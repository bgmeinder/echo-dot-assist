/*
 * A Bluetooth controller on a UART (the Echo Spot's Broadcom BCM43569 on /dev/ttyMT1): the line set up and the chip's
 * firmware patch downloaded, then the same H4 stream ble.c reads from MediaTek's /dev/stpbt on the other Echos.  The
 * kernel has no Bluetooth stack here (no AF_BLUETOOTH, no hci_uart); Android's own stack did this from user space with
 * Broadcom's vendor library (libbt-vendor.so, hardware/broadcom/libbt), whose sequence this follows:
 *
 *   115200 8N1, RTS/CTS        HCI_Reset
 *                              Update_Baudrate (vendor 0xFC18) to the target, the line switched after its answer
 *                              Download_Minidriver (vendor 0xFC2E), 50 ms
 *                              the .hcd's records, each an HCI command (opcode, length, parameters) sent as it is
 *                              the patch's settlement, 200 ms: the chip restarts at 115200 on its own
 *   115200                     HCI_Reset
 *                              Update_Baudrate to the target again, the line switched
 *                              Write_BD_ADDR (vendor 0xFC01): the Echo's address (idme bt_mac_addr, as Android's
 *                              persist.service.bdroid.bdaddr has it), little-endian on the wire
 *
 * A target speed the line cannot hold (a command that goes unanswered after the switch) falls back to 115200 for the
 * rest: slow, enough for LE scanning, not for A2DP.  Nothing here runs after setup: ble.c owns the descriptor.
 */
#include "bt_uart.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

static long long ms_now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1000LL + t.tv_nsec / 1000000; }
static void ms_sleep(int ms) { struct timespec t = { ms / 1000, (ms % 1000) * 1000000L }; while (nanosleep(&t, &t) && errno == EINTR) ; }

static speed_t speed_of(int baud)
{
    switch (baud) {
    case 115200: return B115200;
    case 230400: return B230400;
    case 460800: return B460800;
    case 921600: return B921600;
#ifdef B1000000
    case 1000000: return B1000000;
#endif
#ifdef B1500000
    case 1500000: return B1500000;
#endif
#ifdef B2000000
    case 2000000: return B2000000;
#endif
#ifdef B3000000
    case 3000000: return B3000000;
#endif
#ifdef B4000000
    case 4000000: return B4000000;
#endif
    default: return 0;
    }
}

static int line_speed(int fd, int baud)
{
    struct termios t;
    speed_t s = speed_of(baud);
    if (!s || tcgetattr(fd, &t)) return -1;
    cfsetispeed(&t, s); cfsetospeed(&t, s);
    if (tcsetattr(fd, TCSADRAIN, &t)) return -1;
    tcflush(fd, TCIOFLUSH);
    return 0;
}

static int line_open(const char *tty)
{
    int fd = open(tty, O_RDWR | O_NOCTTY | O_CLOEXEC);
    if (fd < 0) return -1;
    struct termios t;
    if (tcgetattr(fd, &t)) { close(fd); return -1; }
    cfmakeraw(&t);
    t.c_cflag |= CRTSCTS | CLOCAL | CREAD;          /* the chip and the SoC do hardware flow control, as libbt-vendor */
    t.c_cflag &= ~(CSTOPB | PARENB);
    t.c_cc[VMIN] = 0; t.c_cc[VTIME] = 0;
    cfsetispeed(&t, B115200); cfsetospeed(&t, B115200);
    if (tcsetattr(fd, TCSANOW, &t)) { close(fd); return -1; }
    tcflush(fd, TCIOFLUSH);
    return fd;
}

static int read_n(int fd, uint8_t *b, size_t n, long long until)
{
    size_t got = 0;
    while (got < n) {
        long long left = until - ms_now();
        if (left <= 0) return -1;
        struct pollfd p = { fd, POLLIN, 0 };
        int r = poll(&p, 1, (int)left);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) return -1;
        ssize_t k = read(fd, b + got, n - got);
        if (k < 0 && errno == EINTR) continue;
        if (k <= 0) return -1;
        got += (size_t)k;
    }
    return 0;
}

/* One HCI command and its Command Complete (or Command Status): its status byte, -1 when none came within ms.  Events
 * of other kinds and other opcodes are passed over; what is not an event (no H4 type 4) means the line is out of step */
static int command(int fd, unsigned op, const uint8_t *par, unsigned n, int ms)
{
    uint8_t b[260];
    b[0] = 0x01; b[1] = op & 255; b[2] = op >> 8; b[3] = (uint8_t)n;
    if (n) memcpy(b + 4, par, n);
    if (write(fd, b, 4 + n) != (ssize_t)(4 + n)) return -1;
    long long until = ms_now() + ms;
    for (;;) {
        uint8_t h[3], e[256];
        if (read_n(fd, h, 1, until)) return -1;
        if (h[0] != 0x04) continue;                 /* not an event: skipped byte by byte until one starts */
        if (read_n(fd, h + 1, 2, until) || read_n(fd, e, h[2], until)) return -1;
        if (h[1] == 0x0e && h[2] >= 4 && (unsigned)(e[1] | e[2] << 8) == op) return e[3];   /* Command Complete */
        if (h[1] == 0x0f && h[2] >= 4 && (unsigned)(e[2] | e[3] << 8) == op) return e[0];   /* Command Status */
    }
}

static int set_baud(int fd, int baud)
{
    uint8_t p[6] = { 0, 0, baud & 255, baud >> 8 & 255, baud >> 16 & 255, baud >> 24 & 255 };
    if (command(fd, 0xfc18, p, 6, 1000) != 0) return -1;
    ms_sleep(10);
    if (line_speed(fd, baud)) return -1;
    return command(fd, 0x0c03, NULL, 0, 1000) == 0 ? 0 : -1;    /* a reset at the new speed proves it holds */
}

static int patch(int fd, const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "bluetooth: %s: %s\n", path, strerror(errno)); return -1; }
    int records = 0, r = 0;
    if (command(fd, 0xfc2e, NULL, 0, 2000) != 0) { fclose(f); return -1; }     /* Download_Minidriver */
    ms_sleep(50);
    for (uint8_t h[3], p[256]; fread(h, 1, 3, f) == 3; records++) {
        unsigned op = h[0] | h[1] << 8, n = h[2];
        if (fread(p, 1, n, f) != n) { r = -1; break; }
        int st = command(fd, op, p, n, 2000);
        if (st < 0 && op != 0xfc4e) { r = -1; break; }          /* Launch_RAM may not answer: the chip restarts */
    }
    fclose(f);
    if (r) { fprintf(stderr, "bluetooth: the patch stopped at record %d\n", records); return -1; }
    fprintf(stderr, "bluetooth: %d patch records sent\n", records);
    return 0;
}

int bt_uart_open(const char *tty, const char *hcd, int baud, const uint8_t addr[6])
{
    int fd = line_open(tty);
    if (fd < 0) return -1;                          /* errno says why; ble.c logs it once, not at every retry */
    if (command(fd, 0x0c03, NULL, 0, 2000) != 0) {
        /* A chip patched and switched up by an earlier run (hassmic restarted; nothing powers it down in between, root
         * alone could through rfkill) answers at the speed it was left at, with the patch still in: then only the
         * reset there and the address (2026-11-05, the Spot's second start) */
        if (baud > 115200 && !line_speed(fd, baud) && command(fd, 0x0c03, NULL, 0, 1000) == 0) {
            if (addr) {
                uint8_t le[6];
                for (int i = 0; i < 6; i++) le[i] = addr[5 - i];
                if (command(fd, 0xfc01, le, 6, 1000) != 0) fprintf(stderr, "bluetooth: the address was not taken\n");
            }
            fprintf(stderr, "bluetooth: %s up at %d baud, patched before\n", tty, baud);
            return fd;
        }
        fprintf(stderr, "bluetooth: no answer to a reset on %s\n", tty); close(fd); return -1;
    }
    int fast = baud > 115200 && !set_baud(fd, baud);
    if (baud > 115200 && !fast) { line_speed(fd, 115200); fprintf(stderr, "bluetooth: %d baud does not hold, 115200 for the patch\n", baud); }
    if (hcd && *hcd) {
        if (patch(fd, hcd)) { close(fd); return -1; }
        ms_sleep(200);                              /* the settlement: the patched firmware starts, at 115200 */
        line_speed(fd, 115200);
        if (command(fd, 0x0c03, NULL, 0, 2000) != 0) { fprintf(stderr, "bluetooth: no answer after the patch\n"); close(fd); return -1; }
    }
    fast = baud > 115200 && !set_baud(fd, baud);
    if (baud > 115200 && !fast) { line_speed(fd, 115200); command(fd, 0x0c03, NULL, 0, 2000); }
    if (addr) {
        uint8_t le[6];
        for (int i = 0; i < 6; i++) le[i] = addr[5 - i];
        if (command(fd, 0xfc01, le, 6, 1000) != 0) fprintf(stderr, "bluetooth: the address was not taken\n");
    }
    fprintf(stderr, "bluetooth: %s up at %d baud\n", tty, fast ? baud : 115200);
    return fd;
}
