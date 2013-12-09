#ifndef _APPLICATION_H_
#define _APPLICATION_H_

#define IDEAL_SECONDS (2)	// How seconds we should be counting for
#define IDEAL_F_MAINS (50U)	// Ideal mains frequency
#define IDEAL_CYCLES (IDEAL_F_MAINS * IDEAL_SECONDS)	// The number of mains cycles to count

#define IDEAL_F_CLK (32768UL)	// Frequency of the incoming clock
#define IDEAL_COUNTS (IDEAL_SECONDS * IDEAL_F_CLK)	// If mains is exactly 50Hz, should count exactly this many clocks in time period

#define TREND_BANDGAP (20)

enum freq_trend_enum
{
	TREND_DN = -1,
	TREND_NONE = 0,
	TREND_UP = 1
};
typedef enum freq_trend_enum FREQ_TREND_ENUM;

void FreqData_Init(void);
void FreqData_NewValue(uint16_t newFreq);
FREQ_TREND_ENUM FreqData_GetTrend(void);
uint16_t FreqData_GetAverage(void);

#endif
