#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <spidev_lib.h>
#include <unistd.h>
#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <linux/i2c.h>
#include <math.h>
#include <stdint.h>

#define CHIP_NAME "gpiochip0"

#define DAC_1 0x63
#define DAC_2 0x64

#define LDAC1_GPIO 0
#define LDAC2_GPIO 1

#define MCP4728_VREF_VDD 0
#define MCP4728_VREF_INTERNAL 1
#define MCP4728_GAIN_X1 0
#define MCP4728_GAIN_X2 1

void delayMicroseconds(unsigned int micros) {
    usleep(micros);
}

void delay(unsigned int millis) {
    usleep(millis * 1000);
}



int i2c_init(const char *i2c_bus)
{
	int fd;
	if ((fd = open(i2c_bus, O_RDWR)) < 0)
	{
		perror("Error opening I2C bus");
		return -1;
	}
	return fd;
}

int check_device_at_address(int fd, uint8_t address)
{
    if (ioctl(fd, I2C_SLAVE, address) < 0)
	{
        return 0;
	}
	uint8_t buffer;
    if (read(fd, &buffer, 1) != 1)
	{
        return 0;
	}
    return 1;
}



void scan_i2c_bus(int file) {
    printf("\nScanning I2C bus...\n");
    printf("     0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F\n");

    for (int i = 0; i < 128; i += 16) {
        printf("%02x: ", i);
        
        for (int j = 0; j < 16; j++) {
            int addr = i + j;
            if (addr < 0x03 || addr > 0x77) {
                printf("   ");
                continue;
            }
            if (ioctl(file, I2C_SLAVE, addr) < 0) {
                printf("-- ");
                continue;
            }
            char buf;
            if (read(file, &buf, 1) < 0) {
                printf("-- ");
            } else {
                printf("%02x ", addr);
            }
        }
        printf("\n");
    }
    printf("\nScan completed.\n");
}

void i2c_write(int fd, uint8_t address, uint8_t* data, int length) {
    if (ioctl(fd, I2C_SLAVE, address) < 0) {
        printf("Error opening I2C bus\n");
        return;
    }

    if (write(fd, data, length) != length) {
        printf("Error writing to device\n");
        return;
    }
}

static int sysfs_write(const char *path, const char *value)
{
    int fd = open(path, O_WRONLY);
    if (fd < 0) {
        return -1;
    }
    ssize_t w = write(fd, value, strlen(value));
    close(fd);
    return (w == (ssize_t)strlen(value)) ? 0 : -1;
}

static int gpio_export(int gpio)
{
    char buf[64];
    snprintf(buf, sizeof(buf), "/sys/class/gpio/export");
    char val[16];
    snprintf(val, sizeof(val), "%d", gpio);
    // Ignorer l'erreur si déjà exporté
    (void)sysfs_write(buf, val);
    return 0;
}

static int gpio_direction_out(int gpio)
{
    char path[64];
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/direction", gpio);
    return sysfs_write(path, "out");
}

static int gpio_write_value(int gpio, int value)
{
    char path[64];
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/value", gpio);
    return sysfs_write(path, value ? "1" : "0");
}

static void ldac_pulse_low(int gpio)
{
    gpio_write_value(gpio, 1);
    usleep(2);
    gpio_write_value(gpio, 0);
    usleep(2);
    gpio_write_value(gpio, 1);
}

static void setup_ldac(void)
{
    gpio_export(LDAC1_GPIO);
    gpio_export(LDAC2_GPIO);
    gpio_direction_out(LDAC1_GPIO);
    gpio_direction_out(LDAC2_GPIO);
    gpio_write_value(LDAC1_GPIO, 1);
    gpio_write_value(LDAC2_GPIO, 1);
}

static int mcp4728_write_channel_with_udac(int fd, uint8_t address, uint8_t channel, uint16_t value, uint8_t vref, uint8_t gain, uint8_t power_down, uint8_t udac)
{
    if (channel > 3) {
        printf("Error: invalid channel (0-3)\n");
        return -1;
    }

    if (value > 4095) {
        printf("Error: invalid value (0-4095)\n");
        return -1;
    }

    if (vref > 1 || gain > 1 || power_down > 3) {
        printf("Error: invalid vref/gain/power_down parameters\n");
        return -1;
    }

    uint8_t buf[3];

    buf[0] = (0x08 << 3) | ((channel & 0x03) << 1) | (udac & 0x01);
    buf[1] = ((vref & 0x01) << 7) | ((power_down & 0x03) << 5) | ((gain & 0x01) << 4) | ((value >> 8) & 0x0F);
    buf[2] = value & 0xFF;
    // buf[2] = 0x00;

    if (ioctl(fd, I2C_SLAVE, address) < 0) {
        printf("Error opening MCP4728 at address 0x%02X\n", address);
        return -1;
    }

    if (write(fd, buf, 3) != 3) {
        printf("Error writing to MCP4728\n");
        return -1;
    }

    return 0;
}

int mcp4728_write_channel(int fd, uint8_t address, uint8_t channel, uint16_t value, uint8_t vref, uint8_t gain, uint8_t power_down)
{
    // UDAC=0 pour mise à jour immédiate (comportement par défaut)
    return mcp4728_write_channel_with_udac(fd, address, channel, value, vref, gain, power_down, 0);
}

int mcp4728_set_output(int fd, uint8_t address, uint8_t channel, uint16_t value) {
    return mcp4728_write_channel(fd, address, channel, value, MCP4728_VREF_INTERNAL, MCP4728_GAIN_X1, 0);
}

int mcp4728_write_multiple_channels(int fd, uint8_t address, uint16_t values[4]) {
    for (int i = 0; i < 4; i++) {
        if (mcp4728_set_output(fd, address, i, values[i]) != 0) {
            printf("Error writing channel %d\n", i);
            return -1;
        }
    }
    return 0;
}

void main(void)
{
	const char *i2c_bus = "/dev/i2c-1";
	int i2c_fd = i2c_init(i2c_bus);

	if (i2c_fd < 0) {
        printf("Error: failed to initialize I2C bus\n");
		return;
	}

	scan_i2c_bus(i2c_fd);

printf("\n=== MCP4728 Tests ===\n");
	
printf("\nTest 1: Write to channel A of first MCP4728 (0x63)\n");
    printf("Valeur: 2048\n");
    mcp4728_set_output(i2c_fd, DAC_1, 0, 2048);
    usleep(100000);
	
printf("\nTest 2: Write to channel B of second MCP4728 (0x64)\n");
    printf("Valeur: 4095\n");
    mcp4728_set_output(i2c_fd, DAC_2, 1, 4095);
	usleep(100000);
	
printf("\nTest 3: Write all channels of first MCP4728\n");
    uint16_t values[4] = {1024, 2048, 3072, 4095};
	mcp4728_write_multiple_channels(i2c_fd, DAC_1, values);
	usleep(100000);
	
printf("\nTest 4: Sweep on channel C of second MCP4728\n");
	setup_ldac();
	while (1) {
		for (int i = 0; i < 1000; i++) {
		double angle = 2.0 * 3.14159 * i / 1000.0;
		uint16_t value = (uint16_t)(2048 + 2047 * sin(angle));
			mcp4728_write_channel_with_udac(i2c_fd, DAC_1, 0, value, MCP4728_VREF_INTERNAL, MCP4728_GAIN_X1, 0, 1);
			mcp4728_write_channel_with_udac(i2c_fd, DAC_1, 1, value, MCP4728_VREF_INTERNAL, MCP4728_GAIN_X1, 0, 1);
			mcp4728_write_channel_with_udac(i2c_fd, DAC_1, 2, value, MCP4728_VREF_INTERNAL, MCP4728_GAIN_X1, 0, 1);
			mcp4728_write_channel_with_udac(i2c_fd, DAC_1, 3, value, MCP4728_VREF_INTERNAL, MCP4728_GAIN_X1, 0, 1);

			mcp4728_write_channel_with_udac(i2c_fd, DAC_2, 0, value, MCP4728_VREF_INTERNAL, MCP4728_GAIN_X1, 0, 1);
			mcp4728_write_channel_with_udac(i2c_fd, DAC_2, 1, value, MCP4728_VREF_INTERNAL, MCP4728_GAIN_X1, 0, 1);
			mcp4728_write_channel_with_udac(i2c_fd, DAC_2, 2, value, MCP4728_VREF_INTERNAL, MCP4728_GAIN_X1, 0, 1);
			mcp4728_write_channel_with_udac(i2c_fd, DAC_2, 3, value, MCP4728_VREF_INTERNAL, MCP4728_GAIN_X1, 0, 1);

			ldac_pulse_low(LDAC1_GPIO);
			ldac_pulse_low(LDAC2_GPIO);
            usleep(5000);
		}
	}
    // while (1) {
	// 	for (int value = 0; value < 4096; value += 0xF) {

	// 		mcp4728_write_channel_with_udac(i2c_fd, DAC_1, 0, value, MCP4728_VREF_INTERNAL, MCP4728_GAIN_X1, 0, 1);
	// 		mcp4728_write_channel_with_udac(i2c_fd, DAC_1, 1, value, MCP4728_VREF_INTERNAL, MCP4728_GAIN_X1, 0, 1);
	// 		mcp4728_write_channel_with_udac(i2c_fd, DAC_1, 2, value, MCP4728_VREF_INTERNAL, MCP4728_GAIN_X1, 0, 1);
	// 		mcp4728_write_channel_with_udac(i2c_fd, DAC_1, 3, value, MCP4728_VREF_INTERNAL, MCP4728_GAIN_X1, 0, 1);

	// 		mcp4728_write_channel_with_udac(i2c_fd, DAC_2, 0, value, MCP4728_VREF_INTERNAL, MCP4728_GAIN_X1, 0, 1);
	// 		mcp4728_write_channel_with_udac(i2c_fd, DAC_2, 1, value, MCP4728_VREF_INTERNAL, MCP4728_GAIN_X1, 0, 1);
	// 		mcp4728_write_channel_with_udac(i2c_fd, DAC_2, 2, value, MCP4728_VREF_INTERNAL, MCP4728_GAIN_X1, 0, 1);
	// 		mcp4728_write_channel_with_udac(i2c_fd, DAC_2, 3, value, MCP4728_VREF_INTERNAL, MCP4728_GAIN_X1, 0, 1);

	// 		ldac_pulse_low(LDAC1_GPIO);
	// 		ldac_pulse_low(LDAC2_GPIO);
	// 		usleep(20000);
    //         printf("value: %d\n", value);
	// 	}
	// }



printf("\nTests finished!\n");
	
	close(i2c_fd);
}