/* i2creg: a register of an I2C device, read or written through /dev/i2c-N behind the kernel's own driver (I2C_SLAVE_FORCE),
 * for looking at a codec's real state where the driver's regmap cache may not match it (rook: the TLV320AIC32x4's
 * headphone driver mute, 2026-11-04).  Not in `make all`.
 *   i2creg BUS ADDR r REG          print the register (one byte each)
 *   i2creg BUS ADDR w REG VALUE    write it
 * Paged codecs: write the page register first (the AIC32x4: register 0 selects the page).  BUS, ADDR, REG and VALUE
 * are hex. */
#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    if (argc < 5) { fprintf(stderr, "usage: i2creg BUS ADDR r REG | w REG VALUE (hex)\n"); return 2; }
    char path[32]; snprintf(path, sizeof path, "/dev/i2c-%ld", strtol(argv[1], NULL, 16));
    int fd = open(path, O_RDWR);
    if (fd < 0) { perror(path); return 1; }
    if (ioctl(fd, I2C_SLAVE_FORCE, strtol(argv[2], NULL, 16)) < 0) { perror("I2C_SLAVE_FORCE"); return 1; }
    unsigned char reg = strtol(argv[4], NULL, 16), buf[2] = { reg, 0 };
    if (argv[3][0] == 'w' && argc > 5) {
        buf[1] = strtol(argv[5], NULL, 16);
        if (write(fd, buf, 2) != 2) { perror("write"); return 1; }
    }
    if (write(fd, buf, 1) != 1 || read(fd, buf + 1, 1) != 1) { perror("read"); return 1; }
    printf("%02x: %02x\n", reg, buf[1]);
    return 0;
}
