/*
The MIT License (MIT)

Copyright (c) 2015 silverx

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/

#include <inttypes.h>
#include <math.h>

#include "pid.h"
#include "config.h"
#include "util.h"
#include "drv_pwm.h"
#include "control.h"
#include "defines.h"
#include "drv_time.h"


extern float rx[];
extern float gyro[];
extern int failsafe;
extern float pidoutput[];

int onground = 1;
float pwmsum;
float thrsum;

float error[PIDNUMBER];
float motormap( float input);
int lastchange;
int pulse;
//static unsigned long timestart = 0;
float yawangle;

extern float looptime;

extern char auxchange[];
extern char aux[];

extern int pwmdir;

void bridge_sequencer(int dir);
// bridge 
int stage;
int laststage;
int lastdir;

int currentdir;


void control( void)
{

	// hi rates
	float ratemulti;
	float ratemultiyaw;

	
	if ( aux[RATES] ) 
	{
		ratemulti = HIRATEMULTI;
		ratemultiyaw = HIRATEMULTIYAW;
	}
	else 
	{
		ratemulti = 1.0;
		ratemultiyaw = 1.0;
	}

	
	yawangle = yawangle + gyro[YAW]*looptime;

	if ( auxchange[HEADLESSMODE] )
	{
		yawangle = 0;
	}
	
	if ( aux[HEADLESSMODE] ) 
	{
		float temp = rx[ROLL];
		rx[ROLL] = rx[ROLL] * cosf( yawangle) - rx[PITCH] * sinf(yawangle );
		rx[PITCH] = rx[PITCH] * cosf( yawangle) + temp * sinf(yawangle ) ;
	}

	if ( aux[INVERTEDMODE] ) 
	{
		bridge_sequencer(REVERSE);	// reverse
	}
	else
	{
		bridge_sequencer(FORWARD);	// forward
	}


// pwmdir controls hardware directly so we make a copy here
	currentdir = pwmdir;
	
/*	
int change = (rx[5] > 0.5);

if ( change != lastchange )
{
	pulse = 1;
}
lastchange = change;

float motorchange = 0;

if ( pulse )
{
	if ( !timestart) timestart = gettime();
	
	
	if ( gettime() - timestart < 200000 )
	{
		motorchange = 0.2;	
	}
	else
	{
		motorchange = 0.0;
		pulse = 0;
		timestart = 0;
	}
}
*/
float rxtemp[3];

for (int i = 0 ; i < 3 ; i++)
{
	rxtemp[i] = rx[i];
}

if (currentdir == REVERSE)
		{	
			// invert pitch in reverse mode 
		//rxtemp[ROLL] = - rx[ROLL];
		rxtemp[PITCH] = - rx[PITCH];
		rxtemp[YAW]	= - rx[YAW];	
				
		}
		else
		{
			// normal thrust mode
			//rxtemp[ROLL] = - rx[ROLL];
			//rxtemp[PITCH] = - rx[PITCH];
			//rxtemp[YAW]	= - rx[YAW];	 	
			
		}

	error[ROLL] = rxtemp[ROLL] * (float)MAX_RATE * (float)DEGTORAD * ratemulti - gyro[ROLL];
	error[PITCH] = rxtemp[PITCH] * (float)MAX_RATE *(float) DEGTORAD * ratemulti - gyro[PITCH];
	error[YAW] = ( - 1) * rxtemp[YAW] * (float)MAX_RATEYAW * (float)DEGTORAD * ratemultiyaw - gyro[YAW];
	

pid_precalc();
	
	pid(ROLL);
	pid(PITCH);
	pid(YAW);


	
// map throttle so under 10% it is zero	
float	throttle = mapf(rx[3], 0 , 1 , -0.1 , 1 );
if ( throttle < 0   ) throttle = 0;

// turn motors off if throttle is off and pitch / roll sticks are centered
	if ( failsafe || (throttle < 0.001f && (!ENABLESTIX||  (fabsf(rx[ROLL]) < 0.5f && fabsf(rx[PITCH]) < 0.5f ) ) ) ) 

	{ // motors off
		for ( int i = 0 ; i <= 3 ; i++)
		{
			pwm_set( i , 0 );	
		}	
		onground = 1;
		pwmsum = 0;
		thrsum = 0;
	}
	else
	{
		onground = 0;
		if ( stage == BRIDGE_WAIT ) onground = 1;
		float mix[4];	

		if (currentdir == REVERSE)
		{
			// inverted flight
			pidoutput[ROLL] = -pidoutput[ROLL];
			pidoutput[PITCH] = -pidoutput[PITCH];
			//pidoutput[YAW] = -pidoutput[YAW];	
		}
		else
		{
			pidoutput[ROLL] = -pidoutput[ROLL];
			//pidoutput[PITCH] = -pidoutput[PITCH];
			//pidoutput[YAW] = -pidoutput[YAW];	
		}


		
		mix[MOTOR_FR] = throttle - pidoutput[ROLL] - pidoutput[PITCH] + pidoutput[YAW];		// FR
		mix[MOTOR_FL] = throttle + pidoutput[ROLL] - pidoutput[PITCH] - pidoutput[YAW];		// FL	
		mix[MOTOR_BR] = throttle - pidoutput[ROLL] + pidoutput[PITCH] - pidoutput[YAW];		// BR
		mix[MOTOR_BL] = throttle + pidoutput[ROLL] + pidoutput[PITCH] + pidoutput[YAW];		// BL	
			
		
		for ( int i = 0 ; i <= 3 ; i++)
		{
		#ifndef NOMOTORS
		pwm_set( i ,motormap( mix[i] ) );		
		#endif
		}	
		
		pwmsum = 0;
		for ( int i = 0 ; i <= 3 ; i++)
		{
			if ( mix[i] < 0 ) mix[i] = 0;
			if ( mix[i] > 1 ) mix[i] = 1;
			thrsum+= mix[i];
		}	
		thrsum = thrsum / 4;
		
	}// end motors on
	
}


/*
float motormap_old( float input)
{ 
	// this is a thrust to pwm function
	//  float 0 to 1 input and output
	// reverse of a power to thrust graph for 8.5 mm coreless motors + hubsan prop
	// should be ok for other motors without reduction gears.
	// a*x^2 + b*x + c
	// a = 0.75 , b = 0.061 , c = 0.185

if (input > 1.0) input = 1.0;
if (input < 0) input = 0;
	
if ( input < 0.25 ) return input;

input = input*input*0.75  + input*(0.0637);
input += 0.185;

return input;   
}
*/

float motormap_H8( float input)
{ 
	// this is a thrust to pwm function
	//  float 0 to 1 input and output
	// output can go negative slightly
	// measured eachine motors and prop, stock battery
	// a*x^2 + b*x + c
	// a = 0.262 , b = 0.771 , c = -0.0258

if (input > 1.0f) input = 1.0f;
if (input < 0) input = 0;

input = input*input*0.262f  + input*(0.771f);
input += -0.0258f;

return input;   
}

float motormap( float input)
{ 

	// H101 thrust curve for normal thrust direction
	// a*x^2 + b*x + c

if (input > 1.0f) input = 1.0f;
if (input < 0) input = 0;

input = input*input*0.277f  + input*(0.715f);
input += 0.0102f;

return input;   
}



unsigned long bridgetime;

// the bridge sequencer creates a pause between motor direction changes
// that way the motors do not try to instantly go in reverse and have time to slow down


void bridge_sequencer(int dir)
{
  
	if ( dir == DIR1 && stage != BRIDGE_FORWARD )
	{
		 	
		if ( stage ==  BRIDGE_REVERSE )
		{
			stage = BRIDGE_WAIT;
			bridgetime = gettime();
			pwm_dir(FREE);
		}
		if ( stage == BRIDGE_WAIT)
		{
			if ( gettime() - bridgetime > BRIDGE_TIMEOUT ) 
			{
				// timeout has elapsed
				stage = BRIDGE_FORWARD;
				pwm_dir(DIR1);

			}
					
		}
			 	
	}
		if ( dir == DIR2 && stage != BRIDGE_REVERSE )
	{
		 	
		if ( stage ==  BRIDGE_FORWARD )
		{
			stage = BRIDGE_WAIT;
			bridgetime = gettime();
			pwm_dir(FREE);
		}
		if ( stage == BRIDGE_WAIT)
		{
			if ( gettime() - bridgetime > BRIDGE_TIMEOUT ) 
			{
				// timeout has elapsed
				stage = BRIDGE_REVERSE;
				pwm_dir(DIR2);

			}
					
		}
			 	
	}

	
	
	
}









