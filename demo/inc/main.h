/*
 * main.h
 *
 *  Created on: Apr 14, 2016
 *      Author: Hui Min
 */

#ifndef MAIN_H_
#define MAIN_H_

#define MODE_CAT 		1
#define MODE_ACTIVE 	2
#define MODE_NONE		0

#define ACTIVE_PS		0
#define ACTIVE_NO		1
#define ACTIVE_FP		2

#define SENSOR_SAMPLING_TIME 4000
#define INDICATOR_TIME_UNIT 250

#define FIRST_LINE		3
#define SECOND_LINE		14
#define THIRD_LINE		25
#define FOURTH_LINE		36
#define FIFTH_LINE		47
#define SIXTH_LINE		58

#define UART_RX_BUFFER_SIZE 20

typedef struct{
	uint8_t ACCEL 	: 1;
	uint8_t TEMP 	: 1;
	uint8_t LIGHT	: 1;
	uint8_t TRANS	: 1;
}SensorStatus;

extern volatile SensorStatus sensors;
extern volatile uint32_t msTicks;

#endif /* MAIN_H_ */
