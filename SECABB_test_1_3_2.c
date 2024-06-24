/*
 * File:        TFT, keypad, DAC, LED, PORT EXPANDER test
 *              With serial interface to PuTTY console
 *              DMA GetMachineBuffer
 *              !!!Modified scheduler!!!
 * 
 * Author:      Bruce Land
 * For use with Sean Carroll's Big Board
 * http://people.ece.cornell.edu/land/courses/ece4760/PIC32/target_board.html
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
// need for sin function
#include <math.h>
////////////////////////////////////

// lock out timer 2 interrupt during spi communication to port expander
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
//char buffer[60];

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

// === print a line on TFT =====================================================
// Utilities to print a line on the TFT
// Predefined colors definitions (from tft_master.h)
//#define	ILI9340_BLACK   0x0000
//#define	ILI9340_BLUE    0x001F
//#define	ILI9340_RED     0xF800
//#define	ILI9340_GREEN   0x07E0
//#define ILI9340_CYAN    0x07FF
//#define ILI9340_MAGENTA 0xF81F
//#define ILI9340_YELLOW  0xFFE0
//#define ILI9340_WHITE   0xFFFF

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

// === Timer Thread =================================================
// system 1 second interval tick
// prints on TFT and blinks LED on RA0
int sys_time_seconds ;
// thread identifier to set thread parameters
int thread_num_timer;

// update a 1 second tick counter
static PT_THREAD (protothread_timer(struct pt *pt))
{
    PT_BEGIN(pt);
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
        sprintf(buffer,"Time=%d", sys_time_seconds);
        printLine2(0, buffer, ILI9340_BLACK, ILI9340_YELLOW);
        // NEVER exit while
      } // END WHILE(1)
  PT_END(pt);
} // timer thread

// === Keypad Thread =============================================
// Decodes a keypad attached to the port expander, including a shift KEY.
//
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
    static int keypad, i ;
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
    
    // Turn off the T2 ISR for a few microseconds
    start_spi2_critical_section;
    initPE();
    // PortY on Expander ports as digital outputs
    mPortYSetPinsOut(BIT_0 | BIT_1 | BIT_2 | BIT_3);    //Set port as output
    // PortY as inputs
    // note that bit 7 will be shift key input, 
    // separate from keypad
    mPortYSetPinsIn(BIT_4 | BIT_5 | BIT_6 | BIT_7);    //Set port as input
    mPortYEnablePullUp(BIT_4 | BIT_5 | BIT_6 | BIT_7); 
    end_spi2_critical_section ;
    
    // the read-pattern if no button is pulled down by an output
    #define no_button (0x70)

      while(1) {
        // yield time for a keypad should be around 30 mSec
        PT_YIELD_TIME_msec(30);
        
        // stepping thru the 4 rows of the keypad
        for (i=0; i<4; i++) {
            
            start_spi2_critical_section ;
            // scan each row active-low
            writePE(GPIOY, out_table[i]);
            //reading the port also reads the outputs
            keypad  = readPE(GPIOY);
            end_spi2_critical_section ;
            
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

//=== Serial terminal thread =================================================
// simple command interpreter and serial display
// BUT NOTE that there are TWO completely different modes of getting strings
// from the UART. ONe assumes a human is typing, the other assumes a machine
//
// The following thread definitions are necessary for UART control
// and are defined in the protothreads header
//static struct pt pt_input, pt_output, pt_DMA_output, pt_DMA_input ;

// A single serial thread to avoid contention for the device

static PT_THREAD (protothread_serial(struct pt *pt))
{
    PT_BEGIN(pt);
      static char cmd[30], t0;
      static float value;
      static int i;
      static int mode = 1;
      static int v1, v2;
      
      while(1) {  
            // === send string to UART ==============
            // send the prompt via DMA to serial
            
            sprintf(PT_send_buffer,"\r\ncmd>");
            // by spawning a print thread
            PT_SPAWN(pt, &pt_DMA_output, PT_DMA_PutSerialBuffer(&pt_DMA_output) );
           
            // === get string from UART =============
            //spawn a thread to handle terminal input
            // the input thread waits for input
            // -- BUT does NOT block other threads
            // string is returned in "PT_term_buffer"
            // !!!! --- !!!!
            // NOTE:
            // USE ONE of the following:
            //  (1) PT_GetSerialBuffer -- Meant for human typing at a terminal
            //  (2) PT_GetMachineBuffer -- Meant for machine communication
            // !!!! --- !!!!
            // === GetSerialBuffer =======================
            if (mode == 1){
                PT_SPAWN(pt, &pt_input, PT_GetSerialBuffer(&pt_input) );
            }
            //
            // === GetMachine Buffer =====================
            // The machine version is more complex:
            // PT_GetMachineBuffer assumes a input
            // from a module, like GPS or Bluetooth or another CPU
            // It does NOT echo.
            //
            // It REQUIRES a termination scheme:
            // Terminate on count, character value, or time out
            // Or any combination of these by setting three variables.
            //
            // Terminate on <enter> key
             //PT_terminate_char = '\r' ;
             //PT_terminate_count = 0 ;
             // time in milliseconds!
             //PT_terminate_time = 0 ;
            // ---
            // Terminate on '#' character or 10 characters
            // PT_terminate_char = '#' ;
            // PT_terminate_count = 10 ;
            // PT_terminate_time = 0 ;
            // ---
            // Terminate after exactly 5 characters
            // or 
            // Terminate after 5 seconds
            //PT_terminate_char = 0 ; 
            //PT_terminate_count = 5 ; 
            //PT_terminate_time = 5000 ;
            
            // =============================================
            if (mode == 2){
            // >>> note that there will NO visual feedback using the following function <<<<
                PT_SPAWN(pt, &pt_DMA_input, PT_GetMachineBuffer(&pt_DMA_input) );
            }
            // =============================================
            
            // test raw characters
            //for (i=0; i<6; i++) printf("%02x\r\n",PT_term_buffer[i]);
            
            // === check for time out =======================
            // returns when the read thread dies on the termination condition:
            // IF using PT_GetSerialBuffer, when <enter> is pushed
            // IF using PT_GetMachineBuffer, could be on timeout => no valid string
             if(PT_timeout==0) {
                 sscanf(PT_term_buffer, "%s %f", cmd, &value);
             }
            // no actual string
             else {
                 // timed out. No valid data. So,
                 // disable the command parser below
                 cmd[0] = 0 ;
             }
            // ==============================================
            // At this point, we have a valid string buffer
            // Now parse it:
            
            // === command parser ================================
             switch(cmd[0]){
                 case 'f': // set frequency of DAC sawtooth output
                     // enter frequecy in HZ
                    phase_incr_main = (int)(value*(float)two32/Fs);
                    sprintf(PT_send_buffer,"DDS freq = %6.1f", value);
                    // by spawning a print thread
                    PT_SPAWN(pt, &pt_DMA_output, PT_DMA_PutSerialBuffer(&pt_DMA_output) );
                 
                    sprintf(PT_send_buffer,"DDS increment = %d", phase_incr_main);
                    PT_SPAWN(pt, &pt_DMA_output, PT_DMA_PutSerialBuffer(&pt_DMA_output) );
                     break;
                 
                 case 't':
                    sscanf(PT_term_buffer, "%s %d %d %d", cmd, &t0, &v1, &v2);
                    PT_terminate_char = t0 ; // see  http://www.asciitable.com/
                    PT_terminate_count = v1 ;
                    // time in milliseconds!
                    PT_terminate_time = v2 ;
                    break;
                 
                case 'm':
                    // toggle the input mode between 'human' and 'machine'
                    if (mode == 1) {
                        mode=2;
                        sprintf(PT_send_buffer,"Machine input -- no echo");
                    }
                    else {
                        mode = 1;
                        sprintf(PT_send_buffer,"\r\nHuman input");
                    }
                    // spawn a print thread
                    PT_SPAWN(pt, &pt_DMA_output, PT_DMA_PutSerialBuffer(&pt_DMA_output) );
                    break;
             }
             
            // never exit while
      } // END WHILE(1)
  PT_END(pt);
} // thread serial

// === Main  ======================================================

void main(void) {
  
  // === set up timer for DAC ISR =====================
  // timer interrupt //////////////////////////
    // Set up timer2 on,  interrupts, internal clock, prescalar 1, toggle rate
    // at 30 MHz PB clock 60 counts is two microsec
    // 400 is 100 ksamples/sec
    // 2000 is 20 ksamp/sec
    OpenTimer2(T2_ON | T2_SOURCE_INT | T2_PS_1_1, 400);

    // set up the timer interrupt with a priority of 2
    ConfigIntTimer2(T2_INT_ON | T2_INT_PRIOR_2);
    mT2ClearIntFlag(); // and clear the interrupt flag
    
    // === SPI channel for DAC and Expander ============
    // control CS for DAC
    mPORTBSetPinsDigitalOut(BIT_4);
    mPORTBSetBits(BIT_4);
    // SCK2 is on pin RB15 
    // SDO2 (MOSI) is in PPS output group 2, and could be connected to RB5 which is pin 14
    PPSOutput(2, RPB5, SDO2);
    // 16 bit transfer CKP=1 CKE=1
    // possibles SPI_OPEN_CKP_HIGH;   SPI_OPEN_SMP_END;  SPI_OPEN_CKE_REV
    // For any given peripherial, you will need to match these
    // NOTE!! IF you are using the port expander THEN
    // >>> clk divider must be set to 4 for 10 MHz <<<
    SpiChnOpen(SPI_CHANNEL2, SPI_OPEN_ON | SPI_OPEN_MODE16 | SPI_OPEN_MSTEN | SPI_OPEN_CKE_REV , 4);
  // end SPI setup
    
  // === build the DDS sine lookup table =======
   // scaled to produce values between 0 and 4096
   int ii;
   for (ii = 0; ii < sine_table_size; ii++){
         sin_table[ii] = (int)(2047*sin((float)ii*6.283/(float)sine_table_size));
    }
    
  // === setup system wide interrupts  ========
  INTEnableSystemMultiVectoredInt();
  
  // === TFT setup ============================
  // init the display in main since more than one thread uses it.
  // NOTE that this init assumes SPI channel 1 connections
  tft_init_hw();
  tft_begin();
  tft_fillScreen(ILI9340_BLACK);
  //240x320 vertical display
  tft_setRotation(0); // Use tft_setRotation(1) for 320x240
  
  // === config threads ========================
  PT_setup();
  
  // === identify the threads to the scheduler =====
  // add the thread function pointers to be scheduled
  // --- Two parameters: function_name and rate. ---
  // rate=0 fastest, rate=1 half, rate=2 quarter, rate=3 eighth, rate=4 sixteenth,
  // rate=5 or greater DISABLE thread!
  // If you need to access specific thread descriptors (to change rate), 
  // then return the list index
  thread_num_timer = pt_add(protothread_timer, 0);
  pt_add(protothread_key, 1);
  pt_add(protothread_serial, 1);

  // === initalize the scheduler ====================
  PT_INIT(&pt_sched) ;
  // >>> CHOOSE the scheduler method: <<<
  // (1)
  // SCHED_ROUND_ROBIN just cycles thru all defined threads
  //pt_sched_method = SCHED_ROUND_ROBIN ;
  
  // (2)
  // SCHED_RATE executes some threads more often then others
  // -- rate=0 fastest, rate=1 half, rate=2 quarter, rate=3 eighth, rate=4 sixteenth,
  // -- rate=5 or greater DISABLE thread!
  // pt_sched_method = SCHED_RATE ;
  
  pt_sched_method = SCHED_ROUND_ROBIN ;
  
  // === scheduler thread =======================
  // scheduler never exits
  PT_SCHEDULE(protothread_sched(&pt_sched));
  // ============================================
  
} // main

// === end  ======================================================

