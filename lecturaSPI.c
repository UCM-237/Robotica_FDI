#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>

#define SPI_DEVICE "/dev/spidev0.0"
#define SPI_SPEED  1000000 // 1 MHz

// Función para leer un canal (0-7) del MCP3008 a través de SPI nativo
int read_mcp3008(int fd, uint8_t channel) {
    if (channel > 7) return -1;

    uint8_t tx[3] = {
        0x01,                                  // Start bit
        (uint8_t)((0x08 | channel) << 4),     // Single-ended + selección de canal
        0x00                                   // Byte de relleno
    };
    uint8_t rx[3] = {0};

    struct spi_ioc_transfer tr = {
        .tx_buf = (unsigned long)tx,
        .rx_buf = (unsigned long)rx,
        .len = 3,
        .speed_hz = SPI_SPEED,
        .delay_usecs = 0,
        .bits_per_word = 8,
    };

    if (ioctl(fd, SPI_IOC_MESSAGE(1), &tr) < 1) {
        perror("Error al enviar mensaje SPI");
        return -1;
    }

    // Extracción del valor de 10 bits de la respuesta
    int value = ((rx[1] & 0x03) << 8) | rx[2];
    return value;
}

int main(void) {
    // Abrir el dispositivo SPI
    int spi_fd = open(SPI_DEVICE, O_RDWR);
    if (spi_fd < 0) {
        perror("Error al abrir /dev/spidev0.0. Verifica que SPI esté activado en raspi-config");
        return EXIT_FAILURE;
    }

    // Configuración opcional del modo SPI (Modo 0: CPOL=0, CPHA=0)
    uint8_t mode = SPI_MODE_0;
    if (ioctl(spi_fd, SPI_IOC_WR_MODE, &mode) < 0) {
        perror("Error al configurar el modo SPI");
        close(spi_fd);
        return EXIT_FAILURE;
    }

    printf("Leyendo canales analógicos del MCP3008 vía SPI...\n");

    while (1) {
        for (int ch = 0; ch < 8; ch++) {
            int raw_val = read_mcp3008(spi_fd, ch);
            float voltage = (raw_val / 1023.0f) * 3.3f; // Conversión a voltios (Vref = 3.3V)
            printf("Canal %d: %4d (%.2f V) | ", ch, raw_val, voltage);
        }
        printf("\n");
        usleep(500000); // 500 ms
    }

    close(spi_fd);
    return EXIT_SUCCESS;
}
