#include <gpiod.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>

#define CHIP_NAME "/dev/gpiochip0"

// Pines Motor
#define PIN_IN1 5
#define PIN_IN2 6

// Pines Encoder
#define PIN_ENC_A 26
#define PIN_ENC_B 21

// Ruta del sysfs para el PWM Hardware en GPIO 12 (PWM0)
#define PWM_PATH "/sys/class/pwm/pwmchip0/pwm0"

// Variable global de posición (protegida si fuera necesario, o atómica)
volatile long encoder_position = 0;
volatile bool running = true;

// Tabla de estados en cuadratura
static const int8_t QUADRATURE_TABLE[16] = {
     0,  1, -1,  0,
    -1,  0,  0,  1,
     1,  0,  0, -1,
     0, -1,  1,  0
};

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
// Hilo de lectura del Encoder en background
// --------------------------------------------------------------------------
void* encoder_thread_func(void* arg) {
    struct gpiod_chip *chip = gpiod_chip_open(CHIP_NAME);
    if (!chip) {
        perror("Error al abrir gpiochip para encoder");
        return NULL;
    }

    struct gpiod_line_settings *settings = gpiod_line_settings_new();
    gpiod_line_settings_set_direction(settings, GPIOD_LINE_DIRECTION_INPUT);
    gpiod_line_settings_set_bias(settings, GPIOD_LINE_BIAS_PULL_UP);
    gpiod_line_settings_set_edge_detection(settings, GPIOD_LINE_EDGE_BOTH);

    unsigned int offsets[2] = {PIN_ENC_A, PIN_ENC_B};
    struct gpiod_line_config *line_cfg = gpiod_line_config_new();
    gpiod_line_config_add_line_settings(line_cfg, offsets, 2, settings);

    struct gpiod_request_config *req_cfg = gpiod_request_config_new();
    gpiod_request_config_set_consumer(req_cfg, "encoder-thread");

    struct gpiod_line_request *request = gpiod_chip_request_lines(chip, req_cfg, line_cfg);
    struct gpiod_edge_event_buffer *event_buffer = gpiod_edge_event_buffer_new(16);

    enum gpiod_line_value vals[2];
    gpiod_line_request_get_values(request, vals);
    uint8_t state = (vals[0] << 1) | vals[1];

    while (running) {
        // Timeout de 100ms para revisar periódicamente la variable 'running'
        int ret = gpiod_line_request_wait_edge_events(request, 100000000);
        if (ret > 0) {
            int num_events = gpiod_line_request_read_edge_events(request, event_buffer, 16);
            if (num_events > 0) {
                gpiod_line_request_get_values(request, vals);
                uint8_t pin_a_val = (vals[0] == GPIOD_LINE_VALUE_ACTIVE) ? 1 : 0;
                uint8_t pin_b_val = (vals[1] == GPIOD_LINE_VALUE_ACTIVE) ? 1 : 0;

                uint8_t new_pins = (pin_a_val << 1) | pin_b_val;
                state = ((state & 0x03) << 2) | new_pins;

                int delta = QUADRATURE_TABLE[state & 0x0F];
                if (delta != 0) {
                    encoder_position += delta;
                }
            }
        }
    }

    // Limpieza
    gpiod_edge_event_buffer_free(event_buffer);
    gpiod_line_request_release(request);
    gpiod_request_config_free(req_cfg);
    gpiod_line_config_free(line_cfg);
    gpiod_line_settings_free(settings);
    gpiod_chip_close(chip);
    return NULL;
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

    // 3. Lanzar hilo secundario para leer el Encoder
    pthread_t enc_thread;
    pthread_create(&enc_thread, NULL, encoder_thread_func, NULL);

    // 4. Secuencia de prueba del motor
    printf("--- INICIANDO SECUENCIA DEL MOTOR ---\n");

    // Giro en sentido horario al 60% de potencia
    printf("Moviendo ADELANTE al 60%%...\n");
    set_motor_direction(motor_request, MOTOR_FORWARD);
    set_motor_speed(60);

    for (int i = 0; i < 30; i++) {
        printf("Posición Encoder: %ld\n", encoder_position);
        usleep(100000); // 100ms
    }

    // Parada
    printf("PARANDO...\n");
    set_motor_speed(0);
    set_motor_direction(motor_request, MOTOR_STOP);
    sleep(1);

    // Giro en sentido antihorario al 80% de potencia
    printf("Moviendo ATRÁS al 80%%...\n");
    set_motor_direction(motor_request, MOTOR_BACKWARD);
    set_motor_speed(80);

    for (int i = 0; i < 30; i++) {
        printf("Posición Encoder: %ld\n", encoder_position);
        usleep(100000);
    }

    // Parada Final
    printf("FIN. Parando motor...\n");
    set_motor_speed(0);
    set_motor_direction(motor_request, MOTOR_STOP);

    // Apagar hilo y limpiar recursos
    running = false;
    pthread_join(enc_thread, NULL);

    gpiod_line_request_release(motor_request);
    gpiod_request_config_free(motor_req_cfg);
    gpiod_line_config_free(motor_line_cfg);
    gpiod_line_settings_free(motor_settings);
    gpiod_chip_close(chip);

    return EXIT_SUCCESS;
}
