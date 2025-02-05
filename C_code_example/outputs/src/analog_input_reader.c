#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <spidev_lib.h>
#include <unistd.h>
#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <linux/i2c.h>  // Pour struct i2c_msg

#define CHIP_NAME "gpiochip0"

#define DAC_1 0x63 // Nouvelle adresse pour le premier MCP4728
#define DAC_2 0x64 // Nouvelle adresse pour le deuxième MCP4728

// DAC1, DAC0
// 0, 0 : Channel A
// 0, 1 : Channel B
// 1, 0 : Channel C
// 1, 1 : Channel D


// UDAC
// 0 : Upload
// 1 : Do not upload


// PD1, PD0
// 0, 0 : Normal mode
// 0, 1 : Loaded with 1kOhm
// 1, 0 : Loaded with 100kOhm
// 1, 1 : Loaded with 500kOhm

// Gx
// 0 : x1 gain
// 1 : x2 gain

// D11 - D0
// Input Data


// Ajouter ces fonctions de remplacement pour les délais
void delayMicroseconds(unsigned int micros) {
    usleep(micros);
}

void delay(unsigned int millis) {
    usleep(millis * 1000);
}



// Fonction pour initialiser l'I2C
int i2c_init(const char *i2c_bus)
{
	int fd;
	if ((fd = open(i2c_bus, O_RDWR)) < 0)
	{
		perror("Erreur lors de l'ouverture du bus I2C");
		return -1;
	}
	return fd;
}

// Fonction pour vérifier si un périphérique est présent à une adresse I2C spécifique
int check_device_at_address(int fd, uint8_t address)
{
	if (ioctl(fd, I2C_SLAVE, address) < 0)
	{
		return 0; // Pas de périphérique à cette adresse
	}
	// Tenter une lecture pour vérifier la présence
	uint8_t buffer;
	if (read(fd, &buffer, 1) != 1)
	{
		return 0; // Pas de réponse du périphérique
	}
	return 1; // Périphérique trouvé à cette adresse
}



void scan_i2c_bus(int file) {
    printf("\nScanning I2C bus...\n");
    printf("     0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F\n");

    for (int i = 0; i < 128; i += 16) {
        printf("%02x: ", i);
        
        for (int j = 0; j < 16; j++) {
            int addr = i + j;
            if (addr < 0x03 || addr > 0x77) {
                printf("   "); // Skip reserved addresses
                continue;
            }

            // Try to communicate with the device
            if (ioctl(file, I2C_SLAVE, addr) < 0) {
                printf("-- ");
                continue;
            }

            // Try to read from the device
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
        printf("Erreur lors de l'ouverture du bus I2C\n");
        return;
    }

    if (write(fd, data, length) != length) {
        printf("Erreur lors de l'écriture sur le périphérique\n");
        return;
    }
}

void main(void)
{
	const char *i2c_bus = "/dev/i2c-1";
	int i2c_fd = i2c_init(i2c_bus);

	// Rechercher le périphérique à l'adresse par défaut

	scan_i2c_bus(i2c_fd);
}