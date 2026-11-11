/* Minimal STM32H750 USART1 diagnostic UART: no HAL UART driver dependency.
 * 12 MHz HSE -> PLL1 SYSCLK=240 MHz, APB2/PCLK2=120 MHz.
 * USART1 kernel clock remains the default PCLK2.
 */
#include "debug_uart.h"
#include "stm32h7xx_hal.h"

#define DEBUG_UART_BAUD 115200U
#define UART_TX_WAIT_LIMIT 1000000U

static uint8_t debug_uart_ready;

void DebugUart_Init(void)
{
    GPIO_InitTypeDef pins = {0};
    debug_uart_ready = 0U;

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_USART1_CLK_ENABLE();

    pins.Pin = GPIO_PIN_9 | GPIO_PIN_10;
    pins.Mode = GPIO_MODE_AF_PP;
    pins.Pull = GPIO_PULLUP;
    pins.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    pins.Alternate = GPIO_AF7_USART1;
    HAL_GPIO_Init(GPIOA, &pins);

    /* Disable before changing framing, prescaler and baud rate. */
    USART1->CR1 = 0U;
    USART1->CR2 = 0U;    /* 1 stop bit */
    USART1->CR3 = 0U;    /* No hardware flow control, FIFO disabled */
    USART1->PRESC = 0U;  /* USART input clock prescaler /1 */

    const uint32_t pclk2 = HAL_RCC_GetPCLK2Freq();
    if (pclk2 == 0U) return;
    /* Default OVER8=0: BRR = USART kernel clock / baud. */
    USART1->BRR = (pclk2 + DEBUG_UART_BAUD / 2U) / DEBUG_UART_BAUD;
    USART1->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE;

    /* Do not hang at startup if the UART is unexpectedly unavailable. */
    for (uint32_t tries = 0U; tries < UART_TX_WAIT_LIMIT; ++tries) {
        if ((USART1->ISR & (USART_ISR_TEACK | USART_ISR_REACK)) ==
            (USART_ISR_TEACK | USART_ISR_REACK)) {
            debug_uart_ready = 1U;
            return;
        }
    }
}

static void transmit_byte(uint8_t ch)
{
    if (!debug_uart_ready) return;
    for (uint32_t tries = 0U; tries < UART_TX_WAIT_LIMIT; ++tries) {
        if ((USART1->ISR & USART_ISR_TXE_TXFNF) != 0U) {
            USART1->TDR = ch;
            return;
        }
    }
}

void DebugUart_Print(const char *message)
{
    if (message == 0) return;
    while (*message != '\0') {
        transmit_byte((uint8_t)*message++);
    }
}

void DebugUart_PollEcho(void)
{
    if (!debug_uart_ready) return;

    /* Clear a stale overrun so one dropped byte does not halt RX. */
    if ((USART1->ISR & USART_ISR_ORE) != 0U) {
        USART1->ICR = USART_ICR_ORECF;
    }
    if ((USART1->ISR & USART_ISR_RXNE_RXFNE) != 0U) {
        const uint8_t incoming = (uint8_t)USART1->RDR;
        transmit_byte(incoming);
        if (incoming == (uint8_t)'\r') transmit_byte((uint8_t)'\n');
    }
}
