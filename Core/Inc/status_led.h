#ifndef STATUS_LED_H
#define STATUS_LED_H

#include <stdint.h>

/* 颜色参数为 0xRRGGBB。由主循环轮询，SPI6/BDMA 自行发送波形。 */
void status_led_init(void);
void status_led_poll(uint32_t now, uint32_t rgb);

#endif
