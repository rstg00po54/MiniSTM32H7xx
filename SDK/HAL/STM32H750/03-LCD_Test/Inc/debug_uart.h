#ifndef DEBUG_UART_H
#define DEBUG_UART_H

/* USART1 / PA9(TX) PA10(RX), 115200 8N1, polling diagnostic console. */
void DebugUart_Init(void);
void DebugUart_Print(const char *message);
void DebugUart_PollEcho(void);

#endif /* DEBUG_UART_H */
