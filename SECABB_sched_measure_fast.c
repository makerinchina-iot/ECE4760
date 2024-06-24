/*
 * File:        Testing scheduler using
 *              serial interface to PuTTY console
 *              !!!Modified scheduler!!!
 *              NO TFT, port-expander, no LRD, BUT uses DAC ISR
 * 
 * Author:      Bruce Land
 * For use with Sean Carroll's Big Board
 * http://people.ece.cornell.edu/land/courses/ece4760/PIC32/target_board.html
 * Target PIC:  PIC32MX250F128B
 */

////////////////////////////////////
// clock AND protoThreads configure!
// You MUST check this file!
#include "config_1_3_0.h"
// threading library
#include "pt_cornell_1_3_0.h"
// yup, the expander
//#include "port_expander_brl4.h"

////////////////////////////////////
// graphics libraries
// SPI channel 1 connections to TFT
//#include "tft_master.h"
//#include "tft_gfx.h"
// need for rand function
#include <stdlib.h>
// need for sin function
#include <math.h>
////////////////////////////////////

// lock out timer 2 interrupt during spi comm to port expander
// This is necessary if you use the SPI2 channel in an ISR.
// The ISR below runs the DAC using SPI2
#define start_spi2_critical_section INTEnable(INT_T2, 0)
#define end_spi2_critical_section INTEnable(INT_T2, 1)

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
// DDS constant
#define two32 4294967296.0 // 2^32 
#define Fs 100000

//== Timer 2 interrupt handler ===========================================
// generates a DDS sinewave at a frequency set by the serial thread
// default frequency is 400 Hz.
//
volatile unsigned int DAC_data ;// output value
volatile SpiChannel spiChn = SPI_CHANNEL2 ;	// the SPI channel to use
volatile int spiClkDiv = 4 ; // 10 MHz max speed for port expander!!
// the DDS units:
volatile unsigned int phase_accum_main, phase_incr_main=400.0*two32/Fs ;//
// DDS sine table
#define sine_table_size 256
volatile int sin_table[sine_table_size];

void __ISR(_TIMER_2_VECTOR, ipl2) Timer2Handler(void)
{
    int junk;
    
    mT2ClearIntFlag();
    
    // main DDS phase and sine table lookup
    phase_accum_main += phase_incr_main  ;
    DAC_data = sin_table[phase_accum_main>>24]  ;
 
    // === Channel A =============
    // wait for possible port expander transactions to complete
    while (TxBufFullSPI2());
    // reset spi mode to avoid conflict with port expander
    SPI_Mode16();
    while (SPI2STATbits.SPIBUSY); // wait for end of transaction
    // CS low to start transaction
     mPORTBClearBits(BIT_4); // start transaction
    // write to spi2 
    WriteSPI2( DAC_config_chan_A | ((DAC_data + 2048) & 0xfff));
    while (SPI2STATbits.SPIBUSY); // wait for end of transaction
     // CS high
    mPORTBSetBits(BIT_4); // end transaction
    // need to read SPI channel to avoid confusing port expander
    junk = ReadSPI2(); 
   //    
}

//======================================================
// thread structures
// update counters in each thread
int count_thread1, count_thread2, count_thread3 ;
int count_total, last_count_total ;
// thread identifier to set thread parameters
int thread1_id, thread2_id, thread3_id;

// ======================================================
static PT_THREAD (protothread_t1(struct pt *pt))
{
    PT_BEGIN(pt);
      while(1) {
        // just yield as fast as possible
        PT_YIELD(pt) ;
        //PT_YIELD_TIME_msec(1);
        count_thread1++;
        //delay_ms(1);
        // NEVER exit while
      } // END WHILE(1)
  PT_END(pt);
} // t1 thread

// ======================================================
static PT_THREAD (protothread_t2(struct pt *pt))
{
    PT_BEGIN(pt);
      while(1) {
        // just yield as fast as possible
        PT_YIELD(pt) ;
        //PT_YIELD_TIME_msec(2);
        count_thread2++;
        //delay_ms(2);
        
        // NEVER exit while
      } // END WHILE(1)
  PT_END(pt);
} // t2 thread

// ======================================================
static PT_THREAD (protothread_t3(struct pt *pt))
{
    PT_BEGIN(pt);
      while(1) {
        // just yield as fast as possible
        PT_YIELD(pt) ;
        //PT_YIELD_TIME_msec(4);
        count_thread3++;
        //delay_ms(4);
        
        // NEVER exit while
      } // END WHILE(1)
  PT_END(pt);
} //  t3 thread


//=== Serial terminal thread =================================================
// simple command interpreter and serial display
// BUT NOTE that there are two completely different ways of getting strings
// from the UART. ONe assumes a human is typing, the other assumes a machine
//
// The following thread definitions are necessary for UART control
static struct pt pt_input, pt_output, pt_DMA_output ;
// A single serial thread to avoid contention for the device
static PT_THREAD (protothread_serial(struct pt *pt))
{
    PT_BEGIN(pt);
      static char cmd[30];
      static float value;
      crlf;
      while(1) {         
            // send the prompt via DMA to serial
         
            sprintf(PT_send_buffer,"cmd>");
            // by spawning a print thread
            PT_SPAWN(pt, &pt_DMA_output, PT_DMA_PutSerialBuffer(&pt_DMA_output) );
            // get the input and parse it
            PT_SPAWN(pt, &pt_input, PT_GetSerialBuffer(&pt_input) );
            sscanf(PT_term_buffer, "%s %f", cmd, &value);
            
            // command interpreter
             switch(cmd[0]){
                 case 'f': // set frequency of DAC sawtooth output
                     // enter frequecy in HZ
                    phase_incr_main = (int)(value*(float)two32/Fs);
                    sprintf(PT_send_buffer,"DDS freq = %6.1f", value);
                    // by spawning a print thread
                    PT_SPAWN(pt, &pt_DMA_output, PT_DMA_PutSerialBuffer(&pt_DMA_output) );
  
                    sprintf(PT_send_buffer,"DDS increment = %d\r\n", phase_incr_main);
                    PT_SPAWN(pt, &pt_DMA_output, PT_DMA_PutSerialBuffer(&pt_DMA_output) );
                     break;
                 
                 case 'c':
                     // zero the counters
                     count_thread1 = count_thread2 = count_thread3 = 0 ;
                     // wait a fixed length of time
                     PT_YIELD_TIME_msec(1000) ;
                     // sum the counters
                     count_total = count_thread1 + count_thread2 + count_thread3 ;
                    sprintf(PT_send_buffer,"counts 1,2,3,total %d %d %d %d \r\n", 
                            count_thread1, count_thread2, count_thread3, count_total);
                    // by spawning a print thread
                    PT_SPAWN(pt, &pt_DMA_output, PT_DMA_PutSerialBuffer(&pt_DMA_output) );                  
                    break;
                 
             }
             
            // never exit while
      } // END WHILE(1)
  PT_END(pt);
} // thread serial

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

    // control CS for DAC
    mPORTBSetPinsDigitalOut(BIT_4);
    mPORTBSetBits(BIT_4);
    // SCK2 is pin 26 
    // SDO2 (MOSI) is in PPS output group 2, could be connected to RB5 which is pin 14
    PPSOutput(2, RPB5, SDO2);
    // 16 bit transfer CKP=1 CKE=1
    // possibles SPI_OPEN_CKP_HIGH;   SPI_OPEN_SMP_END;  SPI_OPEN_CKE_REV
    // For any given peripherial, you will need to match these
    // NOTE!! IF you are using the port expander THEN
    // -- clk divider must be set to 4 for 10 MHz
    SpiChnOpen(SPI_CHANNEL2, SPI_OPEN_ON | SPI_OPEN_MODE16 | SPI_OPEN_MSTEN | SPI_OPEN_CKE_REV , 4);
  // end DAC setup
    
  // === build the sine lookup table =======
   // scaled to produce values between 0 and 4096
   int ii;
   for (ii = 0; ii < sine_table_size; ii++){
         sin_table[ii] = (int)(2047*sin((float)ii*6.283/(float)sine_table_size));
    }
   
   
  // === config threads ==========
  // turns OFF UART support and debugger pin, unless defines are set
  PT_setup();

  // === setup system wide interrupts  ========
  INTEnableSystemMultiVectoredInt();
  
  // add the thread function pointers to be scheduled
  // --- Two parameters: function_name and rate. ---
  // rate=0 fastest, rate=1 half, rate=2 quarter, rate=3 eighth, rate=4 sixteenth,
  // rate=5 or greater DISABLE thread!
  // If you need to access specific thread descriptors, return the list index
  thread3_id = pt_add(protothread_t3, 2);
  thread2_id = pt_add(protothread_t2, 1);
  thread1_id = pt_add(protothread_t1, 0);
  pt_add(protothread_serial, 2);

  // initalize the scheduler
  PT_INIT(&pt_sched) ;
  // CHOOSE the scheduler method:
  // SCHED_ROUND_ROBIN just cycles thru all defined threads
  // SCHED_RATE executes some threads more often then others
  // -- rate=0 fastest, rate=1 half, rate=2 quarter, rate=3 eighth, rate=4 sixteenth,
  // -- rate=5 or greater DISABLE thread!
  pt_sched_method = SCHED_ROUND_ROBIN ;
  //pt_sched_method = SCHED_RATE ;
  // scheduler never exits
  PT_SCHEDULE(protothread_sched(&pt_sched));
  
} // main

// === end  ======================================================

