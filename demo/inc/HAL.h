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

#include "acc.h"
#include "light.h"
#include "temp.h"
#include "main.h"


#define PORT_TEMP_INT 	0
#define PIN_TEMP_INT 	2

#define UART_TX_BUFFER_SIZE 80

extern volatile uint8_t master_mode;
extern uint8_t prev_mode;

void init_ssp(void);
void init_i2c(void);
void init_GPIO(void);
void init_uart();

void interrupt_init();

void push_String(uint8_t *string);
void pop_String(uint8_t *bufferout);

uint8_t hal_haveBuffer();


static inline uint8_t GetLightInterrupt()
{
	return (~GPIO_ReadValue(PORT_LIGHT_INT)>> PIN_LIGHT_INT) & 0x00000001; // Manual light int read
}

static inline uint8_t getSW4() // active low
{
	return (GPIO_ReadValue(1) >> 31 & 0x01);
}

static inline void offUART() // OFF the power supply of UART
{
	if(sensors.TRANS == DISABLE)
		return;

	//UART_DeInit(LPC_UART3);
	UART_IntConfig(LPC_UART3,UART_INTCFG_RBR,DISABLE);
	NVIC_DisableIRQ(UART3_IRQn);
	sensors.TRANS = DISABLE;
}

/****************************************
 *
 * Remove J7 PIN B, REMAIN A and INH
 *
 ****************************************/
static inline void onUART()
{
	if(sensors.TRANS == ENABLE)
		return;

	init_uart();
	UART_IntConfig(LPC_UART3,UART_INTCFG_RBR,ENABLE);
	NVIC_EnableIRQ(UART3_IRQn);
	sensors.TRANS = ENABLE;
}

static inline void offSensors(uint8_t preserve_light)
{
	//NVIC_DisableIRQ(EINT3_IRQn); // Off the Interrupt for Tempuerature Sensor

	LPC_GPIOINT->IO0IntClr |= 1<<PIN_TEMP_INT; //Port0.2, temp sensor

	sensors.TEMP = DISABLE;

	if (sensors.ACCEL == ENABLE)
		acc_off();

	if(!preserve_light && sensors.LIGHT == ENABLE)
		light_shutdown();
}

static inline void onSensors(uint8_t preserve_light)
{
	//NVIC_EnableIRQ(EINT3_IRQn); // Tempurature Sensor
	LPC_GPIOINT->IO0IntEnR |= 1<<PIN_TEMP_INT; //Port0.2, temp sensor
	sensors.TEMP = ENABLE;

	if (sensors.ACCEL != ENABLE)
		acc_init();

	if(!preserve_light && sensors.LIGHT != ENABLE)
		light_enable();
}


uint8_t Timer_with_StateCheck (uint32_t time, volatile uint8_t *state, uint8_t *prev_state);

static inline uint8_t Timer_SW4(uint32_t time)
{
	return Timer_with_StateCheck(time, &master_mode, &prev_mode);
}



#endif /* end __INT_H */
