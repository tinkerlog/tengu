/* -----------------------------------------------------------------------
 * Title:    Tengu like face use 8x5 LED display
 * Author:   Alexander Weber alex@tinkerlog.com
 * Date:     22.10.2007
 * Hardware: ATmega48
 * Software: WinAVR 20070525
 * 
 */

#include <inttypes.h>
#include <stdlib.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>

#define TRUE 1
#define FALSE 0 
#define LED_BIT PD4

#define SCROLL_COUNT_MAX 125
#define MAX_FACES 6
#define MAX_SAMPLES 8
#define SCALE 12
#define DELTA 12
#define CHANNEL 5

static volatile uint8_t soft_prescaler = 0;		// soft prescaler
static volatile uint8_t screen_mem[8];			// screen memory
static volatile uint8_t active_col;			// active column on screen
static volatile uint8_t col_ptr = 0;			// active column in message
static volatile uint8_t scroll_count = 0;		

// clock
#define SUB_COUNT_MAX 1953
static volatile uint16_t sub_count = 0;
static volatile uint16_t counter = 0;
static volatile uint8_t seconds = 0;
static volatile uint8_t minutes = 0;
static volatile uint8_t hours = 0;
static volatile uint8_t loudness = 0;

#define EYES_NONE 0xFF
#define EYES_CLOSED 0x00
#define EYES_LEFT 0x14
#define EYES_RIGHT 0x05
static volatile uint8_t active_eyes = EYES_NONE;
static volatile uint8_t active_face = 0;
static volatile uint8_t faces[MAX_FACES][8] = {
  // { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},	// empty
  { 
    0x00,	// ----- 
    0x0A, 	// -x-x-
    0x00, 	// -----
    0x0E, 	// -xxx- 
    0x00, 	// -----
    0x00, 	// -----
    0x00, 	// -----
    0x00 	// -----
  },	
  { 
    0x00,	// ----- 
    0x0A, 	// -x-x-
    0x00, 	// -----
    0x0E, 	// -xxx- 
    0x0E, 	// -xxx-
    0x00, 	// -----
    0x00, 	// -----
    0x00 	// -----
  },	
  { 
    0x00,	// ----- 
    0x0A, 	// -x-x-
    0x00, 	// -----
    0x0E, 	// -xxx-
    0x11, 	// x---x 
    0x0E, 	// -xxx-
    0x00, 	// -----
    0x00 	// -----
  },	
  { 
    0x00,	// ----- 
    0x0A, 	// -x-x-
    0x00, 	// -----
    0x0E, 	// -xxx-
    0x11, 	// x---x 
    0x11, 	// x---x 
    0x0E, 	// -xxx-
    0x00 	// -----
  },	
  { 
    0x00,	// ----- 
    0x0A, 	// -x-x-
    0x00, 	// -----
    0x0E, 	// -xxx-
    0x11, 	// x---x 
    0x11, 	// x---x 
    0x11, 	// x---x 
    0x0E, 	// -xxx-
  },	
  { 
    0x00,	// ----- 
    0x1B, 	// xx-xx
    0x00,	// ----- 
    0x0E, 	// -xxx-
    0x11, 	// x---x 
    0x11, 	// x---x 
    0x11, 	// x---x 
    0x0E, 	// -xxx-
  }	
};


// prototypes
uint16_t get_adc(void);
void display_active_col(void);
void scroll_video(void);
void clock_tick(void);


/*
 * SIGNAL TIMER0_OVF_vect
 * Handles overflow interrupts of timer 0.
 *
 * 4MHz
 * ----
 * Prescaler 8 ==> 1953.1Hz
 * Column update every 2 ticks = 976.6Hz
 * Complete display = 122.1Hz
 *
 */
SIGNAL(TIMER0_OVF_vect) {	
  if ((++counter % 2) == 0) {
    display_active_col();
  }
  // count fractions of a second
  if (++sub_count == SUB_COUNT_MAX) {   // 4MHz
    clock_tick();
    sub_count = 0;
  }
}



/*
 * get_adc
 * Return the 10bit value of the selected adc channel.
 */
uint16_t get_adc(void) {

  // ADC setup
  ADCSRA = 
    (1 << ADEN) |			// enable ADC
    (1 << ADPS1) | (1 << ADPS0);	// set prescaler to 8	
		
  // select channel
  ADMUX = CHANNEL;
	
  // warm up the ADC, discard the first conversion
  ADCSRA |= (1 << ADSC);
  while (ADCSRA & (1 << ADSC)); 

  return ADCW;
}



/*
 * clock_tick
 * Not used for face animation
 * Adds a second to the clock. This clock is not exact because
 * of the crystal we use (4MHz).
 */
void clock_tick(void) {
  seconds++;
  if (seconds == 60) {
    seconds = 0;
    minutes++;
    if (minutes == 60) {
      minutes = 0;
      hours++;
      if (hours == 24) {
	hours = 0;
      }
    }
  }
}


/*
 * display_active_col
 * Deactivates the old column and displays the next one.
 *
 *        PORTC 4..0
 * PORTB0 ooooo
 * PORTB1 ooooo
 * PORTB2 ooooo
 * PORTB3 ooooo
 * PORTB4 ooooo
 * PORTB5 ooooo
 * PORTD2 ooooo
 * PORTD3 ooooo
 *
 */
void display_active_col(void) {
	
  // shut down all rows
  PORTC |= 0x1f;

  // shut down active column
  if (active_col > 5) {
    PORTD &= ~0x0c;	// switch off both pins
  }
  else {
    PORTB &= ~(1 << active_col);		
  }
	
  // next column
  active_col = (active_col+1) % 8;
	
  // output all rows
  // if blink then overwrite the eyes
  if ((active_col == 1) && (active_eyes != 0xFF)) {
    PORTC = ~active_eyes;
  }
  else {
    PORTC = ~faces[active_face][active_col];
  }
	
  // activate column
  if (active_col == 6) {
    PORTD |= 0x04;	
  }
  else if (active_col == 7) {
    PORTD |= 0x08;	
  }
  else {
    PORTB |= (1 << active_col);
  }

}


#define STATE_WAITING 0
#define STATE_CLOSED 1

int main(void) {

  uint8_t i = 0;
  uint16_t sum = 0;
  uint16_t amplitude = 0;
  uint8_t blink_state = STATE_CLOSED;
  uint16_t blink_wakeup = 0;
  uint16_t bored_count = 0;

  // timer 0 setup, prescaler 8
  TCCR0B |= (1 << CS01);
  // enable timer 0 interrupt
  TIMSK0 |= (1 << TOIE0);	
	
  DDRC |= 0x1f;	// PC0-PC4: row 0-4
  DDRB |= 0x3f;	// PB0-PB5: col 0-5
  DDRD |= 0x1c;	// PD2-PD3: col 6-7, PD4: debug LED

  sei();

  while (1) {

    // count how bored we are
    if (active_face == 0) {
      bored_count++;
    }
    else {
      bored_count = 0;
    }

    // state machine to control blinking
    switch (blink_state) {
    case (STATE_WAITING):
      if (seconds == blink_wakeup) {
	blink_state = STATE_CLOSED;
	if (bored_count > 2000) {
	  if ((rand() % 2) == 0) {
	    active_eyes = EYES_LEFT;
	  }
	  else {
	    active_eyes = EYES_RIGHT;
	  }
	  blink_wakeup = counter + 2000;  // look left or right for ca. 1 second
	}
	else {
	  active_eyes = EYES_CLOSED;
	  blink_wakeup = counter + 200;  // eyes closed for ca. 0.1 seconds.
	}
      }
      break;
    case (STATE_CLOSED):
      if (counter >= blink_wakeup) {
	blink_state = STATE_WAITING;
	active_eyes = EYES_NONE;
	blink_wakeup = (rand() % 30); // next blink cycle in x seconds
      } 
      break;
    }

    // do sampling and sum the amplitude
    for (i = 0; i < MAX_SAMPLES; i++) {
      amplitude = get_adc();
      if (amplitude > 512 + DELTA) {
	amplitude -= (512 + DELTA);
      }
      else if (amplitude < 512 - DELTA) {
	amplitude = 512 - amplitude - DELTA;
      }
      else {
	amplitude = 0;
      }
      sum += amplitude;
      _delay_ms(1);
    }
    sum = sum / MAX_SAMPLES;
    sum = sum / SCALE;             // scale down to map to faces
    active_face = (sum >= MAX_FACES) ? MAX_FACES - 1 : sum;
    _delay_ms(40);
  }

  return 0;

}

