/*****************************************************************************
 *   temp.c:  Driver for the Temp Sensor 6576 - period
 *
 *   Copyright(C) 2009, Embedded Artists AB
 *   All rights reserved.
 *
 ******************************************************************************/

/*
 * NOTE: GPIOInit must have been called before using any functions in this
 * file.
 */

/******************************************************************************
 * Includes
 *****************************************************************************/

#include "lpc17xx_gpio.h"
#include "temp.h"
#include "system_LPC17xx.h" // load system clock constant parameter: SystemCoreClock

/******************************************************************************
 * Defines and typedefs
 *****************************************************************************/

/*
 * Time-Select Pin Configuration. Selected by Jumper J26 on the base board
 */
#define TEMP_TS1 0
#define TEMP_TS0 0

/*
 * Pin 0.2 or pin 1.5 can be used as input source for the temp sensor
 * Selected by jumper J25.
 */
//#define TEMP_USE_P0_2

#if TEMP_TS1 == 0 && TEMP_TS0 == 0
#define TEMP_SCALAR_DIV10 1
#define NUM_HALF_PERIODS 340
#elif TEMP_TS1 == 0 && TEMP_TS0 == 1
#define TEMP_SCALAR_DIV10 4
#define NUM_HALF_PERIODS 100
#elif TEMP_TS1 == 1 && TEMP_TS0 == 0
#define TEMP_SCALAR_DIV10 16
#define NUM_HALF_PERIODS 32
#elif TEMP_TS1 == 1 && TEMP_TS0 == 1
#define TEMP_SCALAR_DIV10 64
#define NUM_HALF_PERIODS 10
#endif


#define P0_6_STATE ((GPIO_ReadValue(0) & (1 << 6)) != 0)
#define P0_2_STATE ((GPIO_ReadValue(0) & (1 << 2)) != 0)


#ifdef TEMP_USE_P0_6
#define    GET_TEMP_STATE P0_6_STATE
#else
#define    GET_TEMP_STATE P0_2_STATE
#endif


/******************************************************************************
 * External global variables
 *****************************************************************************/

/******************************************************************************
 * Local variables
 *****************************************************************************/

static uint32_t (*getTicks)(void) = NULL;

volatile uint32_t* SYSTICK_LOAD = 0xE000E014;
volatile uint32_t* SYSTICK_VAL = 0xE000E018;

/******************************************************************************
 * Local Functions
 *****************************************************************************/

/******************************************************************************
 * Public Functions
 *****************************************************************************/

/******************************************************************************
 *
 * Description:
 *    Initialize Temp Sensor driver
 *
 * Params:
 *   [in] getMsTicks - callback function for retrieving number of elapsed ticks
 *                     in milliseconds
 *
 *****************************************************************************/


void temp_init (uint32_t (*getMsTicks)(void))
{
#ifdef TEMP_USE_P0_6
    GPIO_SetDir( 0, (1<<6), 0 );
#else
    GPIO_SetDir( 0, (1<<2), 0 );
#endif
    getTicks = getMsTicks;
}

int32_t temp_service ()
{
	//uint8_t state = GET_TEMP_STATE;
	static uint32_t time0 = 0xFFFFFFFF;
	static uint32_t total = 0;
	static uint8_t count = 0;

	uint32_t time1, time_delta;

	if (time0 == 0xFFFFFFFF)
	{
		count = 0;
		total = 0;
		time0 = *SYSTICK_VAL & 0x00FFFFFF;
		return -300;
	}

		time1 = *SYSTICK_VAL & 0x00FFFFFF;

		if(time1 <= time0)
			time_delta = time0 - time1; // systicks is COUNTING DOWN
		else
			time_delta = time0 + (*SYSTICK_LOAD - time1 + 1);

		time0 = time1; // ready for next peroid calculation

		count++;
		total += time_delta;

		//return time_delta;
		if (count<100)
			return -300;
		else
		{
			time0 = 0xFFFFFFFF;
			return ( (total/100) / 100 - 2731 ); // EVERY 100 system clock is 1us
		}

}

/******************************************************************************
 *
 * Description:
 *    Read temperature
 *
 * Returns:
 *    10 x T(c), i.e. 10 times the temperature in Celcius. Example:
 *    if the temperature is 22.4 degrees the returned value is 224.
 *
 *****************************************************************************/
int32_t temp_read (void)
{
    uint8_t state = 0;
    uint32_t t1 = 0;
    uint32_t t2 = 0;
    int i = 0;

    /*
     * T(C) = ( period (us) / scalar ) - 273.15 K
     *
     * 10T(C) = (period (us) / scalar_div10) - 2731 K
     */

    state = GET_TEMP_STATE;

    /* get next state change before measuring time */
    while(GET_TEMP_STATE == state);
    state = !state;

    t1 = getTicks();

    for (i = 0; i < NUM_HALF_PERIODS; i++) {
        while(GET_TEMP_STATE == state);
        state = !state;
    }

    t2 = getTicks();
    if (t2 > t1) {
        t2 = t2-t1;
    }
    else {
        t2 = (0xFFFFFFFF - t1 + 1) + t2;
    }


    return ( (2*1000*t2) / (NUM_HALF_PERIODS*TEMP_SCALAR_DIV10) - 2731 );
}
