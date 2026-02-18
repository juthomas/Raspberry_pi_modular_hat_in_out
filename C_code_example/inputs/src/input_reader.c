#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <spidev_lib.h>
#include <unistd.h>
#include <stdint.h>
#include <signal.h>
#include <errno.h>
#include <math.h>

#define ADS_CHANNEL_COUNT 16
#define ADS_HISTORY_LINES 14
#define ADS_HISTORY_LINE_LEN 256

#define EQ_ROWS 5
#define EQ_STEPS_PER_ROW 8
#define EQ_BAR_WIDTH 4

#define DEFAULT_SAMPLE_DELAY_US 10000U
#define MAX_SAMPLE_DELAY_US 1000000U

#define EQUALIZER_EVERY 2U
#define HISTORY_EVERY 10U

typedef struct s_ads_spi_ctx {
    int spi0_fd;
    int spi1_fd;
    int ready;
}   t_ads_spi_ctx;

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

static int parse_runtime_options(int argc, char **argv, unsigned int *sample_delay_us)
{
    *sample_delay_us = DEFAULT_SAMPLE_DELAY_US;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0) {
            printf("Usage: %s [--delay-us=<microseconds>]\n", argv[0]);
            printf("  --delay-us      : delay between updates in microseconds (0..%u), default: %u\n",
                MAX_SAMPLE_DELAY_US, DEFAULT_SAMPLE_DELAY_US);
            printf("Display cadence is controlled by EQUALIZER_EVERY and HISTORY_EVERY defines in source.\n");
            return 1;
        } else if (strncmp(argv[i], "--delay-us=", 11) == 0) {
            *sample_delay_us = parse_u32_or_default(argv[i] + 11, "delay-us",
                DEFAULT_SAMPLE_DELAY_US, 0U, MAX_SAMPLE_DELAY_US);
        } else {
            printf("Warning: unknown option '%s' (use --help)\n", argv[i]);
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

    if (!ctx->ready || channel >= ADS_CHANNEL_COUNT || !voltage_value) {
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
    unsigned int sample_delay_us)
{
    static const char *blocks[EQ_STEPS_PER_ROW + 1] = {" ", "▁", "▂", "▃", "▄", "▅", "▆", "▇", "█"};

    printf("\033[H\033[J");
    printf("=== ADS input monitor ===\n");
    printf("Config: delay=%u us | eq-every=%u | history-every=%u\n",
        sample_delay_us, EQUALIZER_EVERY, HISTORY_EVERY);
    printf("ADS voltage history (V):\n");

    for (unsigned int i = 0; i < history_count; i++) {
        printf("%s\n", history[i]);
    }

    for (int row = EQ_ROWS - 1; row >= 0; row--) {
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
    unsigned int sample_delay_us;
    unsigned long sample_counter = 0;
    char ads_history[ADS_HISTORY_LINES][ADS_HISTORY_LINE_LEN] = {{0}};
    unsigned int ads_history_count = 0;
    char history_line[ADS_HISTORY_LINE_LEN] = {0};
    float ads_voltages[ADS_CHANNEL_COUNT] = {0.0f};
    uint8_t ads_valid[ADS_CHANNEL_COUNT] = {0};
    t_ads_spi_ctx ads_ctx;
    int parse_status;

    parse_status = parse_runtime_options(argc, argv, &sample_delay_us);
    if (parse_status > 0) {
        return 0;
    }

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    if (ads_spi_init(&ads_ctx) != 0) {
        return 1;
    }

    while (g_keep_running) {
        if ((sample_counter % EQUALIZER_EVERY) == 0) {
            ads_capture_snapshot(&ads_ctx, ads_voltages, ads_valid);
            if ((sample_counter % HISTORY_EVERY) == 0) {
                ads_build_history_line(history_line, sizeof(history_line), sample_counter, ads_voltages, ads_valid);
                ads_push_history(ads_history, &ads_history_count, history_line);
            }
            ads_render_dashboard(ads_history, ads_history_count, ads_voltages, ads_valid,
                sample_delay_us);
        }

        sample_counter++;
        delay_microseconds(sample_delay_us);
    }

    ads_spi_cleanup(&ads_ctx);
    printf("Stopped.\n");
    return 0;
}
