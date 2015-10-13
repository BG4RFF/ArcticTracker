
#include <stdio.h>
#include "ch.h"
#include "hal.h"
#include "chprintf.h"
#include "defines.h"
#include "util/eeprom.h"
#include "config.h"
#include "commands.h"
#include "radio.h"

#define FLAG_BUSY_LOCK  0x01
#define FLAG_COMP_EXP   0x02
#define FLAG_LO_POWER   0x04


static bool     _on = FALSE; 
static uint8_t  _widebw; 
static uint8_t  _flags;        
static uint32_t _txfreq;       // TX frequency in 100 Hz units
static uint32_t _rxfreq;       // RX frequency in 100 Hz units
static uint8_t  _squelch;      // Squelch level (0-8 where 0 is open)
static Stream*  _serial;
  
  
/* 0 = 12.5 KHz, 1 = 25 KHz */
#define RADIO_BW 0
  
  
// Move to config/header file
#define PTT_PORT  IOPORT4
#define PTT_PIN   PORTD_TEENSY_PIN6
#define PD_PORT   IOPORT4  
#define PD_PIN    PORTD_TEENSY_PIN5
#define HL_PORT   IOPORT1
#define HL_PIN    PORTA_TEENSY_PIN4

  
static bool _handshake(void);
static bool _setGroupParm(void);
static void _initialize(void);

 
static const SerialConfig _serialConfig = {
   9600
};


// Stream* shell = (Stream*) &SHELL_SERIAL; 

/***********************************************
 * Initiialize
 ***********************************************/

void radio_init(SerialDriver* sd)
{ return; 
   _serial = (Stream*) sd;
   palSetPadMode(IOPORT4, PORTD_TEENSY_PIN7, PAL_MODE_ALTERNATIVE_3);
   palSetPadMode(IOPORT4, PORTD_TEENSY_PIN8, PAL_MODE_ALTERNATIVE_3);
   palSetPad(PTT_PORT, PTT_PIN);
   sdStart(sd, &_serialConfig);   
   radio_on(true);  
}
  
  
static void _initialize()
{  
   radio_PTT(false);
   sleep(1600);
   _handshake();
   sleep (100);
  
   /* Get parameters from EEPROM */
   GET_PARAM(TRX_TX_FREQ, &_txfreq);
   GET_PARAM(TRX_RX_FREQ, &_rxfreq);
   _squelch = GET_BYTE_PARAM(TRX_SQUELCH);
//  _setGroupParm();
}
  
  
  
/***********************************************
 * Set TX and RX frequency (100 Hz units)
 ***********************************************/
  
bool radio_setFreq(uint32_t txfreq, uint32_t rxfreq)
{
    _txfreq = txfreq;
    _rxfreq = rxfreq;
    return _setGroupParm();
}
 
 
/***********************************************
 * Set squelch level (1-8)
 ***********************************************/

bool radio_setSquelch(uint8_t sq) 
{
    _squelch = sq;
    if (_squelch > 8)
       _squelch = 0; 
    return _setGroupParm();
}


/************************************************
 * Power on
 ************************************************/

void radio_on(bool on)
{
   if (on == _on)
      return; 
   if (on) {
      palSetPad(PD_PORT, PD_PIN);
      _initialize();
   }
   else
      palClearPad(PD_PORT, PD_PIN);
   _on = on; 
}


/************************************************
 * PTT 
 ************************************************/

void radio_PTT(bool on)
{
    if (!_on)
      return;
    if (on)
      palClearPad(PTT_PORT, PTT_PIN);
    else
      palSetPad(PTT_PORT, PTT_PIN);
}


/************************************************
 * Set receiver volume (1-8)
 ************************************************/

bool radio_setVolume(uint8_t vol)
{
   if (!_on)
      return true;
   char reply[16];
   if (vol > 8)
      vol = 8;
   chprintf(_serial, "AT+DMOSETVOLUME=%1d\r\n", vol);
   readline(_serial, reply, 16);
//   chprintf(shell, "REPLY='%s'\r\n", reply);
   return (reply[13] == '0');
}


/************************************************
 * Set mic sensitivity (1-8)
 ************************************************/

bool radio_setMicLevel(uint8_t level)
{
  if (!_on)
    return true;
  if (level > 8)
    level = 8;
  char reply[16];
  chprintf(_serial, "AT+DMOSETMIC=%1d,0\r\n", level);
  readline(_serial, reply, 16);
  return (reply[13] == '0');
}


/************************************************
 * Auto powersave on/off. 
 ************************************************/

bool radio_powerSave(bool on)
{
   if (!_on)
      return true;
   char reply[16];
   chprintf(_serial, "AT+DMOAUTOPOWCONTR=%1d\r\n", (on ? 1:0));
   readline(_serial, reply, 16);
   return (reply[13] == '0');
}


static bool _handshake()
{
   char reply[16];
   chprintf(_serial, "AT+DMOCONNECT\r\n");
   readline(_serial, reply, 16);
//   chprintf(shell, "REPLY='%s'\r\n", reply);
   return (reply[14] == '0');
}



static bool _setGroupParm()
{
   if (!_on)
      return true;
   char txbuf[16], rxbuf[16], reply[16];
   palSetPad(IOPORT3, PORTC_TEENSY_PIN13);
   sprintf(txbuf, "%lu.%04lu", _txfreq/10000, _txfreq%10000);
   sprintf(rxbuf, "%lu.%04lu", _rxfreq/10000, _rxfreq%10000);

   chprintf(_serial, "AT+DMOSETGROUP=%1d,%s,%s,0000,%1d,0000,%1d\r\n",
            _widebw, txbuf, rxbuf, _squelch, _flags);
   readline(_serial, reply, 16);
//   chprintf(shell, "REPLY='%s'\r\n", reply);
   return (reply[15] == '0');
}
