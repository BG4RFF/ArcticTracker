 #if !defined __DEFINES_H__
 #define __DEFINES_H__
 
#include <inttypes.h>
 
 
/* Application configuration macros */
   
/* Conversions */
#define KNOTS2KMH 1.853
#define KNOTS2MPS 0.5148
#define FEET2M 3.2898


/* Buffers */
#define FBUF_SLOTS 512
#define FBUF_SLOTSIZE 32


/* ADC ports for Teensy 3.1 */
#define ADC_TEENSY_PIN10 ADC_DAD0
#define ADC_TEENSY_PIN11 ADC_DAD1


/* Queues for AFSK encoder/decoder */
#define AFSK_RX_QUEUE_SIZE      128
#define AFSK_TX_QUEUE_SIZE      128
#define HDLC_DECODER_QUEUE_SIZE   8
#define HDLC_ENCODER_QUEUE_SIZE   8

/* Hardware timers */
#define AFSK_RX_GPT      GPTD4
#define AFSK_TX_GPT      GPTD2
#define AFSK_TONEGEN_GPT GPTD3
#define BUZZER_GPT       GPTD1


/* Serial ports */
#define SHELL_SERIAL   SDU1
#define WIFI_SERIAL    SD1
#define GPS_SERIAL     SD2
#define TRX_SERIAL     SD3


/* Radio transceiver module */
/* 0 = 12.5 KHz, 1 = 25 KHz */
#define TRX_BANDWIDTH  0
#define TRX_PTT_IOPORT   TEENSY_PIN6_IOPORT
#define TRX_PTT          TEENSY_PIN6
#define TRX_PD_IOPORT    TEENSY_PIN5_IOPORT  
#define TRX_PD           TEENSY_PIN5
#define TRX_SQ_IOPORT    TEENSY_PIN19_IOPORT
#define TRX_SQ           TEENSY_PIN19
#define TRX_SQ_MODE      PAL_MODE_INPUT
#define TRX_SQ_EXTCFG    {EXT_CH_MODE_BOTH_EDGES, trx_sq_handler, PORTB, TRX_SQ}


/* RGB LED */
#define LED_R            TEENSY_PIN2
#define LED_G            TEENSY_PIN3
#define LED_B            TEENSY_PIN4
#define LED_R_IOPORT     TEENSY_PIN2_IOPORT
#define LED_G_IOPORT     TEENSY_PIN3_IOPORT
#define LED_B_IOPORT     TEENSY_PIN4_IOPORT


/* DCD LED */
#define LED_DCD          TEENSY_PIN17       
#define LED_DCD_IOPORT   TEENSY_PIN17_IOPORT

/* Buzzer */
#define BUZZER           TEENSY_PIN18
#define BUZZER_IOPORT    TEENSY_PIN18_IOPORT

/* Pushbutton */
#define BUTTON           TEENSY_PIN16
#define BUTTON_IOPORT    TEENSY_PIN16_IOPORT
#define BUTTON_MODE      PAL_MODE_INPUT_PULLUP
#define BUTTON_EXTCFG    {EXT_CH_MODE_FALLING_EDGE, button_handler, PORTB, BUTTON}


/* LED blinking */
extern uint16_t blink_length, blink_interval;
#define BLINK_NORMAL        { blink_length = 50; blink_interval = 1950; }
#define BLINK_GPS_SEARCHING { blink_length = 450; blink_interval = 450; }



#define THREAD_STACK(n, st)  static THD_WORKING_AREA(wa_##n, st)
#define THREAD_START(n, prio, arg) chThdCreateStatic(wa_##n, sizeof(wa_##n), (prio), n, arg)
#define THREAD_DSTART(n, name, size, prio, arg) \
   chThdCreateFromHeap(NULL, THD_WORKING_AREA_SIZE((size)), (name), (prio), n, arg)
   
#define sleep(n)  chThdSleepMilliseconds(n)
#define t_yield   chThdYield

#define Stream BaseSequentialStream

#define putch(s, ch) streamPut(s, ch)
#define getch(s) streamGet(s)

#define pinIsHigh(x)        (palReadPad(x##_IOPORT, x)==PAL_HIGH)
#define setPin(x)           palSetPad(x##_IOPORT, x)
#define clearPin(x)         palClearPad(x##_IOPORT, x)
#define togglePin(x)        palTogglePad(x##_IOPORT, x)
#define setPinMode(x, mode) palSetPadMode(x##_IOPORT, x, mode)

#endif