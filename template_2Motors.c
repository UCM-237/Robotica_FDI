    #include <gpiod.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>

#define CHIP_NAME "/dev/gpiochip0"

// Pines Motor 1
#define M1_IN1 14
#define M1_IN2 16

// Pines Motor 2 (CAMBIAR SEGÚN TU CONEXIÓN)
#define M2_IN3 20
#define M2_IN4 21

// Rutas Sysfs para PWM Hardware
#define PWM_BASE_PATH "/sys/class/pwm/pwmchip0"
#define PWM0_PATH PWM_BASE_PATH "/pwm0"
#define PWM1_PATH PWM_BASE_PATH "/pwm1"

typedef enum { MOTOR_FORWARD, MOTOR_BACKWARD, MOTOR_STOP } MotorDirection;

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
    // Exportar canal 0 (PWM0) y canal 1 (PWM1)
    sysfs_write(PWM_BASE_PATH "/export", "0");
    sysfs_write(PWM_BASE_PATH "/export", "1");
    usleep(100000); // Esperar a que el Kernel cree los directorios pwm0 y pwm1

    // Configurar Motor 1 (PWM0)
    sysfs_write(PWM0_PATH "/period", "20000000"); // 50 Hz
    sysfs_write(PWM0_PATH "/duty_cycle", "0");
    sysfs_write(PWM0_PATH "/enable", "1");

    // Configurar Motor 2 (PWM1)
    sysfs_write(PWM1_PATH "/period", "20000000"); // 50 Hz
    sysfs_write(PWM1_PATH "/duty_cycle", "0");
    sysfs_write(PWM1_PATH "/enable", "1");
}

// Control de velocidad generalizado (motor_num: 1 para Motor 1, 2 para Motor 2)
void set_motor_speed(int motor_num, int duty_percent) {
    if (duty_percent < 0) duty_percent = 0;
    if (duty_percent > 100) duty_percent = 100;

    long duty_ns = (20000000L * duty_percent) / 100;
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%ld", duty_ns);

    if (motor_num == 1) {
        sysfs_write(PWM0_PATH "/duty_cycle", buffer);
    } else if (motor_num == 2) {
        sysfs_write(PWM1_PATH "/duty_cycle", buffer);
    }
}

// --------------------------------------------------------------------------
// Control de Dirección de los Motores
// --------------------------------------------------------------------------
// Cambia la dirección actualizando únicamente los 2 pines del motor indicado
void set_motor_direction(struct gpiod_line_request *motor_req, int motor_num, MotorDirection dir) {
    enum gpiod_line_value values[2];

    if (dir == MOTOR_FORWARD) {
        values[0] = GPIOD_LINE_VALUE_ACTIVE;
        values[1] = GPIOD_LINE_VALUE_INACTIVE;
    } else if (dir == MOTOR_BACKWARD) {
        values[0] = GPIOD_LINE_VALUE_INACTIVE;
        values[1] = GPIOD_LINE_VALUE_ACTIVE;
    } else {
        values[0] = GPIOD_LINE_VALUE_INACTIVE;
        values[1] = GPIOD_LINE_VALUE_INACTIVE;
    }

    // Actualizamos únicamente los pines del motor deseado pasando los offsets
    unsigned int offsets[2];
    if (motor_num == 1) {
        offsets[0] = M1_IN1;
        offsets[1] = M1_IN2;
    } else {
        offsets[0] = M2_IN3;
        offsets[1] = M2_IN4;
    }

    gpiod_line_request_set_values_subset(motor_req, 2, offsets, values);
}

// --------------------------------------------------------------------------
// Main
// --------------------------------------------------------------------------
int main(void) {
    // 1. Inicializar los dos canales PWM
    init_pwm();

    // 2. Configurar los 4 pines de control de dirección (M1_IN1, M1_IN2, M2_IN3, M2_IN4)
    struct gpiod_chip *chip = gpiod_chip_open(CHIP_NAME);
    if (!chip) {
        perror("Error al abrir gpiochip para motor");
        return EXIT_FAILURE;
    }

    struct gpiod_line_settings *motor_settings = gpiod_line_settings_new();
    gpiod_line_settings_set_direction(motor_settings, GPIOD_LINE_DIRECTION_OUTPUT);

    unsigned int motor_offsets[4] = {M1_IN1, M1_IN2, M2_IN3, M2_IN4};
    struct gpiod_line_config *motor_line_cfg = gpiod_line_config_new();
    gpiod_line_config_add_line_settings(motor_line_cfg, motor_offsets, 4, motor_settings);

    struct gpiod_request_config *motor_req_cfg = gpiod_request_config_new();
    gpiod_request_config_set_consumer(motor_req_cfg, "dual-motor-control");

    struct gpiod_line_request *motor_request = gpiod_chip_request_lines(chip, motor_req_cfg, motor_line_cfg);

    // 3. Secuencia de prueba de ambos motores
    // Motor 1 avanza al 50%
    set_motor_direction(motor_request, 1, MOTOR_FORWARD);
    set_motor_speed(1, 50);

    // Motor 2 retrocede al 75%
    set_motor_direction(motor_request, 2, MOTOR_BACKWARD);
    set_motor_speed(2, 75);

    sleep(3);

    // Detener ambos motores
    set_motor_speed(1, 0);
    set_motor_speed(2, 0);
    set_motor_direction(motor_request, 1, MOTOR_STOP);
    set_motor_direction(motor_request, 2, MOTOR_STOP);

    // 4. Liberar recursos
    gpiod_line_request_release(motor_request);
    gpiod_request_config_free(motor_req_cfg);
    gpiod_line_config_free(motor_line_cfg);
    gpiod_line_settings_free(motor_settings);
    gpiod_chip_close(chip);

    return EXIT_SUCCESS;
}