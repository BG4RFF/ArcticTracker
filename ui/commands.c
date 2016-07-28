/* 
 * Command shell 
 * Use modified version of ChibiOS shell (see util/shell.c)
 */

#include "ch.h"
#include "hal.h"
#include "chprintf.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "radio.h"
#include "config.h"
#include "afsk.h"
#include "hdlc.h"
#include "string.h"
#include "defines.h"
#include "util/shell.h"
#include "adc_input.h"
#include "afsk.h"
#include "ui/ui.h"
#include "ui/commands.h"
#include "ui/text.h"
#include "ui/wifi.h"



#define SHELL_WA_SIZE   THD_WORKING_AREA_SIZE(1024)
extern SerialUSBDriver SDU1;
extern void mon_activate(bool);

static void cmd_mem(Stream *chp, int argc, char *argv[]);
static void cmd_threads(Stream *chp, int argc, char *argv[]);
static void cmd_setfreq(Stream *chp, int argc, char *argv[]);
static void cmd_setsquelch(Stream *chp, int argc, char *argv[]);
static void cmd_setmiclevel(Stream *chp, int argc, char *argv[]);
static void cmd_setvolume(Stream *chp, int argc, char *argv[]);
static void cmd_ptt(Stream *chp, int argc, char *argv[]);
static void cmd_radio(Stream *chp, int argc, char *argv[]);
static void cmd_txtone(Stream *chp, int argc, char *argv[]);
static void cmd_testpacket(Stream *chp, int argc, char *argv[]);
static void cmd_teston(Stream *chp, int argc, char* argv[]);
static void cmd_adc(Stream *chp, int argc, char* argv[]);
static void cmd_led(Stream *chp, int argc, char* argv[]);
static void cmd_listen(Stream *chp, int argc, char* argv[]);
static void cmd_converse(Stream *chp, int argc, char* argv[]);
static void cmd_txpower(Stream *chp, int argc, char *argv[]);
static void cmd_txdelay(Stream *chp, int argc, char *argv[]);
static void cmd_wifi(Stream *chp, int argc, char *argv[]);
static void cmd_mycall(Stream *chp, int argc, char *argv[]);
static void cmd_dest(Stream *chp, int argc, char *argv[]);
static void cmd_digipath(Stream *chp, int argc, char *argv[]);
static void cmd_ip(Stream *chp, int argc, char *argv[]);
static void cmd_macaddr(Stream *chp, int argc, char *argv[]);
static void cmd_symbol(Stream *chp, int argc, char* argv[]);

static void _parameter_setting_bool(Stream*, int, char**, uint16_t, const void*, char* );

#define CMD_BOOL_SETTING(x, name) \
   static inline void cmd_##x(Stream* out, int argc, char** argv) \
      { _parameter_setting_bool(out, argc, argv, x##_offset, &x##_default, name); }
      
CMD_BOOL_SETTING(TRACKER_ON,   "TRACKER");      
CMD_BOOL_SETTING(TIMESTAMP_ON, "TIMESTAMP");
CMD_BOOL_SETTING(COMPRESS_ON,  "COMPRESS");
CMD_BOOL_SETTING(ALTITUDE_ON,  "ALTITUDE");
CMD_BOOL_SETTING(REPORT_BEEP_ON,  "REPORTBEEP");

/*********************************************************************************
 * Shell config
 *********************************************************************************/

static const ShellCommand shell_commands[] = 
{
  { "mem",        "Memory status",                        3, cmd_mem },
  { "threads",    "Thread information",                   3, cmd_threads },
  { "freq",       "Set/get freguency of radio",           4, cmd_setfreq },
  { "squelch",    "Set/get squelch level of receiver",    2, cmd_setsquelch },
  { "volume",     "Set/get volume level of receiver",     3, cmd_setvolume },
  { "miclevel",   "Set mic sensitivity level (1-8)",      4, cmd_setmiclevel }, 
  { "txpower",    "Set TX power (hi=1W, lo=0.5W)",        4, cmd_txpower },
  { "txdelay",    "Set TX delay (flags)",                 3, cmd_txdelay },
  { "ptt",        "Turn on/off transmitter",              3, cmd_ptt },
  { "radio",      "Turn on/off radio",                    5, cmd_radio },
  { "txtone",     "Send 1200Hz (lo) or 2200Hz (hi) tone", 3, cmd_txtone },
  { "testpacket", "Send test APRS packet",                5, cmd_testpacket },
  { "teston",     "Generate test signal with data byte",  6, cmd_teston },
  { "adc",        "Get test samples from ADC",            3, cmd_adc },
  { "led",        "Test RGB LED",                         3, cmd_led },
  { "listen",     "Listen to radio",                      3, cmd_listen },
  { "converse",   "Converse mode",                        4, cmd_converse },
  { "wifi",       "Access ESP-12 WIFI module",            4, cmd_wifi },
  { "mycall",     "Set/get tracker's APRS callsign",      3, cmd_mycall },
  { "dest",       "Set/get APRS destination address",     3, cmd_dest },
  { "symbol",     "Set/get APRS symbol",                  3, cmd_symbol },
  { "digipath",   "Set/get APRS digipeater path",         5, cmd_digipath },  
  { "ip",         "Get IP address from WIFI module",      2, cmd_ip },
  { "macaddr",    "Get MAC address from WIFI module",     3, cmd_macaddr },
  { "tracker",    "Tracking on/off",                      4, cmd_TRACKER_ON },
  { "timestamp",  "Timestamp on/off",                     5, cmd_TIMESTAMP_ON },
  { "compress",   "Compressed positions on/off",          4, cmd_COMPRESS_ON },
  { "altitude",   "Altidude in reports on/off",           4, cmd_ALTITUDE_ON },
  { "reportbeep", "Beep when reporting on/off",          10, cmd_REPORT_BEEP_ON },
  
  {NULL, NULL, 0, NULL}
};



/* FIXME: Use of these buffers makes it unsafe to have multiple shell 
 * instances. We may protect them using a mutex 
 */ 
#define BUFSIZE 90
static char buf[BUFSIZE]; 
static ap_config_t wifiap;




/* 
 * Generic getter/setter method for boolean settings 
 */



static void _parameter_setting_bool(Stream* out, int argc, char** argv, 
                uint16_t ee_addr, const void* default_val, char* name )
{
    if (argc < 1) {
       chprintf(out, name);
       if (get_byte_param(ee_addr, default_val)) 
          chprintf(out," ON\r\n");
       else
          chprintf(out, " OFF\r\n");
       return; 
    }
    if (strncasecmp("on", argv[0], 2) == 0 || strncasecmp("true", argv[0], 1) == 0) {
       chprintf(out, "Ok\r\n");
       set_byte_param(ee_addr, (uint8_t) 1);
    }  
    else if (strncasecmp("off", argv[0], 2) == 0 || strncasecmp("false", argv[0], 1) == 0) {
       chprintf(out, "Ok\r\n");
       set_byte_param(ee_addr, (uint8_t) 0);
    }
    else 
       chprintf(out, "ERROR: parameter must be 'ON' or 'OFF'\r\n");
    
    
}



static const ShellConfig shell_cfg = {
  (Stream *)&SHELL_SERIAL,
  shell_commands
};


/**********************************************
 * Shell start
 **********************************************/

thread_t* myshell_start()
{  
    char line[10]; 
    chprintf(shell_cfg.sc_channel, "\r\n");
    shellGetLine(shell_cfg.sc_channel, line, sizeof(line));
    sleep(10);
    chprintf(shell_cfg.sc_channel, "\r\n\r\nWelcome to Polaric Hacker v. 2.0\r\n");   
    return shellCreate(&shell_cfg, SHELL_WA_SIZE, NORMALPRIO);
}



/****************************************************************************
 * readline from input stream. 
 * Typing ctrl-C will immediately return false
 ****************************************************************************/

bool readline(Stream * cbp, char* buf, const uint16_t max) {
  char x;
  uint16_t i=0; 
  
  for (i=0; i<max; i++) {
    x = streamGet(cbp);     
    if (x == 0x03)     /* CTRL-C */
      return false;
    if (x == '\r') {
      /* Get LINEFEED */
      streamGet(cbp); 
      break; 
    }
    if (x == '\n')
      break;
    buf[i]=x;
  }
  buf[i] = '\0';
  return true;
}


/****************************************************************************
 * Memory status
 ****************************************************************************/

static void cmd_mem(Stream *chp, int argc, char *argv[]) {
  
  (void)argv;
  if (argc > 0) {
    chprintf(chp, "Usage: mem\r\n");
    return;
  }  
  chprintf(chp, "core free memory : %u bytes\r\n", chCoreGetStatusX());
  chprintf(chp, "fbuf used slots  : %u\r\n", fbuf_usedSlots());
  chprintf(chp, "fbuf free slots  : %u\r\n", fbuf_freeSlots());
  chprintf(chp, "fbuf free total  : %u bytes\r\n", fbuf_freeMem());
}


/****************************************************************************
 * Thread information
 * (borrowed from ChibiOS code by Giovanni Di Sirio. 
 *  os/various/shell/shell_cmd.c)
 ****************************************************************************/

static void cmd_threads(Stream *chp, int argc, char *argv[]) {
  static const char *states[] = {CH_STATE_NAMES};
  thread_t *tp;
  
  (void)argv;
  if (argc > 0) {
    chprintf(chp, "Usage: threads\r\n");
    return;
  }
  chprintf(chp, "stklimit    stack     addr refs prio     state  name\r\n\r\n");
  tp = chRegFirstThread();
  do {
    #if (CH_DBG_ENABLE_STACK_CHECK == TRUE) || (CH_CFG_USE_DYNAMIC == TRUE)
    uint32_t stklimit = (uint32_t)tp->wabase;
    #else
    uint32_t stklimit = 0U;
    #endif
    chprintf(chp, "%08lx %08lx %08lx %4lu %4lu %9s  %s\r\n",
             stklimit, (uint32_t)tp->ctx.sp, (uint32_t)tp,
             (uint32_t)tp->refs - 1, (uint32_t)tp->prio, states[tp->state],
             tp->name == NULL ? "" : tp->name);
    tp = chRegNextThread(tp);
  } while (tp != NULL);
}





/****************************************************************************
 * Set/get freguency of radio
 ****************************************************************************/

static void cmd_setfreq(Stream *chp, int argc, char *argv[]) {
  uint32_t txf=0, rxf=0;
  if (argc == 0) {
     GET_PARAM(TRX_TX_FREQ, &txf);
     GET_PARAM(TRX_RX_FREQ, &rxf);
     chprintf(chp, "FREQUENCY: TX=%lu, RX=%lu\r\n", txf, rxf);
  }
  else {
     chprintf(chp, "%s\r\n", parseFreq(argv[0], buf, true));
     if (argc > 1)
        chprintf(chp, "%s\r\n", parseFreq(argv[0], buf, true));
     if (rxf==0)
        rxf = txf;
  }
}


/****************************************************************************
 * Set/get squelch level of receiver
 ****************************************************************************/

static void cmd_setsquelch(Stream *chp, int argc, char *argv[]) {
   uint8_t sq=0;
   if (argc == 0)
      sq = GET_BYTE_PARAM(TRX_SQUELCH);
   else {
      if (argc > 0) 
         sscanf(argv[0], "%hhu", &sq);
      if (sq>8) sq=8; 
      
      SET_BYTE_PARAM(TRX_SQUELCH, sq);
      radio_setSquelch(sq);
   }
   chprintf(chp, "SQUELCH: %d\r\n", sq); 
}



/****************************************************************************
 * Set/get volume level of receiver
 ****************************************************************************/

static void cmd_setmiclevel(Stream *chp, int argc, char *argv[]) {
  uint8_t vol=0;
  if (argc==0)
    chprintf(chp, "ERRROR\r\n");
  else {
    if (argc > 0) 
      sscanf(argv[0], "%hhu", &vol);
    if (vol>8) vol=8;
    radio_setMicLevel(vol);
  }
  chprintf(chp, "MICLEVEL: %d\r\n", vol);
}



/****************************************************************************
 * Set/get volume level of receiver
 ****************************************************************************/

static void cmd_setvolume(Stream *chp, int argc, char *argv[]) {
  uint8_t vol=0;
  if (argc==0)
     vol= GET_BYTE_PARAM(TRX_VOLUME);
  else {
     if (argc > 0) 
        sscanf(argv[0], "%hhu", &vol);
     if (vol>8) vol=8;
  
     SET_BYTE_PARAM(TRX_VOLUME, vol);
     radio_setVolume(vol);
  }
  chprintf(chp, "VOLUME: %d\r\n", vol);
}


/****************************************************************************
 * Set/get volume level of receiver
 ****************************************************************************/

static void cmd_txdelay(Stream *chp, int argc, char *argv[]) {
  uint8_t txd=0;
  if (argc == 0)
     txd = GET_BYTE_PARAM(TXDELAY);
  else {
     sscanf(argv[0], "%hhu", &txd);
     SET_BYTE_PARAM(TXDELAY, txd);
  }
  chprintf(chp, "TXDELAY: %d\r\n", txd); 
}



/****************************************************************************
 * Set/get TX power level (hi/lo)
 ****************************************************************************/

static void cmd_txpower(Stream *chp, int argc, char *argv[]) {
  if (argc==0)
     ; // TBD
  else {
     if (strncasecmp("hi", argv[0], 2) == 0) {
       chprintf(chp, "***** TX POWER HIGH *****\r\n");
       radio_setLowTxPower(false);
     }
     else if (strncasecmp("low", argv[0], 2) == 0) {
       chprintf(chp, "***** TX POWER LOW *****\r\n");
       radio_setLowTxPower(true);
     }     
  }
}


/****************************************************************************
 * Keyup transmitter
 ****************************************************************************/

static void cmd_ptt(Stream *chp, int argc, char *argv[]) {
    if (argc < 1) {
       chprintf(chp, "Usage: ptt on|off\r\n");
       return;
    }
    if (strncmp(argv[0], "on", 2) == 0)
       radio_PTT(true);
    else
       radio_PTT(false);
}


/****************************************************************************
 * Turn on/off radio
 ****************************************************************************/

static void cmd_radio(Stream *chp, int argc, char *argv[]) {
    if (argc < 1) {
       chprintf(chp, "Usage: radio on|off\r\n");
       return;
    } 
    if (strncmp(argv[0], "on", 2) == 0) 
       radio_on(true);
    else 
       radio_on(false);
}



/****************************************************************************
 * Send 1200Hz (lo) or 2200Hz (hi) tone
 ****************************************************************************/

static bool txtone_on = false; 
static void cmd_txtone(Stream *chp, int argc, char *argv[]) {
   if (argc < 1) {
     chprintf(chp, "Usage: tone high|low|off\r\n");
     return;
   } 

   if (strncasecmp("hi", argv[0], 2) == 0) {
      chprintf(chp, "***** TEST TONE HIGH *****\r\n");
      tone_setHigh(true);
      if (!txtone_on) {
         radio_PTT(true); 
         tone_start();
         txtone_on = true;
      } 
   }
   else if (strncasecmp("low", argv[0], 2) == 0) {
      chprintf(chp, "***** TEST TONE LOW *****\r\n");
      tone_setHigh(false);
      if (!txtone_on) {
         radio_PTT(true);
         tone_start();
         txtone_on = true;
      }
   }  
   else if (strncasecmp("off", argv[0], 2) == 0 && txtone_on) {
      radio_PTT(false);
      tone_stop();
      txtone_on = false; 
   }
}


/****************************************************************************
 * Send test APRS packet
 ****************************************************************************/

static void cmd_testpacket(Stream *chp, int argc, char *argv[]) 
{ 
  (void)argc;
  (void)argv;
  
  static FBUF packet;    
  fbq_t* outframes = hdlc_get_encoder_queue();
  addr_t from, to; 
  addr_t digis[7];
  
//  radio_require();; 
  GET_PARAM(MYCALL, &from);
  GET_PARAM(DEST, &to);       
  uint8_t ndigis = GET_BYTE_PARAM(NDIGIS); 
  GET_PARAM(DIGIS, &digis);   
  fbuf_new(&packet); 
  ax25_encode_header(&packet, &from, &to, digis, ndigis, FTYPE_UI, PID_NO_L3); 
  fbuf_putstr(&packet, "The lazy brown dog jumps over the quick fox 1234567890");                      
  chprintf(chp, "Sending (AX25 UI) test packet....\r\n");       
  fbq_put(outframes, packet); 
//  radio_release(); 
}


/****************************************************************************
 * Generate test signal with data byte
 ****************************************************************************/

static void cmd_teston(Stream *chp, int argc, char* argv[])
{
  int ch = 0;
  if (argc < 1) {
    chprintf(chp, "Usage: TESTON <byte>\r\n");
    return;
  }
  sscanf(argv[0], "%xhh", &ch);
//  radio_require();  
  hdlc_test_on((uint8_t) ch);
  chprintf(chp, "**** TEST SIGNAL: 0x%X ****\r\n", ch);
  
  /* And wait until some character has been typed */
  getch(chp);
  hdlc_test_off();
//  radio_release();
}



/****************************************************************************
 * Get test samples from ADC
 ****************************************************************************/

static void cmd_adc(Stream *chp, int argc, char* argv[])
{
   (void)argc;
   (void)argv;
   
   int32_t temp = adc_read_temp();
   chprintf(chp,"Temperature = %d\r\n", temp);
   int32_t inp = adc_read_input();
   chprintf(chp,"Input = %d\r\n", inp);
}



/****************************************************************************
 * Test RGB LED
 ****************************************************************************/

static void cmd_led(Stream *chp, int argc, char* argv[])
{
  if (argc < 4) {
    if (strncasecmp("off", argv[0], 2) == 0)
      rgb_led_off();
    else {
       chprintf(chp, "Usage: LED <R> <G> <B> <off>\r\n");
       chprintf(chp, "       LED OFF \r\n");
    }
    return;
  }
  int r,g,b,off;
  r=atoi(argv[0]);
  g=atoi(argv[1]);
  b=atoi(argv[2]);
  off=atoi(argv[3]);
  rgb_led_mix((uint8_t) r, (uint8_t) g, (uint8_t) b, (uint8_t) off);
}



/****************************************************************************
 * Listen on radio
 ****************************************************************************/

static void cmd_listen(Stream *chp, int argc, char* argv[])
{
  (void) argv;
  (void) argc; 
  
  afsk_rx_enable();
  mon_activate(true);
  getch(chp);
  mon_activate(false);
  afsk_rx_disable();
}



/*****************************************************************************
 * wifi module commands
 *****************************************************************************/

static void cmd_wifi(Stream *chp, int argc, char* argv[])
{
   if (argc < 1) {
      chprintf(chp, "Usage: wifi on|off|info|ap|shell\r\n");
      return;
   } 
   if (strncasecmp("info", argv[0], 3) == 0) {
      chprintf(chp, "    Stn status: %s\r\n",  wifi_status(buf));
      chprintf(chp, "  Connected to: %s\r\n",  wifi_doCommand("CONF", buf));
      chprintf(chp, "    IP address: %s\r\n",  wifi_doCommand("IP", buf));
      chprintf(chp, "   MAC address: %s\r\n",  wifi_doCommand("MAC", buf));
      
      chprintf(chp, "\r\n");
      chprintf(chp, "       AP SSID: %s\r\n",  wifi_doCommand("AP.SSID", buf));     
      chprintf(chp, " AP IP address: %s\r\n",  wifi_doCommand("AP.IP", buf));


      chprintf(chp, "\r\nConfigured access points:\r\n");
      for (int i=0; i<N_WIFIAP; i++) {
         GET_PARAM_I(WIFIAP, i, &wifiap);
         if (strlen(wifiap.ssid) == 0)
            chprintf(chp, " %d: -\r\n", i+1);
         else
            chprintf(chp," %d: %s : '%s'\r\n", i+1, wifiap.ssid, wifiap.passwd);
      }
   }
   else if (strncasecmp("ap", argv[0], 2) == 0) {
      if (argc < 2)
         chprintf(chp, "Usage: wifi ap <1-%d>\r\n", N_WIFIAP);
      else {
         int i = atoi(argv[1]);
         if (i < 1 || i > 4) {
            chprintf(chp, "Argument must be a number 1-4\r\n");
            return; 
         }
         chprintf(chp, "SSID: ");
         shellGetLine(chp, wifiap.ssid, 32);
         if (strlen(wifiap.ssid) > 0) {
            chprintf(chp, "Password: ");
            shellGetLine(chp, wifiap.passwd, 32);
         }
         if (strlen(wifiap.passwd) == 0)
            strcpy(wifiap.passwd, "_OPEN_");
         chprintf(chp, "Ok\r\n");
         SET_PARAM_I(WIFIAP, i-1, &wifiap);
      }
   }
   else if (strncasecmp("shell", argv[0], 2) == 0) {
     chprintf(chp, "***** WIFI DEVICE SHELL. Ctrl-D to exit *****\r\n");
     wifi_shell(chp);
   }
   else if (strncasecmp("on", argv[0], 2) == 0) { 
     chprintf(chp, "***** WIFI MODULE ON *****\r\n");
     wifi_enable();
   }
   else if (strncasecmp("off", argv[0], 2) == 0) {
     chprintf(chp, "***** WIFI MODULE OFF *****\r\n");
     wifi_disable();
   }
}



static void cmd_httpserver(Stream *chp, int argc, char* argv[])
{
   if (argc < 1) {
      chprintf(chp, "Usage: httpserver on|off|auth\r\n");
      return;
   }
}




/****************************************************************************
 * Converse mode
 ****************************************************************************/

static void cmd_converse(Stream *chp, int argc, char* argv[])
{
  (void) argc;
  (void) argv; 
  
  static FBUF packet; 
  chprintf(chp, "***** CONVERSE MODE. Ctrl-D to exit *****\r\n");
  afsk_rx_enable();
  mon_activate(true); 
  fbq_t* outframes = hdlc_get_encoder_queue();
  
  while (!shellGetLine(chp, buf, BUFSIZE)) { 
    addr_t from, to; 
    GET_PARAM(MYCALL, &from);
    GET_PARAM(DEST, &to);       
    addr_t digis[7];
    uint8_t ndigis = GET_BYTE_PARAM(NDIGIS); 
    GET_PARAM(DIGIS, &digis);  
    fbuf_new(&packet);
    ax25_encode_header(&packet, &from, &to, digis, ndigis, FTYPE_UI, PID_NO_L3);
    fbuf_putstr(&packet, buf);                        
    fbq_put(outframes, packet);
  }
  mon_activate(false);
  afsk_rx_disable(); 
}


/****************************************************************************
 * Show or set mycall
 ****************************************************************************/

static void cmd_mycall(Stream *chp, int argc, char *argv[]) 
{
   addr_t x;
   if (argc > 0) {
      str2addr(&x, argv[0], false);
      SET_PARAM(MYCALL, &x);
      chprintf(chp, "Ok\r\n");
   }
   else {
      GET_PARAM(MYCALL, &x);
      chprintf(chp, "MYCALL %s\r\n", addr2str(buf, &x));
   } 
}


/****************************************************************************
 * Show or set dest
 ****************************************************************************/

static void cmd_dest(Stream *chp, int argc, char *argv[]) 
{
  addr_t x;
  if (argc > 0) {
    str2addr(&x, argv[0], false);
    SET_PARAM(DEST, &x);
    chprintf(chp, "Ok\r\n");
  }
  else {
    GET_PARAM(DEST, &x);
    chprintf(chp, "DEST %s\r\n", addr2str(buf, &x));
  } 
}



/****************************************************************************
 * Show or set symbol
 ****************************************************************************/

static void cmd_symbol(Stream *chp, int argc, char* argv[])
{
   if (argc > 0)
     chprintf(chp, "%s\r\n", parseSymbol(argv[0], buf));
   else {
     char tab = GET_BYTE_PARAM(SYMBOL_TAB);
     char sym = GET_BYTE_PARAM(SYMBOL);
     chprintf(chp, "SYMBOL %c%c\r\n", tab,sym);
   }
}



/****************************************************************************
 * Show or set digipeater path
 ****************************************************************************/

static void cmd_digipath(Stream *chp, int argc, char *argv[])
{
    if (argc > 0) 
       chprintf(chp, "%s\r\n", parseDigipathTokens(argc, argv, buf));
    else  {
       __digilist_t digis;
       uint8_t ndigis;
       ndigis = GET_BYTE_PARAM(NDIGIS);
       GET_PARAM(DIGIS, &digis);
       digis2str(buf, ndigis, digis);
       chprintf(chp, "PATH %s\r\n", buf);
    } 
}




/****************************************************************************
 * Show IP address
 ****************************************************************************/

static void cmd_ip(Stream *chp, int argc, char *argv[])
{
  (void) argc;
  (void) argv; 
  
  chprintf(chp, "IP %s\r\n", wifi_doCommand("IP", buf));
}




/****************************************************************************
 * Show MAC address
 ****************************************************************************/

static void cmd_macaddr(Stream *chp, int argc, char *argv[])
{
  (void) argc;
  (void) argv; 
  
  chprintf(chp, "MAC %s\r\n", wifi_doCommand("MAC", buf));
}
