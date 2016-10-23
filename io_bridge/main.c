#include "main.h"

extern unsigned int *_cpu_vectors;

void reset_addr();
void undef_addr();
void swi_addr();
void prefetch_addr();
void abort_addr();
void reserved_addr();
void irq_test();
void fiq_test();
void main();

void _start() {
	set_einit(0);
	disable_first_whatchdog();
	set_einit(1);
	
	enable_irq(0);
	enable_fiq(0);
	init_watchdog();
	
	int i;
	void **vectors = (void **) 0;
	for (i = 0; i < 8; ++i)
		vectors[i] = (&_cpu_vectors)[i];
	vectors[8] = reset_addr;
	vectors[9] = undef_addr;
	vectors[10] = swi_addr;
	vectors[11] = prefetch_addr;
	vectors[12] = abort_addr;
	vectors[13] = reserved_addr;
	vectors[14] = irq_test;
	vectors[15] = fiq_test;
	
	enable_irq(1);
	enable_fiq(1);
	main();
}

void main() {
	unsigned int value, addr;
	while (1) {
		char c = pmb8876_serial_getc();
		if (c == '.') {
			serve_watchdog();
			pmb8876_serial_putc('.');
		} else if (c == 'R') {
			serve_watchdog();
			addr = pmb8876_serial_getc() << 24 | pmb8876_serial_getc() << 16 | pmb8876_serial_getc() << 8 | pmb8876_serial_getc();
			value = REG(addr);
			
			pmb8876_serial_putc((value >> 0 ) & 0xFF);
			pmb8876_serial_putc((value >> 8 ) & 0xFF);
			pmb8876_serial_putc((value >> 16) & 0xFF);
			pmb8876_serial_putc((value >> 24) & 0xFF);
			pmb8876_serial_putc(';');
			serve_watchdog();
		} else if (c == 'W') {
			serve_watchdog();
			addr = pmb8876_serial_getc() << 24 | pmb8876_serial_getc() << 16 | pmb8876_serial_getc() << 8 | pmb8876_serial_getc();
			value = pmb8876_serial_getc() << 24 | pmb8876_serial_getc() << 16 | pmb8876_serial_getc() << 8 | pmb8876_serial_getc();
			
			REG(addr) = value;
			pmb8876_serial_putc(';');
			serve_watchdog();
		}
	}
}
void onerr() {
	while (1) {
		pmb8876_serial_putc('E');
		serve_watchdog();
	}
}
void reset_addr() {
	onerr();
}
void undef_addr() {
	onerr();
}
void swi_addr() {
	
}
void prefetch_addr() {
	onerr();
}
void abort_addr() {
	onerr();
}
void reserved_addr() {
	
}
void irq_test() {
	while (1) {
		pmb8876_serial_putc('\xEE');
	}
}
void fiq_test() {
	while (1) {
		pmb8876_serial_putc('\xCC');
	}
}

static void pmb8876_serial_set_speed(unsigned int speed) {
	REG(PMB8876_USART0_BG) = ((speed >> 16));
	REG(PMB8876_USART0_FDV) = ((speed << 16) >> 16);
}

static void pmb8876_serial_print(char *data) {
	while (*data)
		pmb8876_serial_putc(*data++);
}

static void pmb8876_serial_putc(char c) {
	REG(PMB8876_USART0_TXB) = c;
	while (!(REG(PMB8876_USART0_FCSTAT) & 2));
	REG(PMB8876_USART0_ICR) |= 2;
}

static char pmb8876_serial_getc() {
	while (!(REG(PMB8876_USART0_FCSTAT) & 4));
	REG(PMB8876_USART0_ICR) |= 4;
	return REG(PMB8876_USART0_RXB) & 0xFF;
}

void init_sdram() {
	// Инициализация SDRAM
	REG(EBU_ADDRSEL1) = 0xA8000041;
	REG(EBU_BUSCON1) = 0x30720200;
	
	REG(EBU_SDRMREF0) = 6;
	
	REG(EBU_SDRMCON0) = 0x891C70;
	
	REG(EBU_SDRMOD0) = 0x23;
	
	REG(EBU_ADDRSEL0) = 0xA0000011;
	REG(EBU_ADDRSEL4) = 0xA0000011;
	
	REG(EBU_BUSCON0) = 0x00522600;
	REG(EBU_BUSCON4) = 0x00522600;
}

static void init_watchdog() {
	unsigned int r0 = (REG(SCU_CHIPID) >> 8) & 0xFF;
	
	unsigned int r1 = PCL_62;
	unsigned int r2 = PCL_62 - 0xCC;
	
	if (r0 == 0x14) {
		r1 += 0x60;
		r2 += 0x04;
	}
	unsigned int r3 = r2 - 0x4;
	unsigned int r4 = r3 - 0x0c;
	
	REG(r2) = 1;
	REG(r3) = 0x10;
	REG(r1) = 0x500;
	REG(r4) = 0x4000 | 0x510;
	
	__g_watchdog.time = REG(STM_4);
	__g_watchdog.addr = r1;
	
	switch_watchdog();
}

static void switch_watchdog() {
	unsigned int r2 = REG(__g_watchdog.addr);
	unsigned int r0 = r2 << 22;
	r0 = r0 >> 31;
	r0 = ~r0;
	r0 = r0 & 1;
	r0 = r0 << 9;
	
	r2 = r2 & ~0x200;
	
	REG(__g_watchdog.addr) = r0 | r2;
}

static void serve_watchdog() {
	unsigned int now = REG(STM_4);
	if (now - __g_watchdog.time < 0x200)
		return;
	switch_watchdog();
	 __g_watchdog.time = now;
}

static void hexdump(unsigned char *data, unsigned int len) {
	unsigned int i;
	for (i = 0; i < len; ++i) {
		pmb8876_serial_putc(to_hex((data[i] >> 4) & 0xF));
		pmb8876_serial_putc(to_hex(data[i] & 0xF));
		pmb8876_serial_putc(' ');
	}
}

static char to_hex(unsigned char b) {
	if (b < 0xA)
		return '0' + b;
	return 'A' + (b - 10);
}

static void enable_irq(int flag) {
	unsigned int cpsr;
	asm volatile ("MRS %0, cpsr" : "=r" (cpsr) : );
	cpsr = !flag ? cpsr | 0x80 : cpsr & ~0x80;
	asm volatile ("MSR  CPSR_c, %0" :  : "r" (cpsr));
}

static void enable_fiq(int flag) {
	unsigned int cpsr;
	asm volatile ("MRS %0, cpsr" : "=r" (cpsr) : );
	cpsr = !flag ? cpsr | 0x40 : cpsr & ~0x40;
	asm volatile ("MSR  CPSR_c, %0" :  : "r" (cpsr));
}

static void disable_interrapts() {
	unsigned int cpsr;
	asm volatile ("MRS %0, cpsr" : "=r" (cpsr) : );
	cpsr |= 0xC0;
	asm volatile ("MSR  CPSR_c, %0" :  : "r" (cpsr));
}

static void disable_first_whatchdog() {
	REG(SCU_ROMAMCR) &= ~1;
	REG(SCU_WDTCON1) = 0x8;
}

static void set_einit(char flag) {
	
	// ldr r3, =0xf4400000
	// ldr r1, [r3, #0x24] ;SCU_WDTCON0
	unsigned int wdc0 = REG(SCU_WDTCON0);
	
	//  bic r1, r1, #0x0e
	//  orr r1, r1, #0xf0
	wdc0 &= ~0x0E;
	wdc0 |= 0xf0;
	
	// ldr r2, [r3, #0x28] ;SCU_WDTCON1
	// and r2, r2, #0x0c
	unsigned int wdc1 = REG(SCU_WDTCON1);
	wdc1 &= 0x0c;
	
	// orr r1, r1, r2
	// str r1, [r3, #0x24] ;SCU_WDTCON0
	wdc0 |= wdc1; 
	REG(SCU_WDTCON0) = wdc0;
	
	// bic r1, r1, #0x0d
	// orr r1, r1, #2
	// orr r0, r0, r1
	wdc0 &= ~0x0d;
	wdc0 |= 2;
	wdc0 |= flag;
	REG(SCU_WDTCON0) = wdc0;
}
