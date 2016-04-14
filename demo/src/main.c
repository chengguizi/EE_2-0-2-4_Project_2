/*****************************************************************************
 *   A demo example using several of the peripherals on the base board
 *
 *   Copyright(C) 2011, EE2024
 *   All rights reserved.
 *
 ******************************************************************************/
#include <stdio.h>

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

#define MODE_CAT 		1
#define MODE_ACTIVE 	2
#define MODE_NONE		0
#define ACTIVE_PS		0
#define ACTIVE_NO		1
#define ACTIVE_FP		2

#define SENSOR_SAMPLING_TIME 4000
#define INDICATOR_TIME_UNIT 250

static volatile uint8_t master_mode = MODE_NONE; // Updated by Systick_Handler
static uint8_t prev_mode = MODE_NONE;
static uint8_t active_mode = ACTIVE_NO;
static int8_t bat = -1;



static volatile uint8_t fun_mode = 0;
static volatile uint8_t prev_fun_mode = 0;
static uint8_t barPos = 2;
static uint8_t* msg = NULL;
volatile int32_t temperature = -300;
uint32_t lux = 0;
int8_t x = 0;
int8_t y = 0;
int8_t z = 0;

static volatile uint8_t do_update = 0;

volatile uint32_t msTicks = 0 ; // counter for 1ms SysTicks


inline uint8_t getSW4() // active low
{
	return (GPIO_ReadValue(1) >> 31 & 0x01);
}

uint8_t Timer_with_StateCheck (uint32_t time, volatile uint8_t *state, volatile uint8_t *prev_state)
{
	uint32_t time0 = msTicks;

	while (1)
	{
		if (*state != *prev_state)
			return 1;
		if (msTicks - time0 == time)
			return 0;
	}
	return 0;
}

uint8_t Timer_SW4(uint32_t time)
{
	return Timer_with_StateCheck(time, &master_mode, &prev_mode);
}

void SysTick_Handler(void) { // CHANGE TO 5MS ROUTINE

	static uint8_t debounce_tick = 0;
	static uint8_t blocking = 0;

	msTicks = msTicks + 5;

	if (getSW4()) // switch is de-pressed
	{
		if(debounce_tick >= 2 && !blocking) // Condition that satisfy short press
		{
			switch (master_mode)
				{
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

	debounce_tick++;

	if (debounce_tick >= 200)
	{
		fun_mode = !fun_mode;
		blocking = 1;
	}

	return;

}

void set_active_mod (uint32_t lux)
{
			if (lux < 50)
			{
				light_setHiThreshold(49);
				light_setLoThreshold(0);
				active_mode = ACTIVE_PS;
				//printf("I'm in Active_PS=%d!\n",lux);
				oled_putString (0,0, "ACTIVE - PS", OLED_COLOR_WHITE, OLED_COLOR_BLACK );

				oled_putString (40,10, "PS    ", OLED_COLOR_WHITE, OLED_COLOR_BLACK );
				oled_putString (40,20, "PS    ", OLED_COLOR_WHITE, OLED_COLOR_BLACK );

			}
			else if (lux > 900)
			{
				light_setHiThreshold(1000);
				light_setLoThreshold(901);
				active_mode = ACTIVE_FP;
				//printf("I'm in Active_FP=%d!\n",lux);
				oled_putString (0,0, "ACTIVE - FP", OLED_COLOR_WHITE, OLED_COLOR_BLACK );
			}
			else
			{
				light_setHiThreshold(900);
				light_setLoThreshold(50);
				active_mode = ACTIVE_NO;
				//printf("I'm in Active_NO=%d!\n",lux);
				oled_putString (0,0, "ACTIVE - NO", OLED_COLOR_WHITE, OLED_COLOR_BLACK );
			}
}

void EINT3_IRQHandler(void)
{
	if (LPC_GPIOINT->IO0IntStatR >> PIN_TEMP_INT & 0x01) // Interrupt PIN for Temperature Sensor
	{
		LPC_GPIOINT->IO0IntClr = 1 << PIN_TEMP_INT; // Port0.2, PIO1_5, temp sensor
		int32_t temp = temp_service();
		if (temp> -300)
			temperature = temp;
	}
	NVIC_ClearPendingIRQ(EINT3_IRQn);
}

void UART3_IRQHandler(void)
{
	uint32_t intsrc = LPC_UART3->IIR & UART_IIR_INTID_MASK;

	if (intsrc == UART_IIR_INTID_RDA) // depends on trigger level, FCR[7:6]
	{
		//LPC_UART3->LSR;
		UART_SendData(LPC_UART3,UART_ReceiveData(LPC_UART3));
	}



}

uint32_t getMsTick(void)
{
	return msTicks;
}




static void init_CAT()
{
	bat = -1;
    led7seg_setChar (0xFF,1); // Raw mode, clear the Display
	oled_clearScreen(OLED_COLOR_BLACK); // OFF THE DISPLAY
	pca9532_setLeds (0x0000,0xFFFF); // Turn off all LEDs
	rgb_setLeds (0);
	// SCAN TURN OFF

}

// QUESTION - Why use "static"
static void init_pre_CAT()
{
	init_CAT();
}

static void welcome_screen()
{
	oled_putString (25,1, "I-WATCH", OLED_COLOR_WHITE, OLED_COLOR_BLACK );
	oled_putString (3,13, "Electronic Tag", OLED_COLOR_WHITE, OLED_COLOR_BLACK );
	oled_putString (7,26, "Configuration", OLED_COLOR_WHITE, OLED_COLOR_BLACK );
	oled_putString (13,39, "and Testing", OLED_COLOR_WHITE, OLED_COLOR_BLACK );
	oled_putString (36,52, "Mode", OLED_COLOR_WHITE, OLED_COLOR_BLACK );
}

static void mode_CAT()
{
	uint8_t i;

	fun_mode = 0;
	NVIC_DisableIRQ(EINT3_IRQn);
	printf("====YOU JUST ENTER CAT MODE====\n");
	init_CAT();
	oled_off (); // display OFF
	oled_on();
	oled_gpu_scroll();

	welcome_screen();

	/*for (i=0;i<=0x40;i++)
	{
		oled_VRoll(0);
		if (Timer_SW4(10)) return;
	}*/

	//oled_gpu_Hscroll();


	Timer_SW4(1500);
	oled_command(0x2E); //stop scrolling

	for (i=0;i<4;i++) // 4 second of blink
	{
		rgb_setLeds (RGB_BLUE);
		if (Timer_SW4(500)) return;
		rgb_setLeds (0);
		if (Timer_SW4(500)) return;
	}

	for (i=0;i<4;i++)
	{
		rgb_setLeds (RGB_RED);
		if (Timer_SW4(500)) return;
		rgb_setLeds (0);
		if (Timer_SW4(500)) return;
	}
	if (Timer_SW4(4000)) return;

	for(i=0;i<6;i++)
	{
		led7seg_setChar ('0'+i,0);
		if (Timer_SW4(1000)) return;
	}

	for (bat=0;bat<16;bat++)
	{
		pca9532_setLeds (1<<bat,0x0000);
		if (Timer_SW4(250)) return;
	}

	return;

}

static void mode_ACTIVE ()
{
	if(bat>=0)
		pca9532_setLeds (1<<bat,0x0000);

	oled_clearScreen(OLED_COLOR_BLACK);
    //light_setHiThreshold(1000);
    //light_setLoThreshold(1000);

	set_active_mod( light_read() );

	oled_putString (00,10, "Light:", OLED_COLOR_WHITE, OLED_COLOR_BLACK );
	oled_putString (00,20, "Temp:", OLED_COLOR_WHITE, OLED_COLOR_BLACK );

    light_clearIrqStatus();
    NVIC_EnableIRQ(EINT3_IRQn);


inline void Update_FunMode()
{

	if (fun_mode != prev_fun_mode)
	{
		prev_fun_mode = fun_mode;

		if (!fun_mode)
			oled_rect(0,0,OLED_DISPLAY_WIDTH-1,OLED_DISPLAY_HEIGHT-1,OLED_COLOR_BLACK);

	}

	if (fun_mode)
	{

		if (fun_mode == 1)
		{
			oled_rect(0,0,OLED_DISPLAY_WIDTH-1,OLED_DISPLAY_HEIGHT-1,OLED_COLOR_WHITE);
		}
		else if (fun_mode == 4)
		{
			oled_rect(0,0,OLED_DISPLAY_WIDTH-1,OLED_DISPLAY_HEIGHT-1,OLED_COLOR_BLACK);
		}
		fun_mode = (fun_mode==6) ? 1 : (fun_mode + 1);
	}
}
	//uint32_t lux = light_read();

inline void Update_Screen ()
{
	sprintf (oled_string,"%3u",lux); 	// %6d (print as a decimal integer with a width of at least 6 wide)
										// %3.2f	(print as a floating point at least 3 wide and a precision of 2)
	oled_putString (40,SECOND_LINE, oled_string, OLED_COLOR_WHITE, OLED_COLOR_BLACK );

	sprintf (oled_string,"%3.1f",temperature/10.0);
	oled_putString (35,THIRD_LINE, oled_string, OLED_COLOR_WHITE, OLED_COLOR_BLACK );
	oled_putString (64,THIRD_LINE, oled_string, OLED_COLOR_WHITE, OLED_COLOR_BLACK );

    sprintf (oled_string,"%5d",x);
    oled_putString (3,FIFTH_LINE, oled_string, OLED_COLOR_WHITE, OLED_COLOR_BLACK );

    sprintf (oled_string,"%5d",y);
    oled_putString (32,FIFTH_LINE, oled_string, OLED_COLOR_WHITE, OLED_COLOR_BLACK );

	sprintf (oled_string,"%5d",z);
	oled_putString (62,FIFTH_LINE, oled_string, OLED_COLOR_WHITE, OLED_COLOR_BLACK );
	printf("====YOU JUST ENTER ACTIVE MODE====,bat=%d\n",bat);
}



////////////////////////////////////////////
//////// MAIN FUNCTION /////////////////////
////////////////////////////////////////////

int main (void) {

    int32_t xoff = 0;
    int32_t yoff = 0;
    int32_t zoff = 0;

    uint32_t lux = 0;
    int32_t T;

    int8_t x = 0;

    int8_t y = 0;
    int8_t z = 0;
    uint8_t dir = 1;
    uint8_t wait = 0;

    uint8_t state    = 0;

    uint8_t btn1 = 1;
    uint8_t btn2 = 1;

    uint8_t oled_string[15] = {};


    init_i2c();
    init_ssp();
    init_GPIO();
    init_uart();

    pca9532_init();
    joystick_init();
    acc_init();
    oled_init();
    rgb_init ();

    led7seg_init();

    light_enable();

    /*
     * Assume base board in zero-g position when reading first value.
     */
    acc_read(&x, &y, &z);
    xoff = 0-x;
    yoff = 0-y;
    zoff = 64-z;

    /* ---- Speaker ------> */

    GPIO_SetDir(2, 1<<0, 1);
    GPIO_SetDir(2, 1<<1, 1);

    GPIO_SetDir(0, 1<<27, 1);
    GPIO_SetDir(0, 1<<28, 1);
    GPIO_SetDir(2, 1<<13, 1);
    GPIO_SetDir(0, 1<<26, 1);

    GPIO_ClearValue(0, 1<<27); //LM4811-clk
    GPIO_ClearValue(0, 1<<28); //LM4811-up/dn
    GPIO_ClearValue(2, 1<<13); //LM4811-shutdn

    /* <---- Speaker ------ */

    /* System Clock */
    SysTick_Config(SystemCoreClock / 1000 * 5); // Configure the SysTick interrupt to occur every 1ms
    // CHANGE TO 5 MS
    // By Default SysTick is disabled

    /* Temperature Sensor */
    temp_init (&getMsTick);  //Initialize Temp Sensor driver

    init_pre_CAT();

    /* set light sensor interrupt */
    NVIC_SetPriorityGrouping(5);
    GPIO_SetDir(2, PORT_LIGHT_INT<<PIN_LIGHT_INT, 0); // 0: Input
    // init light sensor interrupt related reg

    light_setIrqInCycles(LIGHT_CYCLE_4);

    // Enable GPIO Interrupt at PIN



    /*********/

	uint32_t prev_bat_sampling = 0;
	uint32_t prev_sensor_sampling = 0;
	uint32_t sampling_rate = SENSOR_SAMPLING_TIME;

    while (1)
    {

    	if (master_mode != prev_mode)
    	{
    		prev_mode = master_mode;

    		if (master_mode == MODE_CAT)
    			mode_CAT();
    		else if (master_mode == MODE_ACTIVE)
    			mode_ACTIVE();
    	}


    	if(master_mode == MODE_ACTIVE)
    	{

    		//if ((msTicks >> 7 & 0x01) && !(prev_msTick >> 7 & 0x01) ) // 256 ms, update battery
    		if (msTicks - prev_bat_sampling >= INDICATOR_TIME_UNIT)
    		{
    			if(active_mode == ACTIVE_PS && bat >= 0)
    			{
    				pca9532_setLeds (0x0000,1<<bat--);
    			}
    			else if (active_mode == ACTIVE_FP && bat < 15)
    			{
    				pca9532_setLeds (1<<++bat,0x0000);
    			}
    			prev_bat_sampling = msTicks;

    			msg = "Today is 2016 April 08th :) \r\n";
    			UART_Send(LPC_UART3, msg, strlen(msg), BLOCKING);
    			Update_FunMode();

    		}

    		sampling_rate = (fun_mode && active_mode == ACTIVE_FP) ? (SENSOR_SAMPLING_TIME / 4) : SENSOR_SAMPLING_TIME;
    		//if ( (msTicks >> 10 & 0x0011) && !(prev_msTick >> 10 & 0x0011) ) //4.096s, update active


    		lux = light_read();

    		do_update |= GetLightInterrupt();

			if (do_update)
			{
				set_active_mod();
				light_clearIrqStatus();
				if (active_mode == ACTIVE_PS)
					do_update = 0;
			}
    		if ( do_update || active_mode != ACTIVE_PS && (msTicks - prev_sensor_sampling >= sampling_rate) )
    		{

    			rgb_setLeds (RGB_BLUE | RGB_RED);

    			acc_read(&x, &y, &z);
				x = x+xoff;
				y = y+yoff;
				z = z+zoff;

				Update_Screen();

    			//printf("4 Sec has passed\n"); // update sensors HERE
    			rgb_setLeds (RGB_RED);
    			prev_sensor_sampling = msTicks;
    			do_update = 0;
    		}


    	}

    }

    while (1)
    {

        /* ####### Accelerometer and LEDs  ###### */
        /* # */



        lux = light_read();


        printf("The value of temp. sensor: %d\n", temp_read());


        sprintf (oled_string,"%03u",lux);

        oled_putString (35,1, "LIGHT", OLED_COLOR_WHITE, OLED_COLOR_BLACK );
        oled_putString (35,33, oled_string, OLED_COLOR_WHITE, OLED_COLOR_BLACK );

        if (y < 0) {
            dir = 1;
            y = -y;
        }
        else {
            dir = -1;
        }

        if (y > 1 && wait++ > (40 / (1 + (y/10)))) {
            moveBar(1, dir);
            wait = 0;
        }


        /* # */
        /* ############################################# */


        /* ####### Joystick and OLED  ###### */
        /* # */

        state = joystick_read();
        if (state != 0)
            drawOled(state);

        /* # */
        /* ############################################# */



        /* ############ Trimpot and RGB LED  ########### */
        /* # */

        btn1 = (GPIO_ReadValue(1) >> 31) & 0x01;
        btn2 = (GPIO_ReadValue(0) >> 4) & 0x01;


        if (btn1 == 0)
        {
            playSong(song);
        }

        if (btn2 == 0)
        {
            led7seg_setChar('4', FALSE);
            Timer0_Wait(100);
        }


        led7seg_setChar('5', FALSE);


        Timer0_Wait(1);
    }


}

void check_failed(uint8_t *file, uint32_t line)
{
	/* User can add his own implementation to report the file name and line number,
	 ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */

	/* Infinite loop */
	while(1);
}

