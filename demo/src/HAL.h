#ifndef __INT_H
#define __INT_H

#include "LPC17xx.h"
#include "lpc_types.h"
#include "core_cm3.h"
#include "lpc17xx_pinsel.h"
#include "lpc17xx_gpio.h"
#include "lpc17xx_i2c.h"
#include "lpc17xx_ssp.h"
#include "lpc17xx_timer.h"
#include "lpc17xx_uart.h"


#define PORT_LIGHT_INT 	2
#define PORT_TEMP_INT 	0
#define PIN_LIGHT_INT	5
#define PIN_TEMP_INT 	2


void init_ssp(void);
void init_i2c(void);
void init_GPIO(void);
void init_uart();



#endif /* end __INT_H */
