#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <spidev_lib.h>
#include <unistd.h>
#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <wiringPi.h>
#include <sys/ioctl.h>

// GPIO pins for LDAC and RDY of the two MCP4728s
#define LDAC_1_PIN 30 // GPIO0 pour le MCP4728 numéro 1
#define RDY_1_PIN 6	  // GPIO1 pour le MCP4728 numéro 1

#define NEW_ADDR_1 0x64 // Nouvelle adresse pour le premier MCP4728

void init_gpio()
{
	wiringPiSetup();
	pinMode(LDAC_1_PIN, OUTPUT);
	pinMode(RDY_1_PIN, INPUT);

	pullUpDnControl(RDY_1_PIN, PUD_UP);
	digitalWrite(LDAC_1_PIN, 1); // LDAC initialement haut
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

// Fonction pour changer l'adresse d'un MCP4728 avec gestion correcte de LDAC
int mcp4728_change_address(int file, uint8_t old_address, uint8_t new_address)
{
	unsigned char write_cmd[4]; // Tableau pour la commande I2C

	// Commande pour écrire l'adresse (C2=0, C1=1, C0=1)
	write_cmd[0] = 0xC1;
	write_cmd[1] = (new_address << 1) & 0xFE; // Bits d'adresse A2, A1, A0
	write_cmd[2] = 0x00;					  // Complément de la commande
	write_cmd[3] = 0x00;					  // Compléter le tableau de commande

	// Spécifier l'adresse I²C du MCP4728 par défaut
	if (ioctl(file, I2C_SLAVE, old_address) < 0)
	{
		perror("Erreur lors de la communication avec le MCP4728");
		return -1;
	}

	// Vérifier l'état de RDY avant d'envoyer la commande
	printf("État de RDY avant l'envoi de la commande : %d\n", digitalRead(RDY_1_PIN));

	// Transition de LDAC : doit être haut pour commencer
	digitalWrite(LDAC_1_PIN, 1);
	delayMicroseconds(50); // Assurer une stabilité avant l'envoi de la commande

	// Envoyer la commande pour changer l'adresse
	if (write(file, write_cmd, 4) != 4)
	{
		perror("Erreur lors de l'envoi de la commande de changement d'adresse");
		return -1;
	}

	// Transition de LDAC de haut à bas juste après le 2ème octet
	delayMicroseconds(50);		 // Délai court après l'envoi des deux premiers octets
	digitalWrite(LDAC_1_PIN, 0); // LDAC passe à bas pour terminer la commande

	// Vérifier l'état de RDY après l'envoi de la commande
	printf("État de RDY après la commande : %d\n", digitalRead(RDY_1_PIN));

	// Attendre que RDY passe à High (l'EEPROM est en train de se programmer)
	while (digitalRead(RDY_1_PIN) == 0)
	{
		printf("EEPROM est en cours de programmation, attente...\n");
		delay(100); // Attente en millisecondes
	}

	// Vérifier si l'ancienne adresse est toujours présente
	if (check_device_at_address(file, old_address)) {
        printf("Le périphérique est toujours à l'ancienne adresse 0x%02X\n", old_address);
    } else {
        printf("Le périphérique n'est plus à l'ancienne adresse 0x%02X\n", old_address);
    }

	printf("Adresse changée avec succès à 0x%02X.\n", new_address);
	return 0;
}

void main(void)
{
	const char *i2c_bus = "/dev/i2c-1";
	int i2c_fd = i2c_init(i2c_bus);
	init_gpio();

	// Rechercher le périphérique à l'adresse par défaut
	uint8_t old_address = 0x60; // Adresse par défaut du MCP4728

	if (check_device_at_address(i2c_fd, old_address))
	{
		printf("Périphérique MCP4728 trouvé à l'adresse %02x\n", old_address);

		// Changer l'adresse du MCP4728
		if (mcp4728_change_address(i2c_fd, old_address, NEW_ADDR_1) == 0)
		{
			printf("Adresse changée avec succès à 0x%02X.\n", NEW_ADDR_1);
		}
		else
		{
			printf("Échec du changement d'adresse à 0x%02X.\n", NEW_ADDR_1);
		}
	}
	else
	{
		printf("Aucun périphérique trouvé à l'adresse %02x\n", old_address);
	}

	if (check_device_at_address(i2c_fd, NEW_ADDR_1))
	{
		printf("Périphérique MCP4728 trouvé à l'adresse %02x\n", old_address);
	}
	else
	{
		printf("Aucun périphérique trouvé à l'adresse %02x\n", NEW_ADDR_1);
	}
}