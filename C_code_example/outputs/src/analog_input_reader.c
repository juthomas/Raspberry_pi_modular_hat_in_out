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
#include <gpiod.h>
#include <errno.h>

#define CHIP_NAME "gpiochip0"
#define CHIP_PATH "/dev/" CHIP_NAME

#define DAC_1 0x63
#define DAC_2 0x64

#define LDAC1_GPIO 0
#define LDAC2_GPIO 1

#define MCP4728_VREF_VDD 0
#define MCP4728_VREF_INTERNAL 1
#define MCP4728_GAIN_X1 0
#define MCP4728_GAIN_X2 1

#define DEFAULT_POINTS_PER_PERIOD 1000U
#define MIN_POINTS_PER_PERIOD 16U
#define MAX_POINTS_PER_PERIOD 20000U
#define DEFAULT_SAMPLE_DELAY_US 1U
#define MAX_SAMPLE_DELAY_US 1000000U

void delayMicroseconds(unsigned int micros) {
    usleep(micros);
}

void delay(unsigned int millis) {
    usleep(millis * 1000);
}

static unsigned int parse_u32_or_default(const char *raw_value, const char *param_name,
    unsigned int default_value, unsigned int min_value, unsigned int max_value)
{
    char *end = NULL;
    unsigned long parsed;

    if (!raw_value || *raw_value == '\0') {
        printf("Warning: missing value for %s, using default %u\n", param_name, default_value);
        return default_value;
    }
    errno = 0;
    parsed = strtoul(raw_value, &end, 10);
    if (errno != 0 || end == raw_value || *end != '\0' || parsed < min_value || parsed > max_value) {
        printf("Warning: invalid %s='%s' (range %u..%u), using default %u\n",
            param_name, raw_value, min_value, max_value, default_value);
        return default_value;
    }
    return (unsigned int)parsed;
}

static int parse_sine_runtime_options(int argc, char **argv,
    unsigned int *points_per_period, unsigned int *sample_delay_us)
{
    *points_per_period = DEFAULT_POINTS_PER_PERIOD;
    *sample_delay_us = DEFAULT_SAMPLE_DELAY_US;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0) {
            printf("Usage: %s [--resolution=<points>] [--points=<points>] [--delay-us=<microseconds>]\n", argv[0]);
            printf("  --resolution / --points : points per sine period (%u..%u), default: %u\n",
                MIN_POINTS_PER_PERIOD, MAX_POINTS_PER_PERIOD, DEFAULT_POINTS_PER_PERIOD);
            printf("  --delay-us              : delay between samples in microseconds (0..%u), default: %u\n",
                MAX_SAMPLE_DELAY_US, DEFAULT_SAMPLE_DELAY_US);
            return 1;
        } else if (strncmp(argv[i], "--resolution=", 13) == 0) {
            *points_per_period = parse_u32_or_default(argv[i] + 13, "resolution",
                DEFAULT_POINTS_PER_PERIOD, MIN_POINTS_PER_PERIOD, MAX_POINTS_PER_PERIOD);
        } else if (strncmp(argv[i], "--points=", 9) == 0) {
            *points_per_period = parse_u32_or_default(argv[i] + 9, "points",
                DEFAULT_POINTS_PER_PERIOD, MIN_POINTS_PER_PERIOD, MAX_POINTS_PER_PERIOD);
        } else if (strncmp(argv[i], "--delay-us=", 11) == 0) {
            *sample_delay_us = parse_u32_or_default(argv[i] + 11, "delay-us",
                DEFAULT_SAMPLE_DELAY_US, 0U, MAX_SAMPLE_DELAY_US);
        } else {
            printf("Warning: unknown option '%s' (use --help)\n", argv[i]);
        }
    }
    return 0;
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

typedef struct s_ldac_gpio_ctx {
    struct gpiod_chip *chip;
    struct gpiod_line_request *request;
    int ready;
}   t_ldac_gpio_ctx;

static t_ldac_gpio_ctx g_ldac_ctx = {0};

static void cleanup_ldac(void)
{
    if (g_ldac_ctx.request) {
        gpiod_line_request_release(g_ldac_ctx.request);
        g_ldac_ctx.request = NULL;
    }
    if (g_ldac_ctx.chip) {
        gpiod_chip_close(g_ldac_ctx.chip);
        g_ldac_ctx.chip = NULL;
    }
    g_ldac_ctx.ready = 0;
}

static int setup_ldac(void)
{
    struct gpiod_line_settings *line_settings = NULL;
    struct gpiod_line_config *line_config = NULL;
    struct gpiod_request_config *request_config = NULL;
    const unsigned int offsets[2] = {LDAC1_GPIO, LDAC2_GPIO};
    const enum gpiod_line_value init_values[2] = {
        GPIOD_LINE_VALUE_ACTIVE,
        GPIOD_LINE_VALUE_ACTIVE
    };

    g_ldac_ctx.chip = gpiod_chip_open(CHIP_PATH);
    if (!g_ldac_ctx.chip) {
        printf("Error: unable to open %s for LDAC control: %s\n", CHIP_PATH, strerror(errno));
        return -1;
    }

    line_settings = gpiod_line_settings_new();
    line_config = gpiod_line_config_new();
    request_config = gpiod_request_config_new();
    if (!line_settings || !line_config || !request_config) {
        printf("Error: unable to allocate libgpiod config objects\n");
        goto error;
    }

    if (gpiod_line_settings_set_direction(line_settings, GPIOD_LINE_DIRECTION_OUTPUT) < 0) {
        printf("Error: failed to configure LDAC lines as outputs\n");
        goto error;
    }
    if (gpiod_line_settings_set_output_value(line_settings, GPIOD_LINE_VALUE_ACTIVE) < 0) {
        printf("Error: failed to set initial LDAC output value\n");
        goto error;
    }

    if (gpiod_line_config_add_line_settings(line_config, offsets, 2, line_settings) < 0) {
        printf("Error: failed to add LDAC line settings\n");
        goto error;
    }
    if (gpiod_line_config_set_output_values(line_config, init_values, 2) < 0) {
        printf("Error: failed to set LDAC initial levels\n");
        goto error;
    }

    gpiod_request_config_set_consumer(request_config, "mcp4728-ldac");
    g_ldac_ctx.request = gpiod_chip_request_lines(g_ldac_ctx.chip, request_config, line_config);
    if (!g_ldac_ctx.request) {
        printf("Error: unable to request LDAC GPIO lines on %s: %s\n", CHIP_PATH, strerror(errno));
        goto error;
    }

    g_ldac_ctx.ready = 1;
    gpiod_request_config_free(request_config);
    gpiod_line_config_free(line_config);
    gpiod_line_settings_free(line_settings);
    return 0;

error:
    gpiod_request_config_free(request_config);
    gpiod_line_config_free(line_config);
    gpiod_line_settings_free(line_settings);
    cleanup_ldac();
    return -1;
}

static int ldac_pulse_low(unsigned int gpio_offset)
{
    if (!g_ldac_ctx.ready || !g_ldac_ctx.request) {
        return -1;
    }
    if (gpiod_line_request_set_value(g_ldac_ctx.request, gpio_offset, GPIOD_LINE_VALUE_ACTIVE) < 0) {
        return -1;
    }
    usleep(2);
    if (gpiod_line_request_set_value(g_ldac_ctx.request, gpio_offset, GPIOD_LINE_VALUE_INACTIVE) < 0) {
        return -1;
    }
    usleep(2);
    if (gpiod_line_request_set_value(g_ldac_ctx.request, gpio_offset, GPIOD_LINE_VALUE_ACTIVE) < 0) {
        return -1;
    }
    return 0;
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

int main(int argc, char **argv)
{
	const char *i2c_bus = "/dev/i2c-1";
	int i2c_fd = i2c_init(i2c_bus);
    int ldac_ready;
    int ldac_error_reported = 0;
    unsigned int points_per_period;
    unsigned int sample_delay_us;
    int parse_status = parse_sine_runtime_options(argc, argv, &points_per_period, &sample_delay_us);

    if (parse_status > 0) {
        return 0;
    }
    if (parse_status < 0) {
        return 1;
    }

	if (i2c_fd < 0) {
        printf("Error: failed to initialize I2C bus\n");
		return 1;
	}

	scan_i2c_bus(i2c_fd);

printf("\n=== MCP4728 Tests ===\n");
	
printf("\nTest 1: Write to channel A of first MCP4728 (0x63)\n");
    printf("Valeur: 2048\n");
    mcp4728_set_output(i2c_fd, DAC_1, 0, 2048);
//    usleep(100000);
	delay(500);
	
printf("\nTest 2: Write to channel B of second MCP4728 (0x64)\n");
    printf("Valeur: 4095\n");
    mcp4728_set_output(i2c_fd, DAC_2, 1, 4095);
//	usleep(100000);
	delay(500);
	
printf("\nTest 3: Write all channels of first MCP4728\n");
    uint16_t values[4] = {1024, 2048, 3072, 4095};
	mcp4728_write_multiple_channels(i2c_fd, DAC_1, values);
//	usleep(100000);
	delay(500);	
printf("\nTest 4: Phased sine sweep on all 8 outputs\n");
	ldac_ready = (setup_ldac() == 0);
    if (!ldac_ready) {
        // Keep outputs moving even if LDAC cannot be driven (kernel GPIO mapping changed, permissions, etc.).
        printf("Warning: LDAC init failed, falling back to immediate DAC update mode (UDAC=0).\n");
    }
    printf("Sine config: resolution=%u points/period, delay=%u us\n", points_per_period, sample_delay_us);
    const double two_pi = 2.0 * 3.14159265358979323846;
    const double phase_step = two_pi / 8.0;
	while (1) {
		for (unsigned int i = 0; i < points_per_period; i++) {
            uint16_t phased_values[8];
            double angle = two_pi * (double)i / (double)points_per_period;

            for (int output = 0; output < 8; output++) {
                double shifted_angle = angle + ((double)output * phase_step);
                phased_values[output] = (uint16_t)(2048.0 + 2047.0 * sin(shifted_angle));
            }
            uint8_t udac = ldac_ready ? 1 : 0;
			mcp4728_write_channel_with_udac(i2c_fd, DAC_1, 0, phased_values[0], MCP4728_VREF_INTERNAL, MCP4728_GAIN_X1, 0, udac);
			mcp4728_write_channel_with_udac(i2c_fd, DAC_1, 1, phased_values[1], MCP4728_VREF_INTERNAL, MCP4728_GAIN_X1, 0, udac);
			mcp4728_write_channel_with_udac(i2c_fd, DAC_1, 2, phased_values[2], MCP4728_VREF_INTERNAL, MCP4728_GAIN_X1, 0, udac);
			mcp4728_write_channel_with_udac(i2c_fd, DAC_1, 3, phased_values[3], MCP4728_VREF_INTERNAL, MCP4728_GAIN_X1, 0, udac);

			mcp4728_write_channel_with_udac(i2c_fd, DAC_2, 0, phased_values[4], MCP4728_VREF_INTERNAL, MCP4728_GAIN_X1, 0, udac);
			mcp4728_write_channel_with_udac(i2c_fd, DAC_2, 1, phased_values[5], MCP4728_VREF_INTERNAL, MCP4728_GAIN_X1, 0, udac);
			mcp4728_write_channel_with_udac(i2c_fd, DAC_2, 2, phased_values[6], MCP4728_VREF_INTERNAL, MCP4728_GAIN_X1, 0, udac);
			mcp4728_write_channel_with_udac(i2c_fd, DAC_2, 3, phased_values[7], MCP4728_VREF_INTERNAL, MCP4728_GAIN_X1, 0, udac);

            if (ldac_ready) {
			    if (ldac_pulse_low(LDAC1_GPIO) < 0 || ldac_pulse_low(LDAC2_GPIO) < 0) {
                    ldac_ready = 0;
                    if (!ldac_error_reported) {
                        printf("Warning: LDAC pulse failed, switching to immediate updates (UDAC=0).\n");
                        ldac_error_reported = 1;
                    }
                }
            }
            // Debug prints in this loop significantly reduce update rate and make the waveform look stepped.
            // if ((i % 100) == 0) {
            //     printf("Values: %u %u %u %u | %u %u %u %u\n",
            //         (unsigned int)phased_values[0], (unsigned int)phased_values[1],
            //         (unsigned int)phased_values[2], (unsigned int)phased_values[3],
            //         (unsigned int)phased_values[4], (unsigned int)phased_values[5],
            //         (unsigned int)phased_values[6], (unsigned int)phased_values[7]);
            // }
            delayMicroseconds(sample_delay_us);
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
    cleanup_ldac();
    return 0;
}
