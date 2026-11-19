#include "status_led.h"
#include "board_regs.h"

#include <stdbool.h>

#define SPI6_BASE 0x58001400u
#define BDMA_BASE 0x58025400u
#define DMAMUX2_BASE 0x58025800u
#define SPI6_CLOCK (1u << 5)
#define LED_PIN 7u
#define LED_MODE_SHIFT (2u * LED_PIN)
#define LED_AF_SHIFT (4u * LED_PIN)

enum
{
    SPI_CR1 = 0x00u, SPI_CR2 = 0x04u, SPI_CFG1 = 0x08u, SPI_CFG2 = 0x0Cu,
    SPI_SR = 0x14u, SPI_TXDR = 0x20u,
    SPI_ENABLE = 1u << 0, SPI_START = 1u << 9, SPI_INTERNAL_NSS = 1u << 12,
    SPI_MODF = 1u << 9, SPI_TXC = 1u << 12,
    SPI_TX_DMA_ENABLE = 1u << 15,
    BDMA_ISR = 0x00u, BDMA_IFCR = 0x04u, BDMA_CCR0 = 0x08u, BDMA_CNDTR0 = 0x0Cu,
    BDMA_CPAR0 = 0x10u, BDMA_CM0AR0 = 0x14u,
    BDMA_ENABLE = 1u << 0, BDMA_TO_PERIPHERAL = 1u << 4, BDMA_INCREMENT = 1u << 7,
    BDMA_COMPLETE = 1u << 1, BDMA_ERROR = 1u << 3, BDMA_CLEAR_CHANNEL0 = 1u << 0,
    SPI6_TX_REQUEST = 12u,
    COLOR_WORDS = 6u, RESET_WORDS = 60u, FRAME_WORDS = COLOR_WORDS + RESET_WORDS,
    TRANSFER_TIMEOUT_MS = 3u, RETRY_INTERVAL_MS = 100u
};

/* BDMA can read SRAM4. Word stores initialize the SRAM ECC before the first transfer. */
static volatile uint32_t waveform[FRAME_WORDS] __attribute__((section(".dma_sram4")));
static uint32_t sent_rgb = UINT32_MAX;
static uint32_t in_flight_rgb, started_ms, last_failure_ms;
static bool busy, cooling_down;
static volatile struct
{
    uint32_t count, dma_status, dma_remaining, spi_status, spi_cr1, apb4enr;
} status_led_fault;

static void set_pin_mode(uint32_t mode)
{
    REG(GPIOA_BASE) = (REG(GPIOA_BASE) & ~(3u << LED_MODE_SHIFT)) | (mode << LED_MODE_SHIFT);
}

static void reset_spi(void)
{
    REG(RCC + 0xF4u) |= SPI6_CLOCK; /* APB4ENR */
    (void)REG(RCC + 0xF4u);
    REG(RCC + 0x9Cu) |= SPI6_CLOCK; /* APB4RSTR */
    (void)REG(RCC + 0x9Cu);
    REG(RCC + 0x9Cu) &= ~SPI6_CLOCK;
    /* Drive internal NSS high before selecting master, to avoid MODF. */
    REG(SPI6_BASE + SPI_CR1) = SPI_INTERNAL_NSS;
    /* HSE at 24 MHz, divided by four. 8-bit frames, one-frame FIFO threshold. */
    REG(SPI6_BASE + SPI_CFG1) = (1u << 28) | SPI_TX_DMA_ENABLE | 7u;
    /* Software NSS, master, simplex TX, CPOL 0, CPHA 1. */
    REG(SPI6_BASE + SPI_CFG2) = (1u << 26) | (1u << 24) | (1u << 22) | (1u << 17);
}

void status_led_init(void)
{
    REG(RCC + AHB4ENR) |= (1u << 21) | 1u; /* BDMA and GPIOA */
    REG(RCC + 0x58u) = (REG(RCC + 0x58u) & ~(7u << 28)) | (5u << 28); /* SPI6SEL = HSE */

    REG(GPIOA_BASE + 0x18u) = 1u << (LED_PIN + 16u); /* Drive low between transfers. */
    REG(GPIOA_BASE + 0x04u) &= ~(1u << LED_PIN);
    REG(GPIOA_BASE + 0x08u) = (REG(GPIOA_BASE + 0x08u) & ~(3u << LED_MODE_SHIFT)) |
                             (2u << LED_MODE_SHIFT);
    REG(GPIOA_BASE + 0x0Cu) &= ~(3u << LED_MODE_SHIFT);
    REG(GPIOA_BASE + 0x20u) = (REG(GPIOA_BASE + 0x20u) & ~(15u << LED_AF_SHIFT)) |
                             (8u << LED_AF_SHIFT); /* PA7 AF8 = SPI6_MOSI */
    set_pin_mode(1u);

    REG(BDMA_BASE + BDMA_CCR0) = 0;
    REG(BDMA_BASE + BDMA_IFCR) = BDMA_CLEAR_CHANNEL0;
    REG(DMAMUX2_BASE) = SPI6_TX_REQUEST;
    REG(BDMA_BASE + BDMA_CPAR0) = SPI6_BASE + SPI_TXDR;
    REG(BDMA_BASE + BDMA_CM0AR0) = (uint32_t)(uintptr_t)waveform;
    for (uint32_t i = COLOR_WORDS; i < FRAME_WORDS; ++i)
    {
        waveform[i] = 0; /* 240 low bytes give 320 us of reset time at 6 MHz. */
    }
    reset_spi();
}

static void start_frame(uint32_t now, uint32_t rgb)
{
    uint32_t grb = ((rgb & 0x00FF00u) << 8) | ((rgb & 0xFF0000u) >> 8) | (rgb & 0xFFu);
    for (uint32_t i = 0; i < COLOR_WORDS; ++i)
    {
        uint32_t word = 0;
        for (uint32_t byte = 0; byte < 4u; ++byte)
        {
            uint32_t symbol = (grb & 0x800000u) != 0u ? 0x78u : 0x60u;
            word |= symbol << (8u * byte);
            grb <<= 1;
        }
        waveform[i] = word;
    }

    REG(BDMA_BASE + BDMA_IFCR) = BDMA_CLEAR_CHANNEL0;
    REG(BDMA_BASE + BDMA_CNDTR0) = sizeof waveform;
    /* BDMA supplies the finite byte count. TSIZE=0 avoids the STM32H723 EOT stall. */
    REG(SPI6_BASE + SPI_CR2) = 0;
    __asm volatile("dsb" ::: "memory");
    REG(SPI6_BASE + SPI_CR1) = SPI_INTERNAL_NSS | SPI_ENABLE;
    set_pin_mode(2u);
    REG(BDMA_BASE + BDMA_CCR0) = BDMA_TO_PERIPHERAL | BDMA_INCREMENT | BDMA_ENABLE;
    REG(SPI6_BASE + SPI_CR1) = SPI_INTERNAL_NSS | SPI_ENABLE | SPI_START;
    busy = true;
    started_ms = now;
    in_flight_rgb = rgb;
}

void status_led_poll(uint32_t now, uint32_t rgb)
{
    /* A debugger attaching in hot-plug mode can change the APB4 clock gates. */
    if ((REG(RCC + 0xF4u) & SPI6_CLOCK) == 0u)
    {
        set_pin_mode(1u);
        REG(BDMA_BASE + BDMA_CCR0) = 0;
        reset_spi();
        busy = false;
        sent_rgb = UINT32_MAX;
        return;
    }
    if (busy)
    {
        uint32_t dma_status = REG(BDMA_BASE + BDMA_ISR);
        uint32_t spi_status = REG(SPI6_BASE + SPI_SR);
        bool complete = (dma_status & BDMA_COMPLETE) != 0u && (spi_status & SPI_TXC) != 0u;
        bool failed = (dma_status & BDMA_ERROR) != 0u || (spi_status & SPI_MODF) != 0u ||
                      (!complete && now - started_ms >= TRANSFER_TIMEOUT_MS);
        if (!complete && !failed)
        {
            return;
        }
        if (failed)
        {
            ++status_led_fault.count;
            status_led_fault.dma_status = dma_status;
            status_led_fault.dma_remaining = REG(BDMA_BASE + BDMA_CNDTR0);
            status_led_fault.spi_status = spi_status;
            status_led_fault.spi_cr1 = REG(SPI6_BASE + SPI_CR1);
            status_led_fault.apb4enr = REG(RCC + 0xF4u);
        }

        set_pin_mode(1u);
        REG(BDMA_BASE + BDMA_CCR0) = 0;
        REG(SPI6_BASE + SPI_CR1) = SPI_INTERNAL_NSS;
        busy = false;
        if (failed)
        {
            reset_spi();
            sent_rgb = UINT32_MAX;
            cooling_down = true;
            last_failure_ms = now;
        }
        else
        {
            sent_rgb = in_flight_rgb;
        }
        return;
    }
    if (cooling_down && now - last_failure_ms < RETRY_INTERVAL_MS)
    {
        return;
    }
    cooling_down = false;
    if (now != 0u && rgb != sent_rgb)
    {
        start_frame(now, rgb);
    }
}
