"""CanMV UART construction. Imports hardware only when explicitly called."""
def open_uart(config):
    from machine import UART, FPIOA
    fpioa = FPIOA()
    number = config['uart_id']
    if number == 4:
        if not config['allow_debug_uart4']:
            raise ValueError('UART4 is a lab-only profile')
        # Existing board routing is read and retained, never guessed or remapped.
        if fpioa.get_pin_num(FPIOA.UART4_TXD) is None or fpioa.get_pin_num(FPIOA.UART4_RXD) is None:
            raise ValueError('no existing UART4 debug routing')
    else:
        fpioa.set_function(config['uart_tx_pin'],getattr(FPIOA,'UART%d_TXD'%number))
        fpioa.set_function(config['uart_rx_pin'],getattr(FPIOA,'UART%d_RXD'%number))
    return UART(getattr(UART,'UART%d'%number),baudrate=config['uart_baudrate'],
                bits=UART.EIGHTBITS,parity=UART.PARITY_NONE,stop=UART.STOPBITS_ONE,
                timeout=config['uart_timeout_ms'])
