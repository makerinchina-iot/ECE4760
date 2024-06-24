/*
 * File:        TFT, keypad, DAC, LED test
 * Author:      Bruce Land
 * For use with Sean Carroll's Big Board
 * Adapted from:
 *              main.c by
 * Author:      Syed Tahmid Mahbub
 * Target PIC:  PIC32MX250F128B
 */

////////////////////////////////////
// clock AND protoThreads configure!
// You MUST check this file!
#include "config_1_3_2.h"
// threading library
#include "pt_cornell_1_3_2.h"

////////////////////////////////////
// graphics libraries
// SPI channel 1 connections to TFT
#include "tft_master.h"
#include "tft_gfx.h"
// need for rand function
#include <stdlib.h>
////////////////////////////////////

////////////////////////////////////
// pullup/down macros for keypad
// PORT B
#define EnablePullDownB(bits) CNPUBCLR=bits; CNPDBSET=bits;
#define DisablePullDownB(bits) CNPDBCLR=bits;
#define EnablePullUpB(bits) CNPDBCLR=bits; CNPUBSET=bits;
#define DisablePullUpB(bits) CNPUBCLR=bits;
//PORT A
#define EnablePullDownA(bits) CNPUACLR=bits; CNPDASET=bits;
#define DisablePullDownA(bits) CNPDACLR=bits;
#define EnablePullUpA(bits) CNPDACLR=bits; CNPUASET=bits;
#define DisablePullUpA(bits) CNPUACLR=bits;
////////////////////////////////////

////////////////////////////////////
// some precise, fixed, short delays
// to use for extending pulse durations on the keypad
// if behavior is erratic
#define NOP asm("nop");
// 1/2 microsec
#define wait20 NOP;NOP;NOP;NOP;NOP;NOP;NOP;NOP;NOP;NOP;NOP;NOP;NOP;NOP;NOP;NOP;NOP;NOP;NOP;NOP;
// one microsec
#define wait40 wait20;wait20;
////////////////////////////////////

/* Demo code for interfacing TFT (ILI9340 controller) to PIC32
 * The library has been modified from a similar Adafruit library
 */
// Adafruit data:
/***************************************************
  This is an example sketch for the Adafruit 2.2" SPI display.
  This library works with the Adafruit 2.2" TFT Breakout w/SD card
  ----> http://www.adafruit.com/products/1480

  Check out the links above for our tutorials and wiring diagrams
  These displays use SPI to communicate, 4 or 5 pins are required to
  interface (RST is optional)
  Adafruit invests time and resources providing this open source code,
  please support Adafruit and open-source hardware by purchasing
  products from Adafruit!

  Written by Limor Fried/Ladyada for Adafruit Industries.
  MIT license, all text above must be included in any redistribution
 ****************************************************/

// string buffer
char buffer[60];

////////////////////////////////////
// DAC ISR
// A-channel, 1x, active
#define DAC_config_chan_A 0b0011000000000000
// B-channel, 1x, active
#define DAC_config_chan_B 0b1011000000000000
//== Timer 2 interrupt handler ===========================================
volatile unsigned int DAC_data ;// output value
volatile SpiChannel spiChn = SPI_CHANNEL2 ;	// the SPI channel to use
volatile int spiClkDiv = 2 ; // 20 MHz max speed for this DAC

void __ISR(_TIMER_2_VECTOR, ipl2) Timer2Handler(void)
{
    mT2ClearIntFlag();
    
    // generate  ramp
    // at 100 ksample/sec, 2^12 samples (4096) on one ramp up
    // yields 100000/4096 = 24.4 Hz.
     DAC_data = (DAC_data + 1) & 0xfff ; // for testing
    
    // CS low to start transaction
     mPORTBClearBits(BIT_4); // start transaction
    // test for ready
     while (TxBufFullSPI2());
     // write to spi2
     WriteSPI2(DAC_config_chan_A | DAC_data);
    // test for done
    while (SPI2STATbits.SPIBUSY); // wait for end of transaction
     // CS high
     mPORTBSetBits(BIT_4); // end transaction
}

// === thread structures ============================================
// thread control structs
// note that UART input and output are threads
static struct pt pt_timer, pt_color, pt_anim, pt_key; ;

// system 1 second interval tick
int sys_time_seconds ;

// === Timer Thread =================================================
// update a 1 second tick counter
static PT_THREAD (protothread_timer(struct pt *pt))
{
    PT_BEGIN(pt);
     tft_setCursor(0, 0);
     tft_setTextColor(ILI9340_WHITE);  tft_setTextSize(1);
     tft_writeString("Time in seconds since boot\n");
     // set up LED to blink
     mPORTASetBits(BIT_0 );	//Clear bits to ensure light is off.
     mPORTASetPinsDigitalOut(BIT_0 );    //Set port as output
      while(1) {
        // yield time 1 second
        PT_YIELD_TIME_msec(1000) ;
        sys_time_seconds++ ;
        // toggle the LED on the big board
        mPORTAToggleBits(BIT_0);
        // draw sys_time
        tft_fillRoundRect(0,10, 100, 14, 1, ILI9340_BLACK);// x,y,w,h,radius,color
        tft_setCursor(0, 10);
        tft_setTextColor(ILI9340_YELLOW); tft_setTextSize(2);
        sprintf(buffer,"%d", sys_time_seconds);
        tft_writeString(buffer);
        // NEVER exit while
      } // END WHILE(1)
  PT_END(pt);
} // timer thread

// === Color Thread =================================================
// draw 3 color patches for R,G,B from a random number
static int color ;
static int i;
static PT_THREAD (protothread_color(struct pt *pt))
{
    PT_BEGIN(pt);
      while(1) {
        // yield time 1 second
        PT_YIELD_TIME_msec(1000) ;

        // choose a random color
        color = rand() & 0xffff ;
       
        // draw color string
        tft_fillRoundRect(0,50, 150, 14, 1, ILI9340_BLACK);// x,y,w,h,radius,color
        tft_setCursor(0, 50);
        tft_setTextColor(ILI9340_WHITE); tft_setTextSize(1);
        sprintf(buffer," %04x  %04x  %04x  %04x", color & 0x1f, color & 0x7e0, color & 0xf800, color);
        tft_writeString(buffer);

        // draw the actual color patches
        tft_fillRoundRect(5,70, 30, 30, 1, color & 0x1f);// x,y,w,h,radius,blues
        tft_fillRoundRect(40,70, 30, 30, 1, color & 0x7e0);// x,y,w,h,radius,greens
        tft_fillRoundRect(75,70, 30, 30, 1, color & 0xf800);// x,y,w,h,radius,reds
        // now draw the RGB mixed color
        tft_fillRoundRect(110,70, 30, 30, 1, color);// x,y,w,h,radius,mix color
        // NEVER exit while
      } // END WHILE(1)
  PT_END(pt);
} // color thread

// === Animation Thread =============================================
// update a 1 second tick counter
static int xc=10, yc=150, vxc=2, vyc=0;
static PT_THREAD (protothread_anim(struct pt *pt))
{
    PT_BEGIN(pt);
      while(1) {
        // yield time 1 second
        PT_YIELD_TIME_msec(32);

        // erase disk
         tft_fillCircle(xc, yc, 4, ILI9340_BLACK); //x, y, radius, color
        // compute new position
         xc = xc + vxc;
         if (xc<5 || xc>235) vxc = -vxc;         
         //  draw disk
         tft_fillCircle(xc, yc, 4, ILI9340_GREEN); //x, y, radius, color
        // NEVER exit while
      } // END WHILE(1)
  PT_END(pt);
} // animation thread

// === Keypad Thread =============================================
// connections:
// A0 -- row 1 -- thru 300 ohm resistor -- avoid short when two buttons pushed
// A1 -- row 2 -- thru 300 ohm resistor
// A2 -- row 3 -- thru 300 ohm resistor
// A3 -- row 4 -- thru 300 ohm resistor
// B7 -- col 1 -- internal pulldown resistor -- avoid open circuit input when no button pushed
// B8 -- col 2 -- internal pulldown resistor
// B9 -- col 3 -- internal pulldown resistor

static PT_THREAD (protothread_key(struct pt *pt))
{
    PT_BEGIN(pt);
    static int keypad, i, pattern;
    // order is 0 thru 9 then * ==10 and # ==11
    // no press = -1
    // table is decoded to natural digit order (except for * and #)
    // 0x80 for col 1 ; 0x100 for col 2 ; 0x200 for col 3
    // 0x01 for row 1 ; 0x02 for row 2; etc
    static int keytable[12]={0x108, 0x81, 0x101, 0x201, 0x82, 0x102, 0x202, 0x84, 0x104, 0x204, 0x88, 0x208};
    // init the keypad pins A0-A3 and B7-B9
    // PortA ports as digital outputs
    mPORTASetPinsDigitalOut(BIT_0 | BIT_1 | BIT_2 | BIT_3);    //Set port as output
    // PortB as inputs
    mPORTBSetPinsDigitalIn(BIT_7 | BIT_8 | BIT_9);    //Set port as input
    // and turn on pull-down on inputs
    EnablePullDownB( BIT_7 | BIT_8 | BIT_9);

      while(1) {

        // yield time
        PT_YIELD_TIME_msec(30);
        
        // read each row sequentially
        mPORTAClearBits(BIT_0 | BIT_1 | BIT_2 | BIT_3);
        pattern = 1; mPORTASetBits(pattern);
   
        for (i=0; i<4; i++) {
            wait40 ;
            keypad  = mPORTBReadBits(BIT_7 | BIT_8 | BIT_9);
            if(keypad!=0) {keypad |= pattern ; break;}
            mPORTAClearBits(pattern);
            pattern <<= 1;
            mPORTASetBits(pattern);
        }
        
        // search for keycode
        if (keypad > 0){ // then button is pushed
            for (i=0; i<12; i++){
                if (keytable[i]==keypad) break;
            }
            // if invalid, two button push, set to -1
            if (i==12) i=-1;
        }
        else i = -1; // no button pushed

        // draw key number
        tft_fillRoundRect(30,200, 100, 14, 1, ILI9340_BLACK);// x,y,w,h,radius,color
        tft_setCursor(30, 200);
        tft_setTextColor(ILI9340_YELLOW); tft_setTextSize(2);
        sprintf(buffer,"%d", i);
        if (i==10)sprintf(buffer,"*");
        if (i==11)sprintf(buffer,"#");
        tft_writeString(buffer);

        // NEVER exit while
      } // END WHILE(1)
  PT_END(pt);
} // keypad thread

// === Main  ======================================================
void main(void) {
 //SYSTEMConfigPerformance(PBCLK);
  
  ANSELA = 0; ANSELB = 0; 

  // set up DAC on big board
  // timer interrupt //////////////////////////
    // Set up timer2 on,  interrupts, internal clock, prescalar 1, toggle rate
    // at 30 MHz PB clock 60 counts is two microsec
    // 400 is 100 ksamples/sec
    // 2000 is 20 ksamp/sec
    OpenTimer2(T2_ON | T2_SOURCE_INT | T2_PS_1_1, 400);

    // set up the timer interrupt with a priority of 2
    ConfigIntTimer2(T2_INT_ON | T2_INT_PRIOR_2);
    mT2ClearIntFlag(); // and clear the interrupt flag

    // SCK2 is pin 26 
    // SDO2 (MOSI) is in PPS output group 2, could be connected to RB5 which is pin 14
    PPSOutput(2, RPB5, SDO2);

    // control CS for DAC
    mPORTBSetPinsDigitalOut(BIT_4);
    mPORTBSetBits(BIT_4);

    // divide Fpb by 2, configure the I/O ports. Not using SS in this example
    // 16 bit transfer CKP=1 CKE=1
    // possibles SPI_OPEN_CKP_HIGH;   SPI_OPEN_SMP_END;  SPI_OPEN_CKE_REV
    // For any given peripherial, you will need to match these
    // clk divider set to 2 for 20 MHz
    SpiChnOpen(SPI_CHANNEL2, SPI_OPEN_ON | SPI_OPEN_MODE16 | SPI_OPEN_MSTEN | SPI_OPEN_CKE_REV , 2);
  // end DAC setup
    
  // === config threads ==========
  // turns OFF UART support and debugger pin, unless defines are set
  PT_setup();

  // === setup system wide interrupts  ========
  INTEnableSystemMultiVectoredInt();

  // init the threads
  PT_INIT(&pt_timer);
  PT_INIT(&pt_color);
  PT_INIT(&pt_anim);
  PT_INIT(&pt_key);

  // init the display
  // NOTE that this init assumes SPI channel 1 connections
  tft_init_hw();
  tft_begin();
  tft_fillScreen(ILI9340_BLACK);
  //240x320 vertical display
  tft_setRotation(0); // Use tft_setRotation(1) for 320x240

  // seed random color
  srand(1);
  
  // round-robin scheduler for threads
  while (1){
      PT_SCHEDULE(protothread_timer(&pt_timer));
      PT_SCHEDULE(protothread_color(&pt_color));
      PT_SCHEDULE(protothread_anim(&pt_anim));
      PT_SCHEDULE(protothread_key(&pt_key));
      }
  } // main

// === end  ======================================================

