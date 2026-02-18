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
#include <signal.h>

#define CHIP_NAME "gpiochip0"
#define CHIP_PATH "/dev/" CHIP_NAME

#define DAC_1 0x63
#define DAC_2 0x64

#define LDAC1_GPIO 0
#define LDAC2_GPIO 1

#define MCP4728_VREF_INTERNAL 1
#define MCP4728_GAIN_X1 0

#define DEFAULT_POINTS_PER_PERIOD 4096U
#define MIN_POINTS_PER_PERIOD 16U
#define MAX_POINTS_PER_PERIOD 20000U

#define DEFAULT_SAMPLE_DELAY_US 10U
#define MAX_SAMPLE_DELAY_US 1000000U

#define EQUALIZER_EVERY 20U
#define HISTORY_EVERY 100U

#define ADS_CHANNEL_COUNT 16
#define ADS_HISTORY_LINES 14
#define ADS_HISTORY_LINE_LEN 256

#define EQ_ROWS 5
#define EQ_STEPS_PER_ROW 8
#define EQ_BAR_WIDTH 4

typedef struct s_ldac_gpio_ctx {
    struct gpiod_chip *chip;
    struct gpiod_line_request *request;
    int ready;
}   t_ldac_gpio_ctx;

typedef struct s_ads_spi_ctx {
    int spi0_fd;
    int spi1_fd;
    int ready;
}   t_ads_spi_ctx;

static t_ldac_gpio_ctx g_ldac_ctx = {0};
static volatile sig_atomic_t g_keep_running = 1;

static void signal_handler(int signo)
{
    (void)signo;
    g_keep_running = 0;
}

static void delay_microseconds(unsigned int micros)
{
    usleep(micros);
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

static int parse_runtime_options(int argc, char **argv,
    unsigned int *points_per_period, unsigned int *sample_delay_us)
{
    *points_per_period = DEFAULT_POINTS_PER_PERIOD;
    *sample_delay_us = DEFAULT_SAMPLE_DELAY_US;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0) {
            printf("Usage: %s [--resolution=<points>] [--delay-us=<microseconds>]\n", argv[0]);
            printf("  --resolution / --points : points per sine period (%u..%u), default: %u\n",
                MIN_POINTS_PER_PERIOD, MAX_POINTS_PER_PERIOD, DEFAULT_POINTS_PER_PERIOD);
            printf("  --delay-us              : delay between updates in microseconds (0..%u), default: %u\n",
                MAX_SAMPLE_DELAY_US, DEFAULT_SAMPLE_DELAY_US);
            printf("Display cadence is controlled by EQUALIZER_EVERY and HISTORY_EVERY defines in source.\n");
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

static int i2c_init(const char *i2c_bus)
{
    int fd = open(i2c_bus, O_RDWR);
    if (fd < 0) {
        perror("Error opening I2C bus");
        return -1;
    }
    return fd;
}

static void scan_i2c_bus(int file)
{
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

static int mcp4728_write_channel_with_udac(int fd, uint8_t address, uint8_t channel,
    uint16_t value, uint8_t vref, uint8_t gain, uint8_t power_down, uint8_t udac)
{
    uint8_t buf[3];

    if (channel > 3 || value > 4095 || vref > 1 || gain > 1 || power_down > 3) {
        return -1;
    }

    buf[0] = (0x08 << 3) | ((channel & 0x03) << 1) | (udac & 0x01);
    buf[1] = ((vref & 0x01) << 7) | ((power_down & 0x03) << 5) | ((gain & 0x01) << 4) | ((value >> 8) & 0x0F);
    buf[2] = value & 0xFF;

    if (ioctl(fd, I2C_SLAVE, address) < 0) {
        return -1;
    }
    if (write(fd, buf, 3) != 3) {
        return -1;
    }
    return 0;
}

static int write_all_mcp_outputs(int i2c_fd, uint8_t udac, const uint16_t values[8])
{
    static const uint8_t addresses[2] = {DAC_1, DAC_2};

    for (int dac = 0; dac < 2; dac++) {
        for (int channel = 0; channel < 4; channel++) {
            int value_index = (dac * 4) + channel;
            if (mcp4728_write_channel_with_udac(i2c_fd, addresses[dac], (uint8_t)channel, values[value_index],
                    MCP4728_VREF_INTERNAL, MCP4728_GAIN_X1, 0, udac) != 0) {
                return -1;
            }
        }
    }
    return 0;
}

static int ads_spi_init(t_ads_spi_ctx *ctx)
{
    spi_config_t spi_config;

    memset(ctx, 0, sizeof(*ctx));
    spi_config.mode = 0;
    spi_config.speed = 1000000;
    spi_config.delay = 0;
    spi_config.bits_per_word = 8;

    ctx->spi0_fd = spi_open("/dev/spidev0.0", spi_config);
    if (ctx->spi0_fd < 0) {
        printf("Error: /dev/spidev0.0 unavailable\n");
        return -1;
    }
    ctx->spi1_fd = spi_open("/dev/spidev0.1", spi_config);
    if (ctx->spi1_fd < 0) {
        printf("Error: /dev/spidev0.1 unavailable\n");
        spi_close(ctx->spi0_fd);
        ctx->spi0_fd = -1;
        return -1;
    }
    ctx->ready = 1;
    return 0;
}

static void ads_spi_cleanup(t_ads_spi_ctx *ctx)
{
    if (ctx->spi0_fd >= 0) {
        spi_close(ctx->spi0_fd);
        ctx->spi0_fd = -1;
    }
    if (ctx->spi1_fd >= 0) {
        spi_close(ctx->spi1_fd);
        ctx->spi1_fd = -1;
    }
    ctx->ready = 0;
}

static int ads_read_voltage(t_ads_spi_ctx *ctx, uint8_t channel, float *voltage_value)
{
    uint8_t tx_buffer[3] = {0};
    uint8_t rx_buffer[3] = {0};
    uint8_t local_channel;
    int spifd;

    if (!ctx->ready || channel > 15 || !voltage_value) {
        return -1;
    }

    spifd = (channel < 8) ? ctx->spi0_fd : ctx->spi1_fd;
    local_channel = (channel < 8) ? channel : (uint8_t)(channel - 8);

    tx_buffer[0] = 1;
    tx_buffer[1] = (uint8_t)((8 + local_channel) << 4);
    tx_buffer[2] = 0;

    if (spi_xfer(spifd, tx_buffer, 3, rx_buffer, 3) < 0) {
        return -1;
    }

    *voltage_value = (float)(((rx_buffer[1] & 3) << 8) + rx_buffer[2]) / 1023.0f * 9.9f;
    return 0;
}

static void ads_capture_snapshot(t_ads_spi_ctx *ads_ctx, float voltages[ADS_CHANNEL_COUNT],
    uint8_t valid[ADS_CHANNEL_COUNT])
{
    for (uint8_t ch = 0; ch < ADS_CHANNEL_COUNT; ch++) {
        voltages[ch] = 0.0f;
        valid[ch] = 0;
        if (ads_read_voltage(ads_ctx, ch, &voltages[ch]) == 0) {
            valid[ch] = 1;
        }
    }
}

static void ads_build_history_line(char *line, size_t line_size, unsigned long sample_counter,
    const float voltages[ADS_CHANNEL_COUNT], const uint8_t valid[ADS_CHANNEL_COUNT])
{
    int used = snprintf(line, line_size, "#%08lu", sample_counter);
    if (used < 0 || (size_t)used >= line_size) {
        return;
    }
    for (uint8_t ch = 0; ch < ADS_CHANNEL_COUNT; ch++) {
        int written;
        if (valid[ch]) {
            // 1 leading space + 4 chars value so it aligns with 4-char equalizer bars.
            written = snprintf(line + used, line_size - (size_t)used, " %4.1f", voltages[ch]);
        } else {
            written = snprintf(line + used, line_size - (size_t)used, " ERR ");
        }
        if (written < 0 || (size_t)written >= line_size - (size_t)used) {
            break;
        }
        used += written;
    }
}

static void ads_push_history(char history[ADS_HISTORY_LINES][ADS_HISTORY_LINE_LEN],
    unsigned int *history_count, const char *line)
{
    if (*history_count < ADS_HISTORY_LINES) {
        snprintf(history[*history_count], ADS_HISTORY_LINE_LEN, "%s", line);
        (*history_count)++;
        return;
    }

    for (unsigned int i = 1; i < ADS_HISTORY_LINES; i++) {
        snprintf(history[i - 1], ADS_HISTORY_LINE_LEN, "%s", history[i]);
    }
    snprintf(history[ADS_HISTORY_LINES - 1], ADS_HISTORY_LINE_LEN, "%s", line);
}

static int voltage_to_equalizer_steps(float voltage)
{
    double scaled;

    if (voltage < 0.0f) {
        voltage = 0.0f;
    } else if (voltage > 10.0f) {
        voltage = 10.0f;
    }

    scaled = ((double)voltage / 10.0) * (double)(EQ_ROWS * EQ_STEPS_PER_ROW);
    return (int)lround(scaled);
}

static void ads_render_dashboard(const char history[ADS_HISTORY_LINES][ADS_HISTORY_LINE_LEN],
    unsigned int history_count, const float voltages[ADS_CHANNEL_COUNT], const uint8_t valid[ADS_CHANNEL_COUNT],
    unsigned int points_per_period, unsigned int sample_delay_us)
{
    static const char *blocks[EQ_STEPS_PER_ROW + 1] = {" ", "▁", "▂", "▃", "▄", "▅", "▆", "▇", "█"};

    // Keep an always-updated dashboard: history above, equalizer on the last 5 lines.
    printf("\033[H\033[J");
    printf("=== MCP->ADS loopback test ===\n");
    printf("Config: resolution=%u points | delay=%u us | eq-every=%u | history-every=%u\n",
        points_per_period, sample_delay_us, EQUALIZER_EVERY, HISTORY_EVERY);
    printf("ADS voltage history (V):\n");
    for (unsigned int i = 0; i < history_count; i++) {
        printf("%s\n", history[i]);
    }
    for (int row = EQ_ROWS - 1; row >= 0; row--) {
        // Prefix width is fixed to 9 chars to match history line prefix (#00000000).
        printf("%2dV%6s", (row + 1) * 2, "");
        for (uint8_t ch = 0; ch < ADS_CHANNEL_COUNT; ch++) {
            int steps = valid[ch] ? voltage_to_equalizer_steps(voltages[ch]) : 0;
            int cell = steps - (row * EQ_STEPS_PER_ROW);

            if (cell < 0) {
                cell = 0;
            } else if (cell > EQ_STEPS_PER_ROW) {
                cell = EQ_STEPS_PER_ROW;
            }
            printf(" ");
            for (int w = 0; w < EQ_BAR_WIDTH; w++) {
                printf("%s", blocks[cell]);
            }
        }
        printf("\n");
    }
    printf("%9s", "");
    for (uint8_t ch = 0; ch < ADS_CHANNEL_COUNT; ch++) {
        printf(" %4u", ch);
    }
    printf("\n");
    fflush(stdout);
}

int main(int argc, char **argv)
{
    const char *i2c_bus = "/dev/i2c-1";
    const double two_pi = 2.0 * 3.14159265358979323846;
    const double phase_step = two_pi / 8.0;
    unsigned int points_per_period;
    unsigned int sample_delay_us;
    int i2c_fd;
    int ldac_ready;
    int ldac_error_reported = 0;
    unsigned long sample_counter = 0;
    char ads_history[ADS_HISTORY_LINES][ADS_HISTORY_LINE_LEN] = {{0}};
    unsigned int ads_history_count = 0;
    char history_line[ADS_HISTORY_LINE_LEN] = {0};
    float ads_voltages[ADS_CHANNEL_COUNT] = {0.0f};
    uint8_t ads_valid[ADS_CHANNEL_COUNT] = {0};
    t_ads_spi_ctx ads_ctx;
    int parse_status;

    parse_status = parse_runtime_options(argc, argv, &points_per_period, &sample_delay_us);
    if (parse_status > 0) {
        return 0;
    }

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    i2c_fd = i2c_init(i2c_bus);
    if (i2c_fd < 0) {
        return 1;
    }

    if (ads_spi_init(&ads_ctx) != 0) {
        close(i2c_fd);
        return 1;
    }

    scan_i2c_bus(i2c_fd);
    ldac_ready = (setup_ldac() == 0);
    if (!ldac_ready) {
        printf("Warning: LDAC init failed, fallback to immediate updates (UDAC=0).\n");
    }

    while (g_keep_running) {
        for (unsigned int i = 0; i < points_per_period && g_keep_running; i++) {
            uint16_t phased_values[8];
            uint8_t udac = ldac_ready ? 1 : 0;
            double angle = two_pi * (double)i / (double)points_per_period;

            for (int output = 0; output < 8; output++) {
                double shifted_angle = angle + ((double)output * phase_step);
                phased_values[output] = (uint16_t)(2048.0 + 2047.0 * sin(shifted_angle));
            }

            if (write_all_mcp_outputs(i2c_fd, udac, phased_values) != 0) {
                printf("Error: failed to write MCP4728 outputs\n");
                g_keep_running = 0;
                break;
            }

            if (ldac_ready) {
                if (ldac_pulse_low(LDAC1_GPIO) < 0 || ldac_pulse_low(LDAC2_GPIO) < 0) {
                    ldac_ready = 0;
                    if (!ldac_error_reported) {
                        printf("Warning: LDAC pulse failed, switching to immediate updates (UDAC=0).\n");
                        ldac_error_reported = 1;
                    }
                }
            }

            if ((sample_counter % EQUALIZER_EVERY) == 0) {
                ads_capture_snapshot(&ads_ctx, ads_voltages, ads_valid);
                if ((sample_counter % HISTORY_EVERY) == 0) {
                    ads_build_history_line(history_line, sizeof(history_line), sample_counter, ads_voltages, ads_valid);
                    ads_push_history(ads_history, &ads_history_count, history_line);
                }
                ads_render_dashboard(ads_history, ads_history_count, ads_voltages, ads_valid,
                    points_per_period, sample_delay_us);
            }

            sample_counter++;
            delay_microseconds(sample_delay_us);
        }
    }

    ads_spi_cleanup(&ads_ctx);
    cleanup_ldac();
    close(i2c_fd);
    printf("Stopped.\n");
    return 0;
}
