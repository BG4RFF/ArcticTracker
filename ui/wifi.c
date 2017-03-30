#include "ch.h"
#include "hal.h"
#include "defines.h"
#include "config.h"
#include "chprintf.h"
#include <stdio.h>
#include <stdlib.h>
#include "string.h"
#include "util/shell.h"
#include "commands.h"
#include "text.h"
#include "wifi.h"


// static bool mon_on = false;
static Stream*  _serial;
static Stream*  _shell = NULL;
static bool wifiEnabled = false;
static bool startUp = true; 

static void wifi_command(void);
static void cmd_getParm(char* p);
static void cmd_setParm(char* p, char* val);
static void wifi_start_server(bool);
char* parseFreq(char* val, char* buf, bool tx);

extern FBQ* mon_text_activate(bool m);


MUTEX_DECL(wifi_mutex);
#define MUTEX_LOCK chMtxLock(&wifi_mutex)
#define MUTEX_UNLOCK chMtxUnlock(&wifi_mutex)

MUTEX_DECL(data_mutex);
#define DMUTEX_LOCK chMtxLock(&data_mutex)
#define DMUTEX_UNLOCK chMtxUnlock(&data_mutex)

BSEMAPHORE_DECL(response_pending, true);
#define WAIT_RESPONSE chBSemWait(&response_pending)
#define SIGNAL_RESPONSE chBSemSignal(&response_pending)

BSEMAPHORE_DECL(data_pending, true);
#define WAIT_DATA chBSemWait(&data_pending)
#define SIGNAL_DATA chBSemSignal(&data_pending)


THREAD_STACK(wifi_monitor, STACK_WIFI);


static const SerialConfig _serialConfig = {
  115200
};



/* FIXME: Should check thread safety when using this */
static char cbuf[129]; 
static bool _running = false; 


void wifi_enable() {
   if (!wifiEnabled) {
      if (!startUp) 
        { beeps(".-- "); blipUp(); }
      startUp = false;
      wifiEnabled = true;
      SET_BYTE_PARAM(WIFI_ON, 1);
      setPin(WIFI_ENABLE);
      sleep(2000);
   }
}

void wifi_disable() {
  if (wifiEnabled) {
      if (igate_is_on()) {
          igate_on(false);
          sleep(3000);
      }
      beeps(".-- "); blipDown();
      clearPin(WIFI_ENABLE);
      wifiEnabled = false; 
      SET_BYTE_PARAM(WIFI_ON, 0);
  }
}


bool wifi_is_enabled() {
  return wifiEnabled;
}


void wifi_on(bool on) {
    if (on) wifi_enable();
    else wifi_disable();
}


void wifi_restart() {
  if (wifiEnabled) {
    wifi_disable();
    sleep(100);
    wifi_enable(); 
  }
}


void wifi_external() {
  setPinMode(WIFI_SERIAL_RXD, PAL_MODE_UNCONNECTED);
  setPinMode(WIFI_SERIAL_TXD, PAL_MODE_UNCONNECTED);
}

void wifi_internal() {
  setPinMode(WIFI_SERIAL_RXD, PAL_MODE_ALTERNATIVE_3);
  setPinMode(WIFI_SERIAL_TXD, PAL_MODE_ALTERNATIVE_3);
}


/***************************************************************
 * Start (or resume) server on WIFI module
 ***************************************************************/

static void wifi_start_server(bool boot) {
  sleep(200);
  if (boot) {
     chprintf(_serial, "SHELL=1\r");
     sleep(300);
     addr_t call;
     char uname[32], passwd[32];
     GET_PARAM(HTTP_USER, uname);
     GET_PARAM(HTTP_PASSWD, passwd);
     if (GET_BYTE_PARAM(HTTP_ON))
        chprintf(_serial, "start_http_server('%s','%s')\r", uname, passwd);
     sleep(100);
  
     GET_PARAM(MYCALL, &call);
     GET_PARAM(SOFTAP_PASSWD, passwd);
     chprintf(_serial, "start_softap('Arctic-%s', '%s')\r", addr2str(uname, &call), passwd);
     sleep(100);
  } 
  chprintf(_serial, "coroutine.resume(listener)\r");
  sleep(100);
  _running = true; 
}



/***************************************************************
 * Invoke command on WIFI module
 ***************************************************************/

static char** client_buf;
static bool client_active = false; 


char* wifi_doCommand(char* cmd, char* buf) {
  if (wifi_is_enabled()) {
     MUTEX_LOCK;
     chprintf(_serial, "%s\r", cmd);
     client_active=true;
     client_buf = &buf; 
     WAIT_RESPONSE;
     client_active=false;
     MUTEX_UNLOCK;
  }
  else
    sprintf(buf, "-");
  return buf; 
}


char* wifi_status(char* buf) {
   char res[8];
   int n;
   if (!wifi_is_enabled()) {
       strcpy(buf, "Disabled");
       return buf;
   }
   wifi_doCommand("STATUS", res);
   n = atoi(res);
   switch(n) {
     case 0: strcpy(buf, "Idle"); break;
     case 1: strcpy(buf, "Connecting.."); break;
     case 2: strcpy(buf, "Wrong password"); break;
     case 3: strcpy(buf, "AP not found"); break;
     case 4: strcpy(buf, "Failed"); break;
     case 5: strcpy(buf, "Connected ok"); break;
   }
   return buf;
}
  
bool wifi_is_connected() {
   char res[8]; 
   int n; 
   wifi_doCommand("STATUS", res);
   n = atoi(res);
   return (n == 5);
}

  
  
/*************************************************************
 * Open internet connection
 *************************************************************/

static bool inet_connected = false;
static bool read_disable = false;
static FBQ* mon_queue;
static FBQ  read_queue;
static char chost[40];


char* inet_chost()
  {return chost; }

  
int inet_open(char* host, int port) {
  char res[10];
  sprintf(chost, "%s:%d", host, port);
  sprintf(cbuf, "NET.OPEN %d %s", port, host);
  wifi_doCommand(cbuf, res);
  if (strncmp("OK", res, 2) != 0) 
      return atoi(res+6);
  inet_connected = true;
  return 0; 
}


void inet_close() {
   if (!inet_connected)
      return; 
   char res[10];
   sprintf(chost, "");
   sprintf(cbuf, "NET.CLOSE");
   wifi_doCommand(cbuf, res);
   /*
    * FIXME: Do we need to call fbq_clear? If so, be sure 
    * that no other tread is blocking on queue 
    */
}

bool inet_is_connected()
  { return inet_connected; }

  
void inet_mon_on(bool on) {
   mon_queue = mon_text_activate(on);
}


void inet_disable_read(bool on) {
  read_disable = on; 
}



void inet_write(char* text) {
   char res[10];
   sprintf(cbuf, "NET.DATA %s", text);
   wifi_doCommand(cbuf, res);
}


/* FIXME: Could this be done more efficiently */

void inet_writeFB(FBUF *fb) {
  char res[10];
  sprintf(cbuf, "NET.DATA ");
  fbuf_read(fb, 128, cbuf+9);
  wifi_doCommand(cbuf, res);
}


int inet_read(char* buf) {
   if (!inet_connected)
      return 0;
   FBUF b = fbq_get(&read_queue);
   int len = fbuf_length(&b);
   fbuf_reset(&b);
   fbuf_read(&b, len, buf);
   fbuf_release(&b); 
   return len;
}


FBUF inet_readFB() {
  return fbq_get(&read_queue);
}


void inet_signalReader() {
  fbq_signal(&read_queue);
}


void inet_ignoreInput() {
   if (!inet_connected)
      return;
   FBUF b = fbq_get(&read_queue);
   fbuf_release(&b);
}



/**************************************************************
 * Connect to shell on WIFI module
 **************************************************************/

void wifi_shell(Stream* chp) {
  bool was_enabled = wifi_is_enabled();
  wifi_enable();
  
  MUTEX_LOCK;
  sleep(50);
  chprintf(_serial, "SHELL=1\r\r");
  _shell = chp;
  while (true) { 
    char c; 
    if (streamRead(chp, (uint8_t *)&c, 1) == 0)
      break;
    if (c == 4) {
      chprintf(chp, "^D");
      break;
    }
    streamPut(_serial, c);
  }
  _shell = NULL;
  chprintf(_serial, "\r");
  wifi_start_server(false);
  MUTEX_UNLOCK;
  
  if (!was_enabled)
      wifi_disable();
}



/*****************************************************************
 * Process commands coming from WIFI module that read parameters
 *****************************************************************/

static void cmd_getParm(char* p) { 
   if (strcmp("MYCALL", p) == 0) {
      addr_t x;
      GET_PARAM(MYCALL, &x);
      chprintf(_serial, "%s\r", addr2str(cbuf, &x)); 
   }
   else if (strcmp("DEST", p) == 0) {
      addr_t x;
      GET_PARAM(DEST, &x);
      chprintf(_serial, "%s\r", addr2str(cbuf, &x)); 
   }
   else if (strcmp("DIGIS", p) == 0) {
      __digilist_t digis;
      GET_PARAM(DIGIS, &digis);
      uint8_t n = GET_BYTE_PARAM(NDIGIS);
      chprintf(_serial, "%s\r", digis2str(cbuf, n, digis)); 
   }
   else if (strcmp("REPORT_COMMENT", p) == 0) {
      GET_PARAM(REPORT_COMMENT, cbuf);
      chprintf(_serial, "%s\r", cbuf);
   }
   else if (strcmp("SYMBOL", p) == 0) {
       char tab = GET_BYTE_PARAM(SYMBOL_TAB);
       char sym = GET_BYTE_PARAM(SYMBOL); 
       chprintf(_serial, "%c%c\r", tab, sym); 
   }
   else if (strcmp("TIMESTAMP", p) == 0)
     chprintf(_serial, "%s\r", PRINT_BOOL(TIMESTAMP_ON, cbuf));
   
   else if (strcmp("COMPRESS", p) == 0)
     chprintf(_serial, "%s\r", PRINT_BOOL(COMPRESS_ON, cbuf));
   
   else if (strcmp("ALTITUDE", p) == 0)
     chprintf(_serial, "%s\r", PRINT_BOOL(ALTITUDE_ON, cbuf));
   
   else if (strcmp("TRX_TX_FREQ", p) == 0) {
      uint32_t x;
      GET_PARAM(TRX_TX_FREQ, &x);
      chprintf(_serial, "%lu\r", x);
   }
   else if (strcmp("TRX_RX_FREQ", p) == 0) {
      uint32_t x;
      GET_PARAM(TRX_RX_FREQ, &x);
      chprintf(_serial, "%lu\r", x);
   }
   else if (strcmp("TRACKER_TURN_LIMIT", p) == 0) {
      uint16_t x; 
      GET_PARAM(TRACKER_TURN_LIMIT, &x);
      chprintf(_serial, "%u\r", x);
   }
   else if (strcmp("TRACKER_MAXPAUSE", p) == 0)
      chprintf(_serial, "%u\r", GET_BYTE_PARAM(TRACKER_MAXPAUSE));
   
   else if (strcmp("TRACKER_MINPAUSE", p) == 0)
      chprintf(_serial, "%u\r", GET_BYTE_PARAM(TRACKER_MINPAUSE));
   
   else if (strcmp("TRACKER_MINDIST", p) == 0)
     chprintf(_serial, "%u\r", GET_BYTE_PARAM(TRACKER_MINDIST));
   
   else if (strcmp("HTTP_ON", p) == 0)
     chprintf(_serial, "%s\r", PRINT_BOOL(HTTP_ON, cbuf));
   
   else if (strcmp("HTTP_USER", p) == 0) {
     GET_PARAM(HTTP_USER, cbuf);
     chprintf(_serial, "%s\r", cbuf);
   }
   
   else if (strcmp("HTTP_PASSWD", p) == 0) {
     GET_PARAM(HTTP_PASSWD, cbuf);
     chprintf(_serial, "%s\r", cbuf);
   }
      
   else if (strcmp("IGATE_ON", p) == 0)
     chprintf(_serial, "%s\r", PRINT_BOOL(IGATE_ON, cbuf));
   
   else if (strcmp("IGATE_HOST", p) == 0) {
     GET_PARAM(IGATE_HOST, cbuf);
     chprintf(_serial, "%s\r", cbuf);
   }
    
   else if (strcmp("IGATE_PORT", p) == 0) {
      uint16_t x; 
      GET_PARAM(IGATE_PORT, &x);
      chprintf(_serial, "%u\r", x);
   }
   
   else if (strcmp("IGATE_PASSCODE", p) == 0) {
      uint16_t x; 
      GET_PARAM(IGATE_PASSCODE, &x);
      chprintf(_serial, "%u\r", x);
   }
   
   else if (strcmp("IGATE_USERNAME", p) == 0) {
     GET_PARAM(IGATE_USERNAME, cbuf);
     chprintf(_serial, "%s\r", cbuf);
   }   

   else if (strcmp("DIGIPEATER_ON", p) == 0)
     chprintf(_serial, "%s\r", PRINT_BOOL(DIGIPEATER_ON, cbuf));
   
   else if (strcmp("DIGIP_WIDE1_ON", p) == 0)
     chprintf(_serial, "%s\r", PRINT_BOOL(DIGIP_WIDE1_ON, cbuf));

   else if (strcmp("DIGIP_SAR_ON", p) == 0)
     chprintf(_serial, "%s\r", PRINT_BOOL(DIGIP_SAR_ON, cbuf));
   
   else if (strncmp("WIFIAP", p, 6) == 0) {
      int i = atoi(p+6);
      if (i<0 || i>5) {
        chprintf(_serial, "ERROR. Index out of bounds\r");
        return;
      }
      ap_config_t x; 
      GET_PARAM_I(WIFIAP, i, &x);
      if (strlen(x.ssid) == 0)
         chprintf(_serial, "-,-\r"); 
      else
         chprintf(_serial, "%s,%s\r", x.ssid, x.passwd);
   }
   else
      chprintf(_serial, "ERROR. Unknown setting\r");
}



/*****************************************************************
 * Process commands coming from WIFI module that write parameters
 *****************************************************************/

static void cmd_setParm(char* p, char* val) { 
    if (strcmp("MYCALL", p) == 0) {
       addr_t x;
       str2addr(&x, val, false);
       SET_PARAM(MYCALL, &x);
       chprintf(_serial, "OK\r"); 
    }
    else if (strcmp("DEST", p) == 0) {
       addr_t x;
       str2addr(&x, val, false);
       SET_PARAM(DEST, &x);
       chprintf(_serial, "OK\r"); 
    }
    else if (strcmp("DIGIS", p) == 0) 
       chprintf(_serial, "%s\r", parseDigipath(val, cbuf));
      
    else if (strcmp("SYMBOL", p) == 0) 
       chprintf(_serial, "%s\r", parseSymbol(val, cbuf));
    
    else if (strcmp("REPORT_COMMENT", p) == 0) {
      /* FIXME: Sanitize input */ 
      SET_PARAM(REPORT_COMMENT, val);
      chprintf(_serial, "OK\r");
    }
    else if (strcmp("TIMESTAMP", p) == 0)
       chprintf(_serial, "%s\r", PARSE_BOOL(TIMESTAMP_ON, val, cbuf));  

    else if (strcmp("COMPRESS", p) == 0)
      chprintf(_serial, "%s\r", PARSE_BOOL(COMPRESS_ON, val, cbuf));  

    else if (strcmp("ALTITUDE", p) == 0)
      chprintf(_serial, "%s\r", PARSE_BOOL(ALTITUDE_ON, val, cbuf));  
    
    else if (strcmp("TRX_TX_FREQ", p) == 0) 
       chprintf(_serial, "%s\r", parseFreq(val, cbuf, true));
    
    else if (strcmp("TRX_RX_FREQ", p) == 0) 
       chprintf(_serial, "%s\r", parseFreq(val, cbuf, false));
    
    else if (strcmp("TRACKER_TURN_LIMIT", p) == 0) 
       chprintf(_serial, "%s\r", parseTurnLimit(val, cbuf));
    
    else if (strcmp("TRACKER_MAXPAUSE", p) == 0)
       chprintf(_serial, "%s\r", PARSE_BYTE(TRACKER_MAXPAUSE, val, 0, 100, cbuf));
    
    else if (strcmp("TRACKER_MINPAUSE", p) == 0)
      chprintf(_serial, "%s\r", PARSE_BYTE(TRACKER_MINPAUSE, val, 0, 100, cbuf));
    
    else if (strcmp("TRACKER_MINDIST", p) == 0)
      chprintf(_serial, "%s\r", PARSE_BYTE(TRACKER_MINDIST, val, 0, 250, cbuf));
    
    else if (strcmp("SOFTAP_PASSWD", p) == 0) {
      SET_PARAM(SOFTAP_PASSWD, val);
      chprintf(_serial, "OK\r"); 
    }
    
    else if (strcmp("HTTP_USER", p) == 0) {
      SET_PARAM(HTTP_USER, val);
      chprintf(_serial, "OK\r"); 
    }
    
    else if (strcmp("HTTP_PASSWD", p) == 0) {
      SET_PARAM(HTTP_PASSWD, val);
      chprintf(_serial, "OK\r"); 
    }
      
    else if (strcmp("IGATE_ON", p) == 0)
      chprintf(_serial, "%s\r", PARSE_BOOL(IGATE_ON, val, cbuf));  
 
      
    else if (strcmp("IGATE_HOST", p) == 0) {
      SET_PARAM(IGATE_HOST, val);
      chprintf(_serial, "OK\r"); 
    }
    
    else if (strcmp("IGATE_PORT", p) == 0)
       chprintf(_serial, "%s\r", PARSE_WORD(IGATE_PORT, val, 0, 65535, cbuf));
       
    else if (strcmp("IGATE_USERNAME", p) == 0) {
      SET_PARAM(IGATE_USERNAME, val);
      chprintf(_serial, "OK\r"); 
    }
    
    else if (strcmp("IGATE_PASSCODE", p) == 0) 
       chprintf(_serial, "%s\r", PARSE_WORD(IGATE_PASSCODE, val, 0, 65535, cbuf));
    
    else if (strcmp("IGATE_FILTER", p) == 0) {
      SET_PARAM(IGATE_FILTER, val);
      chprintf(_serial, "OK\r"); 
    }
      
    else if (strcmp("DIGIPEATER_ON", p) == 0)
      chprintf(_serial, "%s\r", PARSE_BOOL(DIGIPEATER_ON, val, cbuf));  
    
    else if (strcmp("DIGIP_WIDE1_ON", p) == 0)
      chprintf(_serial, "%s\r", PARSE_BOOL(DIGIP_WIDE1_ON, val, cbuf)); 

    else if (strcmp("DIGIP_SAR_ON", p) == 0)
      chprintf(_serial, "%s\r", PARSE_BOOL(DIGIP_SAR_ON, val, cbuf));
    
    else if (strncmp("WIFIAP_RESET", p, 12) == 0) {
      ap_config_t x; 
      *x.ssid = '\0'; 
      *x.passwd = '\0';
      for (int i=0; i<6; i++)
         SET_PARAM_I(WIFIAP, i, &x);
      chprintf(_serial, "OK\r");
    }
      
    else if (strncmp("WIFIAP", p, 6) == 0) {
      int i = atoi(p+6);
      if (i<0 || i>5) {
        chprintf(_serial, "ERROR. Index out of bounds\r");
        return; 
      }
      char* split = strchr(val, ',');
      ap_config_t x; 

      if (*(split+1) == '-')
        *x.passwd = '\0';
      else
        strcpy(x.passwd, split+1);

      if (*val == '-')
        *x.ssid = '\0';
      else {
        *split = '\0'; 
        strcpy(x.ssid, val);
      }
      SET_PARAM_I(WIFIAP, i, &x); 
      chprintf(_serial, "OK\r");
    }
    
    else
       chprintf(_serial, "ERROR. Unknown setting\r");
}

 



static ap_config_t wifiap;


static void cmd_checkAp(char* ssid) {
   for (int i=0; i<N_WIFIAP; i++) {
     GET_PARAM_I(WIFIAP, i, &wifiap);
     if (strcmp(ssid, wifiap.ssid) == 0) {
        chprintf(_serial, "%d,%s\r", i, wifiap.passwd);
        return;
     }
   }
   /* Index 999 means that no config is found */
   chprintf(_serial, "999,_NO_\r");
   
}



/*************************************************************************** 
 * Get and execute a command from the WIFI module
 * 
 * A command is one character followed by arguments and ended with a newline
 * Get parameter: #R PARM
 * Set parameter: #W PARM VALUE 
 ***************************************************************************/

static void wifi_command() {
   char *tokp; 
   
   MUTEX_LOCK;
   readline(_serial, cbuf, 128);
   if (cbuf[0] == 'R') 
      /* Read parameter */
      cmd_getParm((char*) _strtok((char*) cbuf+1, " ", &tokp));
   else if (cbuf[0] == 'W')
      /* Write parameter */
      cmd_setParm((char*) _strtok((char*) cbuf+1, " ", &tokp), (char*) _strtok(NULL, "\0", &tokp));
   else if (cbuf[0] == 'A')
      /* Check access point */
      cmd_checkAp((char*) _strtok((char*) cbuf+1, " ", &tokp));
     
   MUTEX_UNLOCK;
}



/*****************************************************************************
 * Main thread to get characters from the WIFI module over 
 * the serial line. 
 *****************************************************************************/

static THD_FUNCTION(wifi_monitor, arg)
{
   (void) arg;
   chRegSetThreadName("WIFI module listener");
   while (true) {  
      char c;
      if (streamRead(_serial, (uint8_t *)&c, 1) != 0) {
         if (_shell != NULL)
            /* If shell is active, just pass character on to the shell */
            streamPut(_shell, c);
         
         else if (c == '$') {
            readline(_serial, cbuf, 10);
            if (strcmp(cbuf, "__BOOT__") == 0) 
                wifi_start_server(true);
         }
         else if (!_running) continue; 
         
         else if (c == '@') {
            /* Response to command from WIFI module */
	        if (client_active) {
	           readline(_serial, *client_buf, 128);
	           SIGNAL_RESPONSE;
	        }
         }
         
         else if (c == '#')             
             /* Command from WIFI module */
	         wifi_command();
	 
	     else if (c == ':') {
             /* Incoming data */
             FBUF input; 
             fbuf_new(&input);
             fbuf_streamRead(_serial, &input);
             
             /* Insert into queues should be nonblocking. Do not 
              * try to insert if queue is full. 
              */
             if (!read_disable && !fbq_full(&read_queue))
               fbq_put(&read_queue, fbuf_newRef(&input));
             if (mon_queue != NULL && !fbq_full(mon_queue))
               fbq_put(mon_queue, input);
             else
               fbuf_release(&input);
         }
         
         else if (c == '!') {
             /* Connection closed */
             inet_connected = false; 
         }
         else if (c == '*')
             /* Comment */
             readline(_serial, cbuf, 128);
        }
   }
}



void wifi_init(SerialDriver* sd)
{
   _serial = (Stream*) sd;
   wifi_internal();
   clearPin(WIFI_ENABLE);
   sdStart(sd, &_serialConfig);  
   FBQ_INIT(read_queue, INET_RX_QUEUE_SIZE);
   THREAD_START(wifi_monitor, NORMALPRIO, NULL);
   sleep(1000);
   if (GET_BYTE_PARAM(WIFI_ON))
      wifi_enable();
}

