#include "Clock.h"
#include "Processor.h"
#include "ComputerSystemBase.h"
#include "OperatingSystemBase.h"
#ifndef DEFAULT_INTERVAL_BETWEEN_INTERRUPTS
	#define DEFAULT_INTERVAL_BETWEEN_INTERRUPTS 5
#endif

int tics=0;

void Clock_Update()
{
	if(intervalBetweenInterrupts > 0)
	{
		intervalBetweenInterrupts--;
	}	
	else
	{
		Processor_RaiseInterrupt(CLOCKINT_BIT);
		intervalBetweenInterrupts = DEFAULT_INTERVAL_BETWEEN_INTERRUPTS;
	}

	tics++;
	
    // ComputerSystem_DebugMessage(97,CLOCK,tics);

}


int Clock_GetTime() 
{
	return tics;
}
