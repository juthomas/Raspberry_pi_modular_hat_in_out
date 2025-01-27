#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <spidev_lib.h>
#include <unistd.h>
#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <gpiod.h>
#include <sys/ioctl.h>

// GPIO pins for LDAC and RDY of the two MCP4728s
#define LDAC_0_PIN 00  // Numéro GPIO pour LDAC
#define RDY_0_PIN 25    // Numéro GPIO pour RDY
#define LDAC_1_PIN 1  // Numéro GPIO pour LDAC
#define RDY_1_PIN 21    // Numéro GPIO pour RDY
#define CHIP_NAME "gpiochip0"

#define NEW_ADDR_1 0x64 // Nouvelle adresse pour le premier MCP4728

static struct gpiod_chip *chip;
static struct gpiod_line *ldac_0_line;
static struct gpiod_line *rdy_0_line;
static struct gpiod_line *ldac_1_line;
static struct gpiod_line *rdy_1_line;


// Ajouter ces fonctions de remplacement pour les délais
void delayMicroseconds(unsigned int micros) {
    usleep(micros);
}

void delay(unsigned int millis) {
    usleep(millis * 1000);
}


// Remplacer les fonctions digitalWrite/digitalRead
static inline void gpio_write(int value)
{
	gpiod_line_set_value(ldac_1_line, value);
}

static inline int gpio_read(void)
{
	return gpiod_line_get_value(rdy_1_line);
}

void init_gpio()
{
	// Ouvrir le chip GPIO
	chip = gpiod_chip_open_by_name(CHIP_NAME);
	if (!chip) {
		perror("Erreur ouverture GPIO chip");
		exit(1);
	}

	// Obtenir les lignes GPIO
	ldac_0_line = gpiod_chip_get_line(chip, LDAC_0_PIN);
	rdy_0_line = gpiod_chip_get_line(chip, RDY_0_PIN);
	ldac_1_line = gpiod_chip_get_line(chip, LDAC_1_PIN);
	rdy_1_line = gpiod_chip_get_line(chip, RDY_1_PIN);

	if (!ldac_0_line || !rdy_0_line || !ldac_1_line || !rdy_1_line) {
		perror("Erreur obtention des lignes GPIO");
		exit(1);
	}

	if (gpiod_line_request_output(ldac_0_line, "analog_input", 1) < 0) {
		perror("Erreur configuration LDAC");
		exit(1);
	}
	if (gpiod_line_request_input(rdy_0_line, "analog_input") < 0) {
		perror("Erreur configuration RDY");
		exit(1);
	}
	// Configurer les lignes
	if (gpiod_line_request_output(ldac_1_line, "analog_input", 1) < 0) {
		perror("Erreur configuration LDAC");
		exit(1);
	}

	if (gpiod_line_request_input(rdy_1_line, "analog_input") < 0) {
		perror("Erreur configuration RDY");
		exit(1);
	}
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
	printf("État de RDY avant l'envoi de la commande : %d\n", gpio_read());

	// Transition de LDAC : doit être haut pour commencer
	gpio_write(1);
	delayMicroseconds(50); // Assurer une stabilité avant l'envoi de la commande

	// Envoyer la commande pour changer l'adresse
	if (write(file, write_cmd, 4) != 4)
	{
		perror("Erreur lors de l'envoi de la commande de changement d'adresse");
		return -1;
	}

	// Transition de LDAC de haut à bas juste après le 2ème octet
	delayMicroseconds(50);		 // Délai court après l'envoi des deux premiers octets
	gpio_write(0); // LDAC passe à bas pour terminer la commande

	// Vérifier l'état de RDY après l'envoi de la commande
	printf("État de RDY après la commande : %d\n", gpio_read());

	// Attendre que RDY passe à High (l'EEPROM est en train de se programmer)
	// while (gpio_read() == 0)
	// {
	// 	printf("EEPROM est en cours de programmation, attente...\n");
	// }
		delay(100); // Attente en millisecondes

	// Vérifier si l'ancienne adresse est toujours présente
	if (check_device_at_address(file, old_address)) {
        printf("Le périphérique est toujours à l'ancienne adresse 0x%02X\n", old_address);
    } else {
        printf("Le périphérique n'est plus à l'ancienne adresse 0x%02X\n", old_address);
    }

	printf("Adresse changée avec succès à 0x%02X.\n", new_address);
	return 0;
}

void cleanup_gpio(void)
{
	if (ldac_1_line) gpiod_line_release(ldac_1_line);
	if (rdy_1_line) gpiod_line_release(rdy_1_line);
	if (chip) gpiod_chip_close(chip);
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



void main(void)
{
	const char *i2c_bus = "/dev/i2c-1";
	int i2c_fd = i2c_init(i2c_bus);
	init_gpio();

	// Rechercher le périphérique à l'adresse par défaut
	uint8_t old_address = 0x60; // Adresse par défaut du MCP4728

	scan_i2c_bus(i2c_fd);

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

	cleanup_gpio();
}