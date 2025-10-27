#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <spidev_lib.h>
#include <unistd.h>
#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <linux/i2c.h>  // Pour struct i2c_msg
#include <math.h>
#include <stdint.h>

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

// Fonction pour écrire une valeur sur un canal spécifique du MCP4728
// channel: 0=Channel A, 1=Channel B, 2=Channel C, 3=Channel D
// value: valeur 12-bit (0-4095)
// vref: 0=VDD reference, 1=Internal reference
// gain: 0=x1 gain, 1=x2 gain
// power_down: 0=Normal, 1=Vout loaded with 1kOhm, 2=Vout loaded with 100kOhm, 3=Vout loaded with 500kOhm
int mcp4728_write_channel(int fd, uint8_t address, uint8_t channel, uint16_t value, uint8_t vref, uint8_t gain, uint8_t power_down) {
    if (channel > 3) {
        printf("Erreur: canal invalide (0-3)\n");
        return -1;
    }
    
    if (value > 4095) {
        printf("Erreur: valeur invalide (0-4095)\n");
        return -1;
    }
    
    // Format de commande MCP4728 Fast Write (méthode simplifiée):
    // Byte 0: Command (0x58 + channel bits), High nibble = upper 4 bits of value
    // Byte 1: Low byte of value
    
    uint8_t cmd[2];
    
    // Commande Fast Write: 0x50 base + channel<<1 + UDAC
    // UDAC=0 pour mise à jour immédiate
    cmd[0] = 0x50 | (channel << 1);
    
    // Mettre les 4 bits supérieurs de value dans le nibble haut
    cmd[0] |= (value >> 8) & 0x0F;
    
    // Les 8 bits inférieurs de la valeur
    cmd[1] = value & 0xFF;
    
    if (ioctl(fd, I2C_SLAVE, address) < 0) {
        printf("Erreur lors de l'ouverture du périphérique MCP4728 à l'adresse 0x%02X\n", address);
        return -1;
    }
    
    if (write(fd, cmd, 2) != 2) {
        printf("Erreur lors de l'écriture sur le MCP4728\n");
        return -1;
    }
    
    return 0;
}

// Fonction simplifiée pour écrire une valeur directement sur un canal
int mcp4728_set_output(int fd, uint8_t address, uint8_t channel, uint16_t value) {
    return mcp4728_write_channel(fd, address, channel, value, 0, 0, 0);
}

// Fonction pour écrire sur plusieurs canaux
int mcp4728_write_multiple_channels(int fd, uint8_t address, uint16_t values[4]) {
    // Écrire chaque canal individuellement
    for (int i = 0; i < 4; i++) {
        if (mcp4728_set_output(fd, address, i, values[i]) != 0) {
            printf("Erreur lors de l'écriture du canal %d\n", i);
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
		printf("Erreur: impossible d'initialiser le bus I2C\n");
		return;
	}

	// Scanner le bus I2C pour voir les périphériques connectés
	scan_i2c_bus(i2c_fd);

	printf("\n=== Test des MCP4728 ===\n");
	
	// Exemple 1: Écrire une valeur sur un canal spécifique
	printf("\nTest 1: Écriture sur le canal A du premier MCP4728 (0x63)\n");
	printf("Valeur: 2048 (environ 2.5V avec référence 5V)\n");
	mcp4728_set_output(i2c_fd, DAC_1, 0, 2048);  // Canal A, valeur 2048
	usleep(100000);  // Attendre 100ms
	
	// Exemple 2: Écrire sur le canal B du deuxième MCP4728
	printf("\nTest 2: Écriture sur le canal B du deuxième MCP4728 (0x64)\n");
	printf("Valeur: 4095 (maximum - environ 5V avec référence 5V)\n");
	mcp4728_set_output(i2c_fd, DAC_2, 1, 4095);  // Canal B, valeur maximale
	usleep(100000);
	
	// Exemple 3: Écrire sur plusieurs canaux du premier MCP4728 simultanément
	printf("\nTest 3: Écriture sur tous les canaux du premier MCP4728\n");
	uint16_t values[4] = {1024, 2048, 3072, 4095};  // Valeurs croissantes
	mcp4728_write_multiple_channels(i2c_fd, DAC_1, values);
	usleep(100000);
	
	// Exemple 4: Balayage sinusoïdal sur un canal
	printf("\nTest 4: Balayage sur le canal C du deuxième MCP4728\n");
	for (int i = 0; i < 100; i++) {
		// Générer une valeur sinusoïdale approximative
		uint16_t value = (uint16_t)(2048 + 2048 * sin(2.0 * 3.14159 * i / 100.0));
		mcp4728_set_output(i2c_fd, DAC_2, 2, value);
		usleep(50000);  // 50ms entre chaque valeur
	}
	
	printf("\nTests terminés!\n");
	
	close(i2c_fd);
}