#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <spidev_lib.h>
#include <unistd.h>

float get_voltage_value(uint8_t channel)
{
	spi_config_t	spi_config;
	uint8_t			tx_buffer[3] = {0};
	uint8_t 		rx_buffer[3] = {0};
	int				spifd;
	float			voltage_value;

	spi_config.mode=0;
	spi_config.speed=1000000;
	spi_config.delay=0;
	spi_config.bits_per_word=8;

	spifd=spi_open(channel < 8 ? "/dev/spidev0.0" : "/dev/spidev0.1",spi_config);
	if (spifd < 0)
	{
		printf("make sure that \"/dev/spidev0.0\" and \"/dev/spidev0.1\" are available\n");
		return (-1);
	}
	tx_buffer[0] = 1;
	tx_buffer[1] = ((8 + channel - (channel < 8 ? 0 : 8)) << 4);
	tx_buffer[2] = 0;
	spi_xfer(spifd,tx_buffer,3,rx_buffer,3);
	voltage_value = (float)(((rx_buffer[1] & 3) << 8) + rx_buffer[2]) / 1023.0 * 9.9;
	spi_close(spifd); 
	return (voltage_value);
}

void main( void)
{
	for (;;)
	{
		for (int i = 0; i < 16; i++)
		{
			printf("%.2f ", get_voltage_value(i));
		}
		printf("\n");
		sleep(1);
	}
	return (0);
}
