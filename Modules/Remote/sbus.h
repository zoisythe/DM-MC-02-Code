#ifndef SBUS_H
#define SBUS_H

#include <stdbool.h>
#include <stdint.h>

enum { SBUS_CHANNEL_COUNT = 16, SBUS_FRAME_SIZE = 25, SBUS_TIMEOUT_MS = 300 };

struct sbus_receiver
{
    uint32_t frames, bad_frames, uart_errors, last_good_ms;
    uint32_t good_frames;
    uint32_t link_interruptions; /* 记录 failsafe 和有效帧之间的超时，即使轮询时链路已恢复 */
    uint16_t channels[SBUS_CHANNEL_COUNT]; /* 最近有效帧的 CH1～CH16 */
    uint8_t flags; /* 最近完整帧的标志位 */
    bool has_command;
    bool failsafe_active; /* 收到正常帧后清除，丢帧不改变此状态 */
    uint8_t buffer[SBUS_FRAME_SIZE];
    uint8_t count;
};

extern struct sbus_receiver sbus_status;

void sbus_receive_byte(struct sbus_receiver *receiver, uint8_t byte, uint32_t now);
void sbus_receive_error(struct sbus_receiver *receiver);
bool sbus_has_recent_command(const struct sbus_receiver *receiver, uint32_t now);

#endif
