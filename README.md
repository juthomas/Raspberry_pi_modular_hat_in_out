# Raspberry Pi hat with inputs and outputs for modular synthesizer 

![alt text](https://github.com/juthomas/Raspberry_pi_modular_hat_in_out/blob/master/README_images/Analog_Front.png)
![alt text](https://github.com/juthomas/Raspberry_pi_modular_hat_in_out/blob/master/README_images/Analog_Back.png)

## Schematics

- [Link to pdf](https://github.com/juthomas/Raspberry_pi_modular_hat_in_out/blob/master/Schematics%20PDF/Modular_hat_in_out.pdf)

## Bill Of Materials

- [Link to IBOM](https://juthomas.github.io/Raspberry_pi_modular_hat_in_out/)

## Components

- [MCP3008](https://www.lcsc.com/product-detail/Analog-To-Digital-Converters-ADCs_Microchip-Tech-MCP3008-I-SL_C1520159.html) C1520159

- [LM324](https://www.lcsc.com/product-detail/Operational-Amplifier_STMicroelectronics-LM324DT_C71035.html) C71035

- [MCP4728](https://www.lcsc.com/product-detail/Digital-To-Analog-Converters-DACs_Microchip-Tech-MCP4728-E-UN_C108207.html) C108207

## C code examples (clean naming)

- Input reader: `C_code_example/inputs/src/input_reader.c`
  - binary name: `input_reader`
- Output generator: `C_code_example/outputs/src/output_generator.c`
  - binary name: `output_generator`
- Input/Output loopback tester: `C_code_example/input_outputs/src/input_output_tester.c`
  - binary name: `input_output_tester`

## Install dependencies and configure I2C (recommended)

Use the setup script:

`sudo ./scripts/install_rpi_dependencies.sh --baudrate 1000000`

What it does:

- installs apt dependencies (`build-essential`, `cmake`, `git`, `pkg-config`, `libgpiod-dev`, `i2c-tools`)
- installs [spidev-lib](https://github.com/juthomas/spidev-lib) in `/usr/local`
- sets `dtparam=i2c_arm=on,i2c_arm_baudrate=1000000`
- ensures `dtparam=spi=on`
- keeps a backup of `config.txt` before editing

Reboot is required after running the script.

### Manual install (if needed)

`sudo apt install build-essential cmake git pkg-config libgpiod-dev i2c-tools`

`git clone https://github.com/juthomas/spidev-lib.git`

`cd spidev-lib && mkdir -p build && cd build && cmake .. && make -j$(nproc) && sudo make install`

Do not forget to link with `-lspidev-lib` when compiling C code.

## Shell autocompletion for CLI options

The repository includes completion scripts for:

- `output_generator`
- `input_output_tester`
- `install_rpi_dependencies.sh`

Zsh (current session):

`source scripts/completions/rpi_hat_tools.zsh`

Zsh (persistent):

`echo 'source /absolute/path/to/Raspberry_pi_modular_hat_in_out/scripts/completions/rpi_hat_tools.zsh' >> ~/.zshrc`

Bash (current session):

`source scripts/completions/rpi_hat_tools.bash`

Bash (persistent):

`echo 'source /absolute/path/to/Raspberry_pi_modular_hat_in_out/scripts/completions/rpi_hat_tools.bash' >> ~/.bashrc`

## Build and run

Input side (ADC):

`cd C_code_example/inputs && make && ./input_reader`

With runtime options:

`cd C_code_example/inputs && make && ./input_reader --delay-us=100000`

Dashboard cadence is controlled in source with `EQUALIZER_EVERY` and `HISTORY_EVERY` in `C_code_example/inputs/src/input_reader.c`.

Output side (MCP4728):

`cd C_code_example/outputs && make && ./output_generator`

With runtime options:

`cd C_code_example/outputs && make && ./output_generator --resolution=1000 --delay-us=1`

Dashboard cadence is controlled in source with `EQUALIZER_EVERY` and `HISTORY_EVERY` in `C_code_example/outputs/src/output_generator.c`.

Input/Output combined test (MCP4728 sine with per-channel phase + ADS monitoring):

`cd C_code_example/input_outputs && make && ./input_output_tester --resolution=1000 --delay-us=1`

Dashboard cadence is controlled in source with `EQUALIZER_EVERY` and `HISTORY_EVERY` in `C_code_example/input_outputs/src/input_output_tester.c`.

## Electrical Voltage dividers & multiplier

### Divider
https://tinyurl.com/24boqd2k

### Multiplier
<!-- https://tinyurl.com/27d8ra9q -->
https://tinyurl.com/2aczbd22



https://github.com/WiringPi/WiringPi
