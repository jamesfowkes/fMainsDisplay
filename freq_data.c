/*
 * Standard Library Includes
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>

/*
 * Generic Library Includes
 */

#include "ringbuf.h"

/*
 * Local Application Includes
 */

#include "application.h"

/*
 * Defines and Typedefs
 */
 
#define AVERAGE_OVER_LAST_N_MINUTES 10
#define SAMPLE_EVERY_N_SECONDS 60

#define BUFFER_SIZE ((AVERAGE_OVER_LAST_N_MINUTES * 60) / SAMPLE_EVERY_N_SECONDS)

static RING_BUFFER s_frequencyBuffer;
static uint16_t s_freqData[BUFFER_SIZE];

static RING_BUFFER s_averageBuffer;
static uint16_t s_averageData[BUFFER_SIZE];

static uint8_t s_seconds_since_last_reading = 0U;
static uint16_t s_currentAverage = IDEAL_F_MAINS * 1000U;

static uint16_t calculateNewAverage(uint16_t newFreq);

void FreqData_Init(void)
{
	Ringbuf_Init(&s_frequencyBuffer, (uint8_t*)s_freqData, sizeof(uint16_t), BUFFER_SIZE, true);
	Ringbuf_Init(&s_averageBuffer, (uint8_t*)s_averageData, sizeof(uint16_t), BUFFER_SIZE, true);
	
	// Fill the buffers with ideal frequency values
	uint8_t i = 0;
	uint16_t freq = IDEAL_F_MAINS * 1000U;

	for (i = 0; i < BUFFER_SIZE; ++i)
	{	
		Ringbuf_Put(&s_frequencyBuffer, (uint8_t*)&freq);
	}
}

void FreqData_NewValue(uint16_t newFreq)
{
	s_seconds_since_last_reading += IDEAL_SECONDS;
	
	if (s_seconds_since_last_reading >= SAMPLE_EVERY_N_SECONDS)
	{
		s_currentAverage = calculateNewAverage(newFreq);
		Ringbuf_Put(&s_frequencyBuffer, (uint8_t*)&newFreq);
		Ringbuf_Put(&s_averageBuffer, (uint8_t*)&s_currentAverage);
		
		s_seconds_since_last_reading = 0U;
	}
}

FREQ_TREND_ENUM FreqData_GetTrend(void)
{
	FREQ_TREND_ENUM trend = TREND_NONE;
	if (Ringbuf_Full(&s_averageBuffer))
	{
		uint16_t newestAverage = *(uint16_t*)(Ringbuf_Get_Newest(&s_averageBuffer));
		uint16_t oldestAverage = *(uint16_t*)(Ringbuf_Get_Oldest(&s_averageBuffer));
		
		if (newestAverage > oldestAverage)
		{
			if ((newestAverage - oldestAverage) >= TREND_BANDGAP)
			{
				trend = TREND_UP;
			}
		}
		else if (oldestAverage > newestAverage)
		{
			if ((oldestAverage - newestAverage) >= TREND_BANDGAP)
			{
				trend = TREND_DN;
			}
		}
	}
	
	return trend;
}

uint16_t FreqData_GetAverage(void)
{
	return s_currentAverage;
}

static uint16_t calculateNewAverage(uint16_t newFreq)
{
	uint16_t oldestFreq = *(uint16_t*)(Ringbuf_Get_Oldest(&s_frequencyBuffer));
	uint32_t tmp_average = (uint32_t)s_currentAverage;
	
	tmp_average *= BUFFER_SIZE;
	tmp_average = tmp_average - oldestFreq + newFreq;
	tmp_average /= BUFFER_SIZE;
	
	return (uint16_t)tmp_average;
}
