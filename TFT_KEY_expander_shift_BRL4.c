/*
 * File:        TFT, keypad, DAC, LED, PORT EXPANDER test
 * Author:      Bruce Land
 * For use with Sean Carroll's Big Board
 * Target PIC:  PIC32MX250F128B
 */

////////////////////////////////////
// clock AND protoThreads configure!
// You MUST check this file!
#include "config_1_3_2.h"
// threading library
#include "pt_cornell_1_3_2.h"
// yup, the expander
#include "port_expander_brl4.h"

////////////////////////////////////
// graphics libraries
// SPI channel 1 connections to TFT
#include "tft_master.h"
#include "tft_gfx.h"
// need for rand function
#include <stdlib.h>
////////////////////////////////////

// lock out timer interrupt during spi comm to port expander
// This is necessary if you use the SPI2 channel in an ISR
#define start_spi2_critical_section INTEnable(INT_T2, 0);
#define end_spi2_critical_section INTEnable(INT_T2, 1);

////////////////////////////////////
// some precise, fixed, short delays
// to use for extending pulse durations on the keypad
// if behavior is erratic
#define NOP asm("nop");
// 20 cycles 
#define wait20 NOP;NOP;NOP;NOP;NOP;NOP;NOP;NOP;NOP;NOP;NOP;NOP;NOP;NOP;NOP;NOP;NOP;NOP;NOP;NOP;
// 40 cycles
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
volatile int spiClkDiv = 4 ; // 10 MHz max speed for prot expander!!

void __ISR(_TIMER_2_VECTOR, ipl2) Timer2Handler(void)
{
    int junk;
    
    mT2ClearIntFlag();
    
    // generate  ramp
    // at 100 ksample/sec, 2^12 samples (4096) on one ramp up
    // yields 100000/4096 = 24.4 Hz.
     DAC_data = (DAC_data + 1) & 0xfff ; // for testing
    
     // test for ready
     while (TxBufFullSPI2());
     
    // reset spi mode to avoid conflict with expander
    SPI_Mode16();
    // CS low to start transaction
     mPORTBClearBits(BIT_4); // start transaction 
     // write to spi2
     WriteSPI2(DAC_config_chan_A | DAC_data);
    // test for done
    while (SPI2STATbits.SPIBUSY); // wait for end of transaction
    // MUST read to clear buffer for port expander elsewhere in code
    junk = ReadSPI2(); 
   // CS high
   mPORTBSetBits(BIT_4); // end transaction
   //
     
}

// === print a line on TFT =====================================================
// print a line on the TFT
// string buffer
char buffer[60];
void printLine(int line_number, char* print_buffer, short text_color, short back_color){
    // line number 0 to 31 
    /// !!! assumes tft_setRotation(0);
    // print_buffer is the string to print
    int v_pos;
    v_pos = line_number * 10 ;
    // erase the pixels
    tft_fillRoundRect(0, v_pos, 239, 8, 1, back_color);// x,y,w,h,radius,color
    tft_setTextColor(text_color); 
    tft_setCursor(0, v_pos);
    tft_setTextSize(1);
    tft_writeString(print_buffer);
}

void printLine2(int line_number, char* print_buffer, short text_color, short back_color){
    // line number 0 to 31 
    /// !!! assumes tft_setRotation(0);
    // print_buffer is the string to print
    int v_pos;
    v_pos = line_number * 20 ;
    // erase the pixels
    tft_fillRoundRect(0, v_pos, 239, 16, 1, back_color);// x,y,w,h,radius,color
    tft_setTextColor(text_color); 
    tft_setCursor(0, v_pos);
    tft_setTextSize(2);
    tft_writeString(print_buffer);
}

// Predefined colors definitions (from tft_master.h)
//#define	ILI9340_BLACK   0x0000
//#define	ILI9340_BLUE    0x001F
//#define	ILI9340_RED     0xF800
//#define	ILI9340_GREEN   0x07E0
//#define ILI9340_CYAN    0x07FF
//#define ILI9340_MAGENTA 0xF81F
//#define ILI9340_YELLOW  0xFFE0
//#define ILI9340_WHITE   0xFFFF

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
// And toggles the Z port on the port expander
// Port Expander connections:
// z0 -- set as output -- view on scope
// z1 -- set as output -- view on scope
static int color ;
static int i;
static unsigned char ioZ;
static PT_THREAD (protothread_color(struct pt *pt))
{
    PT_BEGIN(pt);
    
      while(1) {
        // yield time 1 second
        PT_YIELD_TIME_msec(500) ;
        
        start_spi2_critical_section;     
        setBits(GPIOZ, BIT_0)  ; 
        toggleBits(GPIOZ, BIT_1)  ; 
        end_spi2_critical_section;
        
        PT_YIELD_TIME_msec(500) ;
        
        start_spi2_critical_section;     
        clearBits(GPIOZ, BIT_0)  ; 
        ioZ = readBits(GPIOZ, BIT_0 | BIT_1)  ; 
        end_spi2_critical_section;

        tft_fillRoundRect(0, 170, 150, 20, 1, ILI9340_BLACK);// x,y,w,h,radius,color
        tft_setCursor(0, 170);
        tft_setTextColor(ILI9340_WHITE); tft_setTextSize(2);
        sprintf(buffer,"Port Z %02x", ioZ);
        tft_writeString(buffer);
        
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
// Port Expander connections:
// y0 -- row 1 -- thru 300 ohm resistor -- avoid short when two buttons pushed
// y1 -- row 2 -- thru 300 ohm resistor
// y2 -- row 3 -- thru 300 ohm resistor
// y3 -- row 4 -- thru 300 ohm resistor
// y4 -- col 1 -- internal pullup resistor -- avoid open circuit input when no button pushed
// y5 -- col 2 -- internal pullup resistor
// y6 -- col 3 -- internal pullup resistor
// y7 -- shift key connection -- internal pullup resistor

static PT_THREAD (protothread_key(struct pt *pt))
{
    PT_BEGIN(pt);
    static int keypad, i, pattern;
    // order is 0 thru 9 then * ==10 and # ==11
    // no press = -1
    // table is decoded to natural digit order (except for * and #)
    // with shift key codes for each key
    // keys 0-9 return the digit number
    // keys 10 and 11 are * adn # respectively
    // Keys 12 to 21 are the shifted digits
    // keys 22 and 23 are shifted * and # respectively
    static int keytable[24]=
    //        0     1      2    3     4     5     6      7    8     9    10-*  11-#
            {0xd7, 0xbe, 0xde, 0xee, 0xbd, 0xdd, 0xed, 0xbb, 0xdb, 0xeb, 0xb7, 0xe7,
    //        s0     s1    s2  s3    s4    s5    s6     s7   s8    s9    s10-* s11-#
             0x57, 0x3e, 0x5e, 0x6e, 0x3d, 0x5d, 0x6d, 0x3b, 0x5b, 0x6b, 0x37, 0x67};
    // bit pattern for each row of the keypad scan -- active LOW
    // bit zero low is first entry
    static char out_table[4] = {0b1110, 0b1101, 0b1011, 0b0111};
    // init the port expander
    start_spi2_critical_section;
    initPE();
    // PortY on Expander ports as digital outputs
    mPortYSetPinsOut(BIT_0 | BIT_1 | BIT_2 | BIT_3);    //Set port as output
    // PortY as inputs
    // note that bit 7 will be shift key input, 
    // separate from keypad
    mPortYSetPinsIn(BIT_4 | BIT_5 | BIT_6 | BIT_7);    //Set port as input
    mPortYEnablePullUp(BIT_4 | BIT_5 | BIT_6 | BIT_7);
    
    // init the Z port to try other output functions
    mPortZSetPinsOut(BIT_0 | BIT_1);    //Set port as output
    end_spi2_critical_section ;
    
    // the read-pattern if no button is pulled down by an output
    #define no_button (0x70)

      while(1) {
        // yield time
        PT_YIELD_TIME_msec(30);
    
        for (i=0; i<4; i++) {
            start_spi2_critical_section;
            // scan each rwo active-low
            writePE(GPIOY, out_table[i]);
            //reading the port also reads the outputs
            keypad  = readPE(GPIOY);
            end_spi2_critical_section;
            // was there a keypress?
            if((keypad & no_button) != no_button) { break;}
        }
        
        // search for keycode
        if (keypad > 0){ // then button is pushed
            for (i=0; i<24; i++){
                if (keytable[i]==keypad) break;
            }
            // if invalid, two button push, set to -1
            if (i==24) i=-1;
        }
        else i = -1; // no button pushed

        // draw key number
        if (i>-1 && i<10) sprintf(buffer,"   %x %d", keypad, i);
        if (i==10 ) sprintf(buffer,"   %x *", keypad);
        if (i==11 ) sprintf(buffer,"   %x #", keypad);
        if (i>11 && i<22 ) sprintf(buffer, "   %x shift-%d", keypad, i-12);
        if (i==22 ) sprintf(buffer,"   %x ahift-*", keypad);
        if (i==23 ) sprintf(buffer,"   %x shift-#", keypad);
        if (i>-1 && i<12) printLine2(10, buffer, ILI9340_GREEN, ILI9340_BLACK);
        else if (i>-1) printLine2(10, buffer, ILI9340_RED, ILI9340_BLACK);
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
    // clk divider set to 4 for 10 MHz
    SpiChnOpen(SPI_CHANNEL2, SPI_OPEN_ON | SPI_OPEN_MODE16 | SPI_OPEN_MSTEN | SPI_OPEN_CKE_REV , 4);
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

