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

## Install dependency for C coding

[spidev-lib](https://github.com/juthomas/spidev-lib)

dont forget to add the `-lspidev-lib` flag when compiling

`sudo apt install libgpiod-dev`

## Electrical Voltage dividers & multiplier

### Divider
https://tinyurl.com/24boqd2k

### Multiplier
<!-- https://tinyurl.com/27d8ra9q -->
https://tinyurl.com/2aczbd22



https://github.com/WiringPi/WiringPi
