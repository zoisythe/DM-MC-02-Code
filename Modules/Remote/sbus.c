#include "sbus.h"

enum { SBUS_START = 0x0F, SBUS_LOST_FRAME = 1u << 2, SBUS_FAILSAFE = 1u << 3 };

struct sbus_receiver sbus_status;

static void decode_frame(struct sbus_receiver *receiver, uint32_t now)
{
    const uint8_t *bytes = receiver->buffer;
    ++receiver->frames;
    receiver->flags = bytes[23];
    if ((receiver->flags & SBUS_FAILSAFE) != 0u)
    {
        receiver->failsafe_active = true;
        ++receiver->link_interruptions;
    }
    if ((receiver->flags & (SBUS_LOST_FRAME | SBUS_FAILSAFE)) != 0u)
    {
        return;
    }

    for (uint32_t channel = 0; channel < SBUS_CHANNEL_COUNT; ++channel)
    {
        uint32_t bit = channel * 11u;
        uint32_t index = 1u + bit / 8u;
        uint32_t packed = (uint32_t)bytes[index] | ((uint32_t)bytes[index + 1u] << 8);
        if (index + 2u <= 22u)
        {
            packed |= (uint32_t)bytes[index + 2u] << 16;
        }
        receiver->channels[channel] = (uint16_t)((packed >> (bit % 8u)) & 0x7FFu);
    }
    if (receiver->has_command && !receiver->failsafe_active &&
        !sbus_has_recent_command(receiver, now))
    {
        ++receiver->link_interruptions;
    }
    ++receiver->good_frames;
    receiver->last_good_ms = now;
    receiver->has_command = true;
    receiver->failsafe_active = false;
}

void sbus_receive_byte(struct sbus_receiver *receiver, uint8_t byte, uint32_t now)
{
    if (receiver->count == 0u && byte != SBUS_START)
    {
        return;
    }
    receiver->buffer[receiver->count++] = byte;
    if (receiver->count < SBUS_FRAME_SIZE)
    {
        return;
    }
    if (receiver->buffer[24] == 0u && (receiver->buffer[23] & 0xF0u) == 0u)
    {
        receiver->count = 0;
        decode_frame(receiver, now);
        return;
    }

    ++receiver->bad_frames;
    uint32_t start = 1u;
    while (start < SBUS_FRAME_SIZE && receiver->buffer[start] != SBUS_START)
    {
        ++start;
    }
    uint32_t kept = SBUS_FRAME_SIZE - start;
    for (uint32_t i = 0; i < kept; ++i)
    {
        receiver->buffer[i] = receiver->buffer[start + i];
    }
    receiver->count = (uint8_t)kept;
}

void sbus_receive_error(struct sbus_receiver *receiver)
{
    ++receiver->uart_errors;
    receiver->count = 0;
}

bool sbus_has_recent_command(const struct sbus_receiver *receiver, uint32_t now)
{
    return receiver->has_command && now - receiver->last_good_ms < SBUS_TIMEOUT_MS;
}
