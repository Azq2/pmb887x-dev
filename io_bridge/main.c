#include "main.h"

void irq_test();
void abort_addr();
void undef_addr();
void prefetch_addr();
void swi_handler();
void inf_loop();
void main();

int current_irq = 0;

void _start() {
	set_einit(0);
	disable_first_whatchdog();
	set_einit(1);
	
	enable_irq(0);
	enable_fiq(0);
	init_watchdog();
	
	i2c_init();
	i2c_smbus_write_byte(0x31, 0xE, 0b11);
	
	pmb8876_serial_set_speed(UART_SPEED_614400);
	while (pmb8876_serial_getc() != 'O');
	while (pmb8876_serial_getc() != 'K');
	pmb8876_serial_putc('.');
	
	REG(0xF4400078) = 0;
	
	int i;
	void **vectors = (void **) 0;
	vectors[8] = inf_loop;
	vectors[9] = undef_addr;
	vectors[10] = swi_handler;
	vectors[11] = prefetch_addr;
	vectors[12] = abort_addr;
	vectors[13] = inf_loop;
	vectors[14] = irq_test;
	vectors[15] = inf_loop;
	
	enable_irq(1);
	enable_fiq(1);
	
	main();
}

int command_handler(int irq) {
	unsigned int value, addr;
	if (pmb8876_serial_has_byte()) {
		char c = pmb8876_serial_getc();
		if (c == '.') {
			serve_watchdog();
			pmb8876_serial_putc(irq ? ',' : '.');
			
			return 1;
		} else if (c == 'R' || c == 'r' || c == 'I' || c == 'i') { // 4, 2, 3, 1 bytes
			serve_watchdog();
			addr = pmb8876_serial_getc() << 24 | pmb8876_serial_getc() << 16 | pmb8876_serial_getc() << 8 | pmb8876_serial_getc();
			if (addr == IRQ_CURRENT_NUM) {
				value = current_irq;
			} else {
				if (c == 'R') { // 4
					value = REG(addr);
				} else if (c == 'r') { // 2
					value = REG_SHORT(addr);
				} else if (c == 'I') { // 3
					value = REG_SHORT(addr) << 8 | REG_BYTE(addr);
				} else if (c == 'i') { // 1
					value = REG_BYTE(addr);
				}
			}
			pmb8876_serial_putc((value >> 0 ) & 0xFF);
			pmb8876_serial_putc((value >> 8 ) & 0xFF);
			pmb8876_serial_putc((value >> 16) & 0xFF);
			pmb8876_serial_putc((value >> 24) & 0xFF);
			pmb8876_serial_putc(irq ? '!' : ';');
			serve_watchdog();
			
			return 1;
		} else if (c == 'W' || c == 'w' || c == 'O' || c == 'o') { // 4, 2, 3, 1 bytes
			serve_watchdog();
			addr = pmb8876_serial_getc() << 24 | pmb8876_serial_getc() << 16 | pmb8876_serial_getc() << 8 | pmb8876_serial_getc();
			value = pmb8876_serial_getc() << 24 | pmb8876_serial_getc() << 16 | pmb8876_serial_getc() << 8 | pmb8876_serial_getc();
			
			if (c == 'W') { // 4
				REG(addr) = value;
			} else if (c == 'w') { // 2
				REG_SHORT(addr) = value & 0xFFFF;
			} else if (c == 'O') { // 3
				REG_BYTE(addr) = value & 0xFF;
				REG_SHORT(addr) = (value >> 8) & 0xFFFF;
			} else if (c == 'o') { // 1
				REG_BYTE(addr) = value & 0xFF;
			}
			
			pmb8876_serial_putc(irq ? '!' : ';');
			serve_watchdog();
			
			if (addr == IRQ_ACK)
				return 2;
			
			return 1;
		}
	}
	return 0;
}

void main() {
	unsigned int value, addr;
	while (1) {
		if (pmb8876_serial_has_byte())
			asm("swi 0");
	}
}

void onerr(char c) {
	while (1) {
		pmb8876_serial_putc(c);
	}
}

void __IRQ swi_handler() {
	command_handler(0);
}

void __IRQ inf_loop() {
	onerr(0xEC);
}

void __IRQ abort_addr() {
	onerr(0xE1);
}

void __IRQ undef_addr() {
	onerr(0xE2);
}

void __IRQ prefetch_addr() {
	onerr(0xE3);
}

void __IRQ irq_test() {
	current_irq = REG(IRQ_CURRENT_NUM);
	
	while (!command_handler(1)); // Ждём команду
	pmb8876_serial_putc(current_irq & 0xFF); // Высылаем сразу после команды текущий IRQ
	while (command_handler(0) != 2); // Ждём IRQ_ACK
	
	current_irq = 0;
}
