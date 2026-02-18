#!/usr/bin/env bash
set -euo pipefail

DEFAULT_I2C_BAUDRATE=1000000
I2C_BAUDRATE="${I2C_BAUDRATE:-$DEFAULT_I2C_BAUDRATE}"
SPI_LIB_REPO="${SPI_LIB_REPO:-https://github.com/juthomas/spidev-lib.git}"
REBOOT_AFTER_SETUP=0

usage() {
    cat <<'EOF'
Usage: sudo ./scripts/install_rpi_dependencies.sh [--baudrate <value>] [--reboot]

Options:
  --baudrate <value>  Set I2C ARM baudrate (default: 1000000)
  --reboot            Reboot automatically after configuration
  --help              Show this help

Environment variables:
  I2C_BAUDRATE        Same as --baudrate
  SPI_LIB_REPO        Override spidev-lib git URL
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --baudrate)
            if [[ $# -lt 2 ]]; then
                echo "Error: --baudrate requires a value" >&2
                exit 1
            fi
            I2C_BAUDRATE="$2"
            shift 2
            ;;
        --reboot)
            REBOOT_AFTER_SETUP=1
            shift
            ;;
        --help|-h)
            usage
            exit 0
            ;;
        *)
            echo "Error: unknown option '$1'" >&2
            usage
            exit 1
            ;;
    esac
done

if [[ ! "$I2C_BAUDRATE" =~ ^[0-9]+$ ]]; then
    echo "Error: invalid I2C baudrate '$I2C_BAUDRATE'" >&2
    exit 1
fi

if [[ "$EUID" -ne 0 ]]; then
    echo "Error: run this script with sudo or as root" >&2
    exit 1
fi

CONFIG_FILE="/boot/firmware/config.txt"
if [[ ! -f "$CONFIG_FILE" ]]; then
    CONFIG_FILE="/boot/config.txt"
fi
if [[ ! -f "$CONFIG_FILE" ]]; then
    echo "Error: unable to find Raspberry Pi config.txt" >&2
    exit 1
fi

BACKUP_FILE="${CONFIG_FILE}.bak.$(date +%Y%m%d_%H%M%S)"
TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT

echo "[1/4] Installing apt dependencies..."
export DEBIAN_FRONTEND=noninteractive
apt-get update
apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    git \
    pkg-config \
    libgpiod-dev \
    i2c-tools

echo "[2/4] Installing spidev-lib..."
if [[ -f /usr/local/include/spidev_lib.h && -f /usr/local/lib/libspidev-lib.a ]]; then
    echo "spidev-lib already present in /usr/local, skipping build."
else
    git clone --depth 1 "$SPI_LIB_REPO" "$TMP_DIR/spidev-lib"
    cmake -S "$TMP_DIR/spidev-lib" -B "$TMP_DIR/spidev-lib/build" -DCMAKE_BUILD_TYPE=Release
    cmake --build "$TMP_DIR/spidev-lib/build" -j"$(nproc)"
    cmake --install "$TMP_DIR/spidev-lib/build"
    if command -v ldconfig >/dev/null 2>&1; then
        ldconfig
    fi
fi

echo "[3/4] Configuring I2C/SPI in $CONFIG_FILE..."
cp "$CONFIG_FILE" "$BACKUP_FILE"

if grep -qE '^[[:space:]]*dtparam=i2c_arm=' "$CONFIG_FILE"; then
    sed -i -E "s|^[[:space:]]*dtparam=i2c_arm=.*$|dtparam=i2c_arm=on,i2c_arm_baudrate=${I2C_BAUDRATE}|" "$CONFIG_FILE"
else
    printf "\ndtparam=i2c_arm=on,i2c_arm_baudrate=%s\n" "$I2C_BAUDRATE" >> "$CONFIG_FILE"
fi

if grep -qE '^[[:space:]]*dtparam=spi=' "$CONFIG_FILE"; then
    sed -i -E "s|^[[:space:]]*dtparam=spi=.*$|dtparam=spi=on|" "$CONFIG_FILE"
else
    printf "dtparam=spi=on\n" >> "$CONFIG_FILE"
fi

echo "[4/4] Done."
echo "Backup created at: $BACKUP_FILE"
echo "Current bus config:"
grep -nE '^[[:space:]]*dtparam=(i2c_arm|spi)=' "$CONFIG_FILE" || true

if [[ "$REBOOT_AFTER_SETUP" -eq 1 ]]; then
    echo "Rebooting now..."
    reboot
else
    echo "Reboot required to apply I2C baudrate changes."
    echo "Run: sudo reboot"
fi
