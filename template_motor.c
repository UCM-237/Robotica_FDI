#include <gpiod.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>

#define CHIP_NAME "/dev/gpiochip0"

// Pines Motor
// CAMBIAR SU NECESARIO
#define PIN_IN1 14
#define PIN_IN2 16


// Ruta del sysfs para el PWM Hardware en GPIO 12 (PWM0)
#define PWM_PATH "/sys/class/pwm/pwmchip0/pwm0"

// --------------------------------------------------------------------------
// Funciones de control de PWM por sysfs
// --------------------------------------------------------------------------
void sysfs_write(const char *path, const char *value) {
    int fd = open(path, O_WRONLY);
    if (fd < 0) return;
    write(fd, value, strlen(value));
    close(fd);
}

void init_pwm(void) {
    // Exportar el canal PWM0 si no está exportado
    sysfs_write("/sys/class/pwm/pwmchip0/export", "0");
    usleep(100000); // Esperar a que el Kernel cree la carpeta

    // Periodo en nanosegundos (20.000.000 ns = 50 Hz, estándar para motores)
    sysfs_write(PWM_PATH "/period", "20000000");
    sysfs_write(PWM_PATH "/duty_cycle", "0");
    sysfs_write(PWM_PATH "/enable", "1");
}

void set_motor_speed(int duty_percent) {
    if (duty_percent < 0) duty_percent = 0;
    if (duty_percent > 100) duty_percent = 100;

    // Calcular el duty cycle en nanosegundos respecto al periodo (20.000.000 ns)
    long duty_ns = (20000000L * duty_percent) / 100;
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%ld", duty_ns);
    sysfs_write(PWM_PATH "/duty_cycle", buffer);
}


// --------------------------------------------------------------------------
// Control de Dirección del Motor (IN1 e IN2)
// --------------------------------------------------------------------------
typedef enum { MOTOR_FORWARD, MOTOR_BACKWARD, MOTOR_STOP } MotorDirection;

void set_motor_direction(struct gpiod_line_request *motor_req, MotorDirection dir) {
    enum gpiod_line_value values[2];
    
    if (dir == MOTOR_FORWARD) {
        values[0] = GPIOD_LINE_VALUE_ACTIVE; // IN1
        values[1] = GPIOD_LINE_VALUE_INACTIVE;  // IN2
    } else if (dir == MOTOR_BACKWARD) {
        values[0] = GPIOD_LINE_VALUE_INACTIVE;  // IN1
        values[1] = GPIOD_LINE_VALUE_ACTIVE; // IN2
    } else {
        values[0] = GPIOD_LINE_VALUE_INACTIVE;  // IN1
        values[1] = GPIOD_LINE_VALUE_INACTIVE;   // IN2
    }

    gpiod_line_request_set_values(motor_req, values);
}

// --------------------------------------------------------------------------
// Main
// --------------------------------------------------------------------------
int main(void) {
    // 1. Inicializar PWM para la velocidad (ENA)
    init_pwm();

    // 2. Configurar pines del Motor (IN1 e IN2) con libgpiod
    struct gpiod_chip *chip = gpiod_chip_open(CHIP_NAME);
    if (!chip) {
        perror("Error al abrir gpiochip para motor");
        return EXIT_FAILURE;
    }

    struct gpiod_line_settings *motor_settings = gpiod_line_settings_new();
    gpiod_line_settings_set_direction(motor_settings, GPIOD_LINE_DIRECTION_OUTPUT);

    unsigned int motor_offsets[2] = {PIN_IN1, PIN_IN2};
    struct gpiod_line_config *motor_line_cfg = gpiod_line_config_new();
    gpiod_line_config_add_line_settings(motor_line_cfg, motor_offsets, 2, motor_settings);

    struct gpiod_request_config *motor_req_cfg = gpiod_request_config_new();
    gpiod_request_config_set_consumer(motor_req_cfg, "motor-control");

    struct gpiod_line_request *motor_request = gpiod_chip_request_lines(chip, motor_req_cfg, motor_line_cfg);

    // 3. Secuencia de prueba del motor
    // VUESTRO CÓDIGO AQUÍ

    
    
    //4. Cerramos todo bien	 
    gpiod_line_request_release(motor_request);
    gpiod_request_config_free(motor_req_cfg);
    gpiod_line_config_free(motor_line_cfg);
    gpiod_line_settings_free(motor_settings);
    gpiod_chip_close(chip);

    return EXIT_SUCCESS;
}
