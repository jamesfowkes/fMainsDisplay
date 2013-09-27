/*
 * Standard Library Includes
 */

#include <stdint.h>

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
 * AVR Library Includes
 */
 
#include "lib_clk.h"
#include "lib_tmr8_tick.h"

/*
 * Device Includes
 */

#include "lib_shiftregister_common.h"
#include "lib_tlc5916.h"

/*
 * Common Library Includes
 */

 #include "lib_shiftregister.h"

/*
 * Defines and Typedefs
 */

#define IDEAL_SECONDS (2U)	// How seconds we should be counting for
#define IDEAL_F_MAINS (50U)	// Ideal mains frequency
#define IDEAL_CYCLES (IDEAL_F_MAINS * IDEAL_SECONDS)	// The number of mains cycles to count

#define IDEAL_F_CLK (32768UL)	// Frequency of the incoming clock
#define IDEAL_COUNTS (IDEAL_SECONDS * IDEAL_F_CLK)	// If mains is exactly 50Hz, should count exactly this many clocks in time period

#define HISTORY_SAMPLES (32) // Needs to be power of two for ring buffer. 32 samples is close enouugh to 1 minute of data

#define DISPLAY_FIXED_POINT_MULTIPLIER (1000U)

// Mains input on INT0
#define MAINS_PORT IO_PORTB
#define MAINS_PIN 2
#define MAINS_INT_MASK (1 << INT0)

// 32768Hz input on PCINT7
#define CLK_PORT IO_PORTA
#define CLK_PIN 7
#define CLK_INT_MASK (1 << PCINT7)

// Shift register clock and data
#define TLC_DATA_PORT &PORTA
#define TLC_DATA_PIN 0
#define TLC_CLK_PORT &PORTA
#define TLC_CLK_PIN 1
#define TLC_OE_PORT PORTA
#define TLC_OE_PIN 2
#define TLC_LAT_PORT PORTA
#define TLC_LAT_PIN 3

// Up/Down LED outputs
#define UP_PORT PORTA
#define UP_PIN 4
#define DN_PORT PORTA
#define DN_PIN 5

#define enableApplicationInterrupts() (GIMSK |= (MAINS_INT_MASK | CLK_INT_MASK))
#define disableApplicationInterrupts() (GIMSK &= ~(MAINS_INT_MASK | CLK_INT_MASK))

/*
 * Private Function Prototypes
 */

static void initialiseMap(void);
static void initialiseDisplay(void);
static void initialiseBuffer(void);

static void calculateFrequency(void);

static void updateUpDn(void);
static void updateDisplay(void);
static void setupIO(void);
static void resetCount(void);

static void tlcOEFn(bool on);
static void tlcLatchFn(bool on);

/*
 * Private Variables
 */

static SEVEN_SEGMENT_MAP map = 
	{
		0, // A
		1, // B
		2, // C
		3, // D
		4, // E
		5, // F
		6, // G
		7, // DP
	};

static uint8_t s_displayMap[10];
static uint16_t s_kHzCount = 0;
static uint16_t s_cycleCount = 0;

static RING_BUFFER s_frequencyHistory;
static uint16_t s_freqData[HISTORY_SAMPLES];

static volatile bool s_updateDisplayFlag = false;

static TLC5916_CONTROL s_tlc = {
	.sr.shiftOutFn = SR_ShiftOut,
	.latch = tlcLatchFn,
	.oe = tlcOEFn
};

int main(void)
{
	
	CLK_Init(0);
	
	initialiseMap();
	
	initialiseBuffer();
	
	setupIO();
	
	resetCount();
	
	sei();
	
	while (true)
	{
		if (s_updateDisplayFlag)
		{
			s_updateDisplayFlag = false;
			calculateFrequency();
			updateUpDn();
			updateDisplay();
			resetCount();
			enableApplicationInterrupts();
		}
	}
	
	return 0;
}

static void initialiseBuffer(void)
{
	uint8_t i;
	
	Ringbuf_Init(&s_frequencyHistory, (RINGBUF_DATA)s_freqData, sizeof(uint16_t), HISTORY_SAMPLES, true);
	
	// Fill the buffer with ideal frequency values
	for (i = 0; i < HISTORY_SAMPLES; ++i)
	{
		s_freqData[i] = (IDEAL_F_MAINS * DISPLAY_FIXED_POINT_MULTIPLIER);
	}
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
	IO_SetMode(MAINS_PORT, MAINS_PIN, IO_MODE_INPUT);
	IO_SetMode(CLK_PORT, CLK_PIN, IO_MODE_INPUT);
	
	SR_Init(TLC_DATA_PORT, TLC_DATA_PIN, TLC_CLK_PORT, TLC_CLK_PIN);
	
	initialiseDisplay();
	
	// Set INT0 sense control to falling edge
	MCUCR &= ~((1 << ISC01) | (1 << ISC00));
	MCUCR |= (1 << ISC01);
	
	// Set PCINT interrupt
	PCMSK0 |= (1 << PCINT7);
	GIMSK |= (1 << PCIE0);
	
	enableApplicationInterrupts();
}

static void initialiseDisplay(void)
{
	uint8_t displayBytes[] = {5, 0, 0, 0, 0};
	
	uint16_t digit = 0;
	
	for (digit = 0; digit < 4; digit++)
	{
		displayBytes[digit] = s_displayMap[digit];
		TLC5916_ClockOut(displayBytes, 5, &s_tlc);
	}
	
	SSEG_AddDecimal(&displayBytes[2], &map);
	TLC5916_OutputEnable(&s_tlc, true);
}

static void calculateFrequency(void)
{
	// The pin change interrupt counts twice per interrupt, divide s_kHzCount by 2
	// which means mutiplying derived mains freq by 2.
	// Multiply by DISPLAY_FIXED_POINT_MULTIPLIER to shift calculated frequency into fixed point range
	
	uint16_t newFreq = IDEAL_COUNTS * DISPLAY_FIXED_POINT_MULTIPLIER * IDEAL_F_MAINS * 2 / s_kHzCount;
	
	Ringbuf_Put(&s_frequencyHistory, (uint8_t *)&newFreq);
	
}

static void updateUpDn(void)
{
	uint16_t newFreq = *(Ringbuf_Get_Newest(&s_frequencyHistory));
	uint16_t oldFreq = *(Ringbuf_Get_Oldest(&s_frequencyHistory));
	
	if (newFreq > oldFreq)
	{
		IO_On(UP_PORT, UP_PIN);
		IO_Off(DN_PORT, DN_PIN);
	}
	else if (newFreq < oldFreq)
	{
		IO_Off(UP_PORT, UP_PIN);
		IO_On(DN_PORT, DN_PIN);
	}
	else
	{
		IO_Off(UP_PORT, UP_PIN);
		IO_Off(DN_PORT, DN_PIN);
	}
}

static void updateDisplay(void)
{
	uint16_t freq = *(Ringbuf_Get_Newest(&s_frequencyHistory));
	
	uint8_t displayBytes[] = {0, 0, 0, 0, 0};
	uint16_t values[] = {10000, 1000, 100, 10, 1};
	uint8_t place = 0;
	uint8_t digit = 0;
	
	for (place = 0; place < 5; place++)
	{
		digit = (uint8_t)(freq / values[place]);
		freq -= (digit * values[place]);
		
		displayBytes[place] = s_displayMap[digit];
	}
	
	SSEG_AddDecimal(&displayBytes[2], &map);
	
	TLC5916_ClockOut(displayBytes, 5, &s_tlc);
}

static void resetCount(void)
{
	s_kHzCount = 0;
	s_cycleCount = 0;
}

/* IO functions for TLC5916 */
static void tlcOEFn(bool on) { on ? IO_On(TLC_OE_PORT, TLC_OE_PIN) : IO_Off(TLC_OE_PORT, TLC_OE_PIN); }
static void tlcLatchFn(bool on) { on ? IO_On(TLC_LAT_PORT, TLC_LAT_PIN) : IO_Off(TLC_LAT_PORT, TLC_LAT_PIN); }

ISR(EXT_INT0_vect)
{
	if (++s_cycleCount == IDEAL_CYCLES)
	{
		disableApplicationInterrupts();
		s_updateDisplayFlag = true;
	}
}

ISR(PCINT0_vect)
{
	s_kHzCount++;
}