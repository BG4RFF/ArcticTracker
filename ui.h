#if !defined __UI_H__
#define __UI_H__

void ui_init(void);
void rgb_led_on(bool, bool, bool);
void rgb_led_off(void);
void rgb_led_mix(uint8_t red, uint8_t green, uint8_t blue);

#endif
