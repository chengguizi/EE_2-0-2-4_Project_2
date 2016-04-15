/*****************************************************************************
 *   A demo example using several of the peripherals on the base board
 *
 *   Copyright(C) 2011, EE2024
 *   All rights reserved.
 *
 ******************************************************************************/
#include <stdio.h>
#include <string.h>

#include "lpc17xx_pinsel.h"
#include "lpc17xx_gpio.h"
#include "lpc17xx_i2c.h"
#include "lpc17xx_ssp.h"
#include "lpc17xx_timer.h"
#include "lpc17xx_uart.h"

#include "joystick.h"
#include "pca9532.h"
#include "acc.h"
#include "oled.h"
#include "rgb.h"
#include "light.h"
#include "temp.h"

#include "led7seg.h"
#include "HAL.h"
#include "main.h"

volatile uint8_t master_mode = MODE_NONE; // Updated by Systick_Handler
uint8_t prev_mode = MODE_NONE;
uint8_t active_mode = ACTIVE_PS;
static int8_t bat = 0;

static volatile uint8_t fun_mode = 0; // Updated by Systick_Handler
static uint8_t prev_fun_mode = 0;

static uint8_t msg[200] = { };
static uint8_t oled_string[17] = { }; //#define OLED_DISPLAY_WIDTH  96, maximum 16 character
static uint8_t uartBuffer[UART_RX_BUFFER_SIZE + 1];

/*******************************************************************
 *
 * Sensor Data Storage
 *
 ******************************************************************/
volatile SensorStatus sensors = { ENABLE, ENABLE, ENABLE, DISABLE };
SensorStatus prev_sensor;
volatile uint8_t SCAN_FLAG = 0;

static volatile int32_t temperature = -300;
static volatile int32_t prev_temperature = -300; // simulate second temp sensor

static uint32_t lux = 0;

static int8_t x = 0;
static int8_t y = 0;
static int8_t z = 0;
static int32_t xoff = 0;
static int32_t yoff = 0;
static int32_t zoff = 0;

static volatile uint8_t do_update = 0;

static uint8_t CommStatus = 0;

/************************************************************
 *
 * System Clock Interrupt: Switch Deouncing and Mode Switch / 200Hz
 *
 ************************************************************/

volatile uint32_t msTicks = 0; // counter for 1ms SysTicks

void SysTick_Handler(void) { // CHANGE TO 5MS ROUTINE

	static uint8_t debounce_tick = 0;
		static uint8_t blocking = 0;

		msTicks = msTicks + 5;

		if (getSW4()) // switch is de-pressed
		{
			if (debounce_tick >= 2 && !blocking) // Condition that satisfy short press
					{
				switch (master_mode) {
				case MODE_NONE:
					master_mode = MODE_CAT;
					break;
				case MODE_CAT:
					master_mode = MODE_ACTIVE;
					break;
				case MODE_ACTIVE:
					master_mode = MODE_CAT;
					break;
				}
			}

			debounce_tick = 0;
			blocking = 0;
			return;
		}

		if (blocking) // active low
			return;

		if (debounce_tick++ >= 200) {
			fun_mode = !fun_mode;
			blocking = 1;
		}

		return;
}

void set_active_mod() // assume lux is already be updated
{

	if (lux < 50) {
		light_setHiThreshold(49);
		light_setLoThreshold(0);
		active_mode = ACTIVE_PS;
		oled_putString(57, FIRST_LINE, "PS", OLED_COLOR_BLACK,
				OLED_COLOR_WHITE);
		oled_putString(40, SECOND_LINE, " PS   ", OLED_COLOR_WHITE,
				OLED_COLOR_BLACK);
		oled_putString(34, THIRD_LINE, "  PS   PS  ", OLED_COLOR_WHITE,
				OLED_COLOR_BLACK);
		oled_putString(2, FIFTH_LINE, "  PS   PS   PS  ", OLED_COLOR_WHITE,
				OLED_COLOR_BLACK);

		//offUART(); // Disable UART in PS Mode
		offSensors(1); // Sensors Idle, preserve light sensor
		return;
	}

	if (active_mode == ACTIVE_PS)
		onSensors(1);

	if (lux > 900) {
		light_setHiThreshold(1000);
		light_setLoThreshold(901);
		do_update = 1;
		active_mode = ACTIVE_FP;
		oled_putString(57, FIRST_LINE, "FP", OLED_COLOR_BLACK,
				OLED_COLOR_WHITE);

		// Enable UART in FP Mode
		//onUART(); // Sould only enable during checking

	} else {
		light_setHiThreshold(900);
		light_setLoThreshold(50);
		active_mode = ACTIVE_NO;
		do_update = 1;
		oled_putString(57, FIRST_LINE, "NO", OLED_COLOR_BLACK,
				OLED_COLOR_WHITE);

		// Disable UART in FP Mode
		//offUART(); // no need
	}

	//oled_putString (3,FIFTH_LINE, "                ", OLED_COLOR_WHITE, OLED_COLOR_BLACK );

}

void EINT3_IRQHandler(void) // Triggered roughly every 3 ms
{
	if (LPC_GPIOINT ->IO0IntStatR >> PIN_TEMP_INT & 0x0001) // Interrupt PIN for Temperature Sensor
			{
		LPC_GPIOINT ->IO0IntClr = 1 << PIN_TEMP_INT; // Port0.2, PIO1_5, temp sensor
		int32_t temp = temp_service();
		if (temp > -300) {
			prev_temperature = temperature;
			temperature = temp;
		}

	}

	if (LPC_GPIOINT ->IO2IntStatF >> 10 & 0x0001) // Interrupt PIN for SW3
			{
		LPC_GPIOINT ->IO2IntEnF &= ~(0x0001 << 10); // Port2.10, SW3, disable
		LPC_GPIOINT ->IO2IntClr = 1 << 10;
		SCAN_FLAG = 1;

	}

	NVIC_ClearPendingIRQ(EINT3_IRQn);
}

void UART3_IRQHandler(void) {
	static uint8_t msg[200] = { };

	static uint8_t pointer = 0;
	uint8_t char_buffer;
	uint32_t intsrc = LPC_UART3 ->IIR & UART_IIR_INTID_MASK;

	// Receive Data Available or Character time-out
	if (intsrc == UART_IIR_INTID_RDA || intsrc == UART_IIR_INTID_CTI ) // depends on trigger level, FCR[7:6]
	{
		//LPC_UART3->LSR;
		//UART_SendData(LPC_UART3,UART_ReceiveData(LPC_UART3));
		char_buffer = UART_ReceiveData(LPC_UART3 );

		if (char_buffer == '\r' || pointer == 20) {

			uartBuffer[pointer] = 0;
			pointer = 0;
			if (strcmp(uartBuffer, "status") == 0) {

				push_String("\r\n====   I-WATCH Status Telemetry   =====\r\n");

				strcpy(msg,"Light: ");
				if(sensors.LIGHT == ENABLE)
					strcat(msg, "Active | ");
				else
					strcat(msg, "Idle | ");

				strcat(msg, "Temp: ");
				if(sensors.TEMP == ENABLE)
					strcat(msg, "Active | ");
						else
					strcat(msg, "Idle | ");

				strcat(msg, "Accel: ");
				if(sensors.ACCEL == ENABLE)
					strcat(msg, "Active\r\n");
						else
					strcat(msg, "Idle\r\n");
				push_String(msg);

				sprintf(msg, "Battery: %d%%\r\n", (int32_t) bat*100/15);
				push_String(msg);

			}

		} else if(char_buffer == '\b')
		{
			pointer--;
			if(pointer<0)
				pointer=0;
		}
		else
			uartBuffer[pointer++] = char_buffer;

	}

}

int8_t uartService() // IN ACTIVE MODE, ASSSUME ALWAYS ENABLED
{

	if (hal_haveBuffer()) {
		uint8_t temp[UART_TX_BUFFER_SIZE + 1];
		pop_String(temp);
		UART_Send(LPC_UART3, temp, strlen(temp), BLOCKING);
		return 1;
	}
	return 0;

}

//uint32_t getMsTick(void)
//{
//	return msTicks;
//}

/******************************
 *
 * All indicator off
 * All Sensors & UART off
 *
 ******************************/
static void init_CAT() {
	oled_off(); // display OFF
	bat = 0;		// reset battery level
	led7seg_setChar(0xFF, 1, 0); // Raw mode, clear the Display
	oled_clearScreen(OLED_COLOR_BLACK); // Clear OLED DISPLAY
	pca9532_setLeds(0x0000, 0xFFFF); // Turn off all row LEDs
	rgb_setLeds(0);			// off Multicolor LED
	active_mode = ACTIVE_PS; // Make Sure, light sensor is turned on properly

	offSensors(0);
	offUART(); 	// off Scan Mode
}

/***************************
 *  This should only be run ONCE
 ****************************/
static inline void onetime_init() {
	init_i2c();
	init_ssp();
	init_GPIO();

	pca9532_init(); // LED array
	joystick_init();
	oled_init();
	rgb_init();
	led7seg_init();
	light_init();

	interrupt_init();

	init_CAT();
}

static void welcome_screen() {
	oled_on();
	oled_gpu_scroll(); // Create Animation
	if (Timer_SW4(200))
		return;
	oled_putString(26, 3, "I-WATCH", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
	if (Timer_SW4(200))
		return;
	oled_putString(6, 15, "Electronic Tag", OLED_COLOR_WHITE, OLED_COLOR_BLACK);
	if (Timer_SW4(200))
		return;
	oled_putString(9, 27, "Configuration", OLED_COLOR_WHITE, OLED_COLOR_BLACK);
	if (Timer_SW4(200))
		return;
	oled_putString(16, 39, "and Testing", OLED_COLOR_WHITE, OLED_COLOR_BLACK);
	if (Timer_SW4(200))
		return;
	oled_putString(38, 51, "Mode", OLED_COLOR_WHITE, OLED_COLOR_BLACK);

	oled_command(0x2E); //stop scrolling
}

/******************************************************
 *
 * Read Accel Offset, OFF Sensors and UART
 * Turn On all the sensors at last
 *
 *******************************************************/
static void mode_CAT(int32_t* xoff, int32_t* yoff, int32_t* zoff) {
	uint8_t i;

	init_CAT(); // Turn off Everything, including UART

	acc_init();

	welcome_screen();

	acc_read(&x, &y, &z);
	*xoff = 0 - x;
	*yoff = 0 - y;
	*zoff = 64 - z;
	acc_off();

	if (Timer_SW4(1))
		return;

	for (i = 0; i < 4; i++) // 4 second of blink
			{
		rgb_setLeds(RGB_BLUE);
		if (Timer_SW4(500))
			return;
		rgb_setLeds(0);
		if (Timer_SW4(500))
			return;
	}

	for (i = 0; i < 4; i++) {
		rgb_setLeds(RGB_RED);
		if (Timer_SW4(500))
			return;
		rgb_setLeds(0);
		if (Timer_SW4(500))
			return;
	}

	rgb_setLeds(RGB_RED); // Turn on forever

	for (i = 0; i < 6; i++) {
		led7seg_setChar('0' + i, 0, fun_mode);
		if (Timer_SW4(1000))
			return;
	}

	for (bat = 0; bat < 16; bat++) {
		pca9532_setLeds(1 << bat, 0x0000);
		if (Timer_SW4(250))
			return;
	}
	bat--;

	oled_off(); // display OFF
	oled_clearScreen(OLED_COLOR_BLACK);
	oled_putString(03, FIRST_LINE, "     CAT   ", OLED_COLOR_BLACK,
			OLED_COLOR_WHITE);
	oled_putString(03, SECOND_LINE, "Light:", OLED_COLOR_WHITE,
			OLED_COLOR_BLACK);
	oled_putString(03, THIRD_LINE, "Temp:", OLED_COLOR_WHITE, OLED_COLOR_BLACK);
	oled_putString(03, FOURTH_LINE, "Accel: (x,y,z)", OLED_COLOR_WHITE,
			OLED_COLOR_BLACK);
	oled_rect(2, 2, 8 * 11, 10, OLED_COLOR_WHITE);
	oled_on();

	//onSensors(0);
	return;
}

static void mode_ACTIVE() {
	onSensors(0);
	onUART();
	oled_command(0x2E); //stop scrolling

	//if(bat>=0)
	//	pca9532_setLeds (1<<bat,0x0000); // correct the difference from CAT mode

	oled_off(); // display OFF
	oled_clearScreen(OLED_COLOR_BLACK);
	oled_putString(03, FIRST_LINE, "ACTIVE - ", OLED_COLOR_BLACK,
			OLED_COLOR_WHITE);
	oled_putString(03, SECOND_LINE, "Light:", OLED_COLOR_WHITE,
			OLED_COLOR_BLACK);
	oled_putString(03, THIRD_LINE, "Temp:", OLED_COLOR_WHITE, OLED_COLOR_BLACK);
	oled_putString(03, FOURTH_LINE, "Accel: (x,y,z)", OLED_COLOR_WHITE,
			OLED_COLOR_BLACK);
	oled_rect(2, 2, 8 * 11, 10, OLED_COLOR_WHITE);
	oled_on();

	do_update = 1;
}

inline void Update_FunMode() {

	if (fun_mode != prev_fun_mode) {
		prev_fun_mode = fun_mode;

		if (!fun_mode)
			oled_rect(0, 0, OLED_DISPLAY_WIDTH - 1, OLED_DISPLAY_HEIGHT - 1,
					OLED_COLOR_BLACK);

	}

	if (fun_mode) {

		if (fun_mode == 1) {
			oled_rect(0, 0, OLED_DISPLAY_WIDTH - 1, OLED_DISPLAY_HEIGHT - 1,
					OLED_COLOR_WHITE);
		} else if (fun_mode == 4) {
			oled_rect(0, 0, OLED_DISPLAY_WIDTH - 1, OLED_DISPLAY_HEIGHT - 1,
					OLED_COLOR_BLACK);
		}
		fun_mode = (fun_mode == 6) ? 1 : (fun_mode + 1);
	}
}

inline void Update_Screen() {
	sprintf(oled_string, "%3u ", lux); // %6d (print as a decimal integer with a width of at least 6 wide)
									   // %3.2f	(print as a floating point at least 3 wide and a precision of 2)
	oled_putString(40, SECOND_LINE, oled_string, OLED_COLOR_WHITE,
			OLED_COLOR_BLACK);

	sprintf(oled_string, "%3.1f ", prev_temperature / 10.0);
	oled_putString(39, THIRD_LINE, oled_string, OLED_COLOR_WHITE,
			OLED_COLOR_BLACK);
	sprintf(oled_string, "%3.1f ", temperature / 10.0);
	oled_putString(69, THIRD_LINE, oled_string, OLED_COLOR_WHITE,
			OLED_COLOR_BLACK);

	sprintf(oled_string, "%4d", (int32_t) x);
	oled_putString(3, FIFTH_LINE, oled_string, OLED_COLOR_WHITE,
			OLED_COLOR_BLACK);

	sprintf(oled_string, "%4d", (int32_t) y);
	oled_putString(36, FIFTH_LINE, oled_string, OLED_COLOR_WHITE,
			OLED_COLOR_BLACK);

	sprintf(oled_string, "%4d", (int32_t) z);
	oled_putString(66, FIFTH_LINE, oled_string, OLED_COLOR_WHITE,
			OLED_COLOR_BLACK);
}

void Update_Battery(uint32_t * prev_bat_sampling) {
	int8_t prev_bat = bat;
	if (msTicks - *prev_bat_sampling >= INDICATOR_TIME_UNIT) {
		if (active_mode == ACTIVE_PS && bat > 0) {
			pca9532_setLeds(0x0000, 1 << bat--);
		} else if (active_mode == ACTIVE_FP && bat < 15) {
			pca9532_setLeds(1 << ++bat, 0x0000);
		}


		Update_FunMode();
		*prev_bat_sampling = msTicks;

		if (prev_bat == 15 && bat == 14) {
			sprintf(msg, "Satellite Communication Link Suspended.\r\n");
			push_String(msg);
			CommStatus = 0;

		} else if (prev_bat == 14 && bat == 15) {

			sprintf(msg, "Satellite Communication Link Established.\r\n");
			push_String(msg);
			do_update = 1;
			CommStatus = 1;
		}

	}
}

void Update_Light_Mode(uint32_t *prev_sensor_sampling) {
	static uint32_t sampling_rate = SENSOR_SAMPLING_TIME;
	//static uint32_t SCAN_timeout = 0;

	sampling_rate =
			(fun_mode && active_mode == ACTIVE_FP) ?
					(SENSOR_SAMPLING_TIME / 6) : SENSOR_SAMPLING_TIME;

	lux = light_read();
	do_update |= GetLightInterrupt();

	if (do_update) {
		set_active_mod();
		light_clearIrqStatus();
		if (active_mode == ACTIVE_PS)
			do_update = 0;
	}

	if (SCAN_FLAG) {
		onSensors(1);
		do_update = 1;
	}

	if (do_update
			|| active_mode != ACTIVE_PS
					&& (msTicks - *prev_sensor_sampling >= sampling_rate)) {

		if (fun_mode)
			rgb_setLeds(RGB_BLUE | RGB_RED);

		acc_read(&x, &y, &z);
		x = x + xoff;
		y = y + yoff;
		z = z + zoff;

		Update_Screen();

		/// UART
		if (SCAN_FLAG || CommStatus) {
			sprintf(msg, "L%d_TA_%3.1f_TB%3.1f_AX%d_AY%d_AZ%d\n\r", lux,
					prev_temperature / 10.0, temperature / 10.0, x, y, z);
			//UART_Send(LPC_UART3, msg, strlen(msg), BLOCKING);
			push_String(msg);

		}

		rgb_setLeds(RGB_RED);

		if (SCAN_FLAG == 1) {
			if (active_mode == ACTIVE_PS)
				offSensors(1);

			LPC_GPIOINT ->IO2IntEnF |= 1 << 10; // Port2.10, SW3, enable
			SCAN_FLAG = 0;
		} else
			*prev_sensor_sampling = msTicks;

		do_update = 0;
	}

}

void Update_Light_Accel() {
	lux = light_read();
	acc_read(&x, &y, &z);
	x = x + xoff;
	y = y + yoff;
	z = z + zoff;
}

//void SwitchCheck() {
//
//}

////////////////////////////////////////////
//////// MAIN FUNCTION /////////////////////
////////////////////////////////////////////

int main(void) {

	uint32_t prev_bat_sampling = 0;
	uint32_t prev_sensor_sampling = 0;

	/* ---- Speaker ------> */

	GPIO_SetDir(2, 1 << 0, 1);
	GPIO_SetDir(2, 1 << 1, 1);

	GPIO_SetDir(0, 1 << 27, 1);
	GPIO_SetDir(0, 1 << 28, 1);
	GPIO_SetDir(2, 1 << 13, 1);
	GPIO_SetDir(0, 1 << 26, 1);

	GPIO_ClearValue(0, 1 << 27); //LM4811-clk
	GPIO_ClearValue(0, 1 << 28); //LM4811-up/dn
	GPIO_ClearValue(2, 1 << 13); //LM4811-shutdn

	/* <---- Speaker ------ */

	onetime_init(); // one-time Initialization + CAT mode step 1

	while (1) {
		//SwitchCheck();

		switch (master_mode) {
		case MODE_NONE:
			break;

		case MODE_CAT:

			// ONE-Time Set-up for CAT Mode
			if (prev_mode != MODE_CAT) {
				prev_mode = MODE_CAT; // Must update first
				mode_CAT(&xoff, &yoff, &zoff); //BLOCKING MODULE
				onSensors(0); // Turn ON all sensors
				break;
			}
			Update_Light_Accel();
			Update_Screen();
			break;
		case MODE_ACTIVE:

			// ONE-TIME Set-up for ACTIVE MODE
			if (prev_mode != MODE_ACTIVE) {
				prev_mode = MODE_ACTIVE;
				mode_ACTIVE(); // trigger the first sensor update
				prev_sensor_sampling = prev_bat_sampling = msTicks;
				break;
			}

			Update_Battery(&prev_bat_sampling);

			Update_Light_Mode(&prev_sensor_sampling);

			uartService();
			break;
		}
	}
}

void check_failed(uint8_t *file, uint32_t line) {
	/* User can add his own implementation to report the file name and line number,
	 ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
	printf("Wrong parameters value: file %s on line %d\r\n", file, line);
	/* Infinite loop */
	while (1);
}

