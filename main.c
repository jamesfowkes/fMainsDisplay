/*
 * main.c
 *
 *  Startup file for display to show mains frequency
 */

/*
 * Standard Library Includes
 */

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>

/*
 * AVR Includes (Defines and Primitives)
 */
 
#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/wdt.h>

/*
 * Generic Library Includes
 */

#include "ringbuf.h"
#include "seven_segment_map.h"

/*
 * Device Includes
 */

#include "lib_shiftregister_common.h"
#include "lib_tlc5916.h"

/*
 * AVR Library Includes
 */

#include "lib_io.h"
#include "lib_clk.h"
#include "lib_shiftregister.h"
#include "lib_pcint.h"
#include "lib_extint.h"

/*
 * Local Application Includes
 */

#include "application.h"

/*
 * Defines and Typedefs
 */

#define IDEAL_CYCLES (IDEAL_F_MAINS * IDEAL_SECONDS)	// The number of mains cycles to count

#define IDEAL_F_CLK (32768UL)	// Frequency of the incoming clock
#define IDEAL_32KHZ_COUNTS (IDEAL_SECONDS * IDEAL_F_CLK)	// If mains is exactly 50Hz, should count exactly this many clocks in time period

#define HISTORY_SAMPLES (32) // Needs to be power of two for ring buffer. 32 samples is close enough to 1 minute of data

#define DISPLAY_FIXED_POINT_MULTIPLIER (1000UL)

// Correction to apply to kHz count before storage and display (based on
// calibration with http://www.dynamicdemand.co.uk/grid.htm)
#define CORRECTION_FACTOR (680)

// Mains input
#define MAINS_PORT IO_PORTB
#define MAINS_PIN 2
#define MAINS_PCINT_NUMBER 10

// 32768Hz input
#define CLK_PORT IO_PORTA
#define CLK_PIN 7
#define CLK_PCINT_NUMBER 7

// Shift register clock and data
#define TLC_CLK_PORT PORTB
#define TLC_DATA_PORT PORTA
#define TLC_LATCH_PORT PORTA
#define TLC_OE_PORT PORTA

#define eTLC_CLK_PORT IO_PORTB
#define eTLC_DATA_PORT IO_PORTA
#define eTLC_LATCH_PORT IO_PORTA
#define eTLC_OE_PORT IO_PORTA

#define TLC_DATA_PIN 1
#define TLC_CLK_PIN 0
#define TLC_OE_PIN 3
#define TLC_LATCH_PIN 2

// Up/down LEDs
#define UP_PORT PORTB
#define eUP_PORT IO_PORTB
#define UP_PIN 1
#define DN_PORT PORTA
#define eDN_PORT IO_PORTA
#define DN_PIN 0

enum state_enum
{
	COUNT,
	DISPLAY,
	WAIT_FOR_SYNC
};
typedef enum state_enum STATE_ENUM;

/*
 * Private Function Prototypes
 */

static void initialiseMap(void);
static void initialiseDisplay(void);

static void calculateFrequency(void);
static void updateUpDn(void);
static void updateDisplay(void);
static void setupIO(void);

static void tlcOEFn(bool on);
static void tlcLatchFn(bool on);
static void tlcNullFn(bool on);

/*
 * Private Variables
 */

static SEVEN_SEGMENT_MAP map =
{
	0, // A
	1, // B
	3, // C
	4, // D
	5, // E
	7, // F
	6, // G
	2, // DP
};

static uint8_t s_displayMap[10];
static volatile uint32_t s_kHzCount = 0;
static volatile uint16_t s_cycleCount = 0;
static uint16_t s_lastFreq;

static volatile STATE_ENUM s_state = WAIT_FOR_SYNC;

static TLC5916_CONTROL s_tlc;

int main(void)
{
	
	CLK_Init(0);

	initialiseMap();

	FreqData_Init();

	setupIO();

	sei();
	
	wdt_disable();
	
	while (true)
	{
		if (s_state == DISPLAY)
		{
			cli();
			
			calculateFrequency();
			updateUpDn();
			updateDisplay();
			
			s_state = WAIT_FOR_SYNC;
			sei();
		}
	}

	return 0;
}

static void initialiseMap(void)
{
	uint8_t i;

	for (i = 0; i < 10; ++i)
	{
		s_displayMap[i] = SSEG_CreateDigit(i, &map, true);
	}
}

static void setupIO(void)
{
	IO_SetMode(eTLC_DATA_PORT, TLC_DATA_PIN, IO_MODE_OUTPUT);
	IO_SetMode(eTLC_CLK_PORT, TLC_CLK_PIN, IO_MODE_OUTPUT);
	IO_SetMode(eTLC_OE_PORT, TLC_OE_PIN, IO_MODE_OUTPUT);
	IO_SetMode(eTLC_LATCH_PORT, TLC_LATCH_PIN, IO_MODE_OUTPUT);

	IO_SetMode(eUP_PORT, UP_PIN, IO_MODE_OUTPUT);
	IO_SetMode(eDN_PORT, DN_PIN, IO_MODE_OUTPUT);
	
	SR_Init(SFRP(TLC_CLK_PORT), TLC_CLK_PIN, SFRP(TLC_DATA_PORT), TLC_DATA_PIN);
	
	initialiseDisplay();

	PCINT_EnableInterrupt(MAINS_PCINT_NUMBER, true);
	PCINT_EnableInterrupt(CLK_PCINT_NUMBER, true);	
}

static void initialiseDisplay(void)
{
	uint8_t displayBytes[] = {5, 0, 0, 0, 0};

	uint8_t digit = 0;
	
	s_tlc.sr.shiftOutFn = SR_ShiftOut;
	s_tlc.sr.clkFn = tlcNullFn;
	s_tlc.sr.dataFn = tlcNullFn;
	s_tlc.latch = tlcLatchFn;
	s_tlc.oe = tlcOEFn;
	
	TLC5916_OutputEnable(&s_tlc, true);
	
	for (digit = 0; digit < 5; digit++)
	{
		displayBytes[digit] = s_displayMap[ displayBytes[digit] ];		
	}

	SSEG_AddDecimal(&displayBytes[1], &map, true);
	
	TLC5916_ClockOut(displayBytes, 5, &s_tlc);
	
	IO_On(UP_PORT, UP_PIN);
	IO_On(DN_PORT, DN_PIN);
}

static void calculateFrequency(void)
{
	// Multiply by DISPLAY_FIXED_POINT_MULTIPLIER to shift calculated frequency into fixed point range
	s_kHzCount += CORRECTION_FACTOR;
	s_kHzCount /= 2;
	s_lastFreq = (uint16_t)(IDEAL_32KHZ_COUNTS * DISPLAY_FIXED_POINT_MULTIPLIER * IDEAL_F_MAINS / s_kHzCount);
	
	FreqData_NewValue(s_lastFreq);
}

static void updateUpDn(void)
{
	switch( FreqData_GetTrend() )
	{
	case TREND_UP:
		IO_On(UP_PORT, UP_PIN);
		IO_Off(DN_PORT, DN_PIN);
		break;
	case TREND_DN:
		IO_Off(UP_PORT, UP_PIN);
		IO_On(DN_PORT, DN_PIN);
		break;
	case TREND_NONE:
		IO_Off(UP_PORT, UP_PIN);
		IO_Off(DN_PORT, DN_PIN);
		break;
	}
}

static void updateDisplay(void)
{
	uint8_t displayBytes[] = {0, 0, 0, 0, 0};
	uint16_t values[] = {10000, 1000, 100, 10, 1};
	uint8_t place = 0;
	uint8_t digit = 0;

	for (place = 0; place < 5; place++)
	{
		digit = (uint8_t)(s_lastFreq / values[place]);
		s_lastFreq -= (digit * values[place]);

		displayBytes[place] = s_displayMap[digit];
	}

	SSEG_AddDecimal(&displayBytes[1], &map, true);

	TLC5916_ClockOut(displayBytes, 5, &s_tlc);
}

/* IO functions for s_tlc5916 */
static void tlcOEFn(bool on) { on ? IO_On(TLC_OE_PORT, TLC_OE_PIN) : IO_Off(TLC_OE_PORT, TLC_OE_PIN); }
static void tlcLatchFn(bool on) { on ? IO_On(TLC_LATCH_PORT, TLC_LATCH_PIN) : IO_Off(TLC_LATCH_PORT, TLC_LATCH_PIN); }
static void tlcNullFn(bool on) { (void)on; }

ISR(PCINT0_vect) // Clock 32768Hz vector
{
	if (s_state == COUNT)
	{
		s_kHzCount++;
	}
}

ISR(PCINT1_vect) // Mains 50Hz vector
{
	if (s_state == WAIT_FOR_SYNC)
	{
		s_kHzCount = 0;
		s_cycleCount = 0;
		s_state = COUNT;
	}
	
	if (++s_cycleCount == (IDEAL_CYCLES * 2))
	{
		s_state = DISPLAY;
	}
}
