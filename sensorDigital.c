#include <gpiod.h>
#include <stdio.h>
#include  <unistd.h>  // Para usleep

#define CHIP_NAME "/dev/gpiochip4"
#define PIN_LED  0
#define PIN_BUTTON 16
int main(void) {

	// 1. Abrir el chip GPIO
	struct gpiod_chip *chip =gpiod_chip_open(CHIP_NAME);
	struct gpiod_line *line_led;
    struct gpiod_line *line_button;

	if(!chip){
		perror("Error al abrir el chip gpiod");
		return 1;
	}
	
	// 1. Configurar PIN 17 como SALIDA
    struct gpiod_line_settings *settings_out = gpiod_line_settings_new();
    gpiod_line_settings_set_direction(settings_out, GPIOD_LINE_DIRECTION_OUTPUT);
    
    struct gpiod_line_config *line_cfg_out = gpiod_line_config_new();
    unsigned int offsets_out[] = {PIN_LED,1};
    gpiod_line_config_add_line_settings(line_cfg_out, offsets_out, 2, settings_out);

    struct gpiod_request_config *req_cfg_out = gpiod_request_config_new();
    gpiod_request_config_set_consumer(req_cfg_out, "led");

    struct gpiod_line_request *request_out = gpiod_chip_request_lines(chip, req_cfg_out, line_cfg_out);

    // 2. Configurar PIN 16 como ENTRADA
    struct gpiod_line_settings *settings_in = gpiod_line_settings_new();
    gpiod_line_settings_set_direction(settings_in, GPIOD_LINE_DIRECTION_INPUT);

    struct gpiod_line_config *line_cfg_in = gpiod_line_config_new();
    unsigned int offsets_in[] = {PIN_BUTTON};
    gpiod_line_config_add_line_settings(line_cfg_in, offsets_in, 1, settings_in);

    struct gpiod_request_config *req_cfg_in = gpiod_request_config_new();
    gpiod_request_config_set_consumer(req_cfg_in, "boton");

    struct gpiod_line_request *request_in = gpiod_chip_request_lines(chip, req_cfg_in, line_cfg_in);
	while(1){
		enum gpiod_line_value valor =gpiod_line_request_get_value(request_in,PIN_BUTTON);
		printf("El valor del GPIO %d es: %d\n", PIN_BUTTON, valor);
		enum gpiod_line_value valores[]= {valor, valor};
		gpiod_line_request_set_values(request_out,valores);
		usleep(100000);

	}
	// Liberar recursos
    gpiod_line_request_release(request_out);
    gpiod_line_request_release(request_in);
    gpiod_chip_close(chip);
}
