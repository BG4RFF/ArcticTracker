 #if !defined __RADIO_H__
 #define __RADIO_H__
  
 #include <stdint.h>
 
 void radio_require(void);
 void radio_release(void);
 void radio_wait_enabled(void);
 void radio_init(SerialDriver* sd);
 bool radio_setFreq(uint32_t txfreq, uint32_t rxfreq);
 bool radio_setSquelch(uint8_t sq);
 void radio_on(bool on);
 void radio_PTT(bool on);
 bool radio_setVolume(uint8_t vol);
 bool radio_setMicLevel(uint8_t level);
 bool radio_powerSave(bool on);
 bool radio_setLowTxPower(bool on); 
 void squelch_handler(EXTDriver *extp, expchannel_t channel);
 
 
#endif