#include "core.h"
#include "rb.h"
#include "main.h"

#define SERIAL_BASE 0xa0000000
#define SERIAL_FLAG_REGISTER 0x18
#define SERIAL_BUFFER_FULL (1 << 5)

static unsigned long int next = 1;
 
void memset(void *p, uint8 v, uintptr sz) {
	uint8 volatile		*_p;
	uintptr				x;
	
	_p = (uint8*)p;
	
	for (x = 0; x < sz; ++x) {
		_p[x] = v;
	}
	
	return;
}
 
uint32 __rand(uint32 next) {
    next = next * 1103515245 + 12345;
    return (uint32)(next / 65536) % 32768;
}
 
int rand() {
	return (int)__rand(next);
}

void srand(unsigned int seed)
{
    next = seed;
}

static void kserdbg_putc(char c) {
    while (*(volatile unsigned long*)(SERIAL_BASE + SERIAL_FLAG_REGISTER) & (SERIAL_BUFFER_FULL));
	
    *(volatile unsigned long*)SERIAL_BASE = c;
}

static void kserdbg_puts(const char * str) {
    while (*str != 0) {
		kserdbg_putc(*str++);
	}
}

char* itoh(int i, char *buf)
{
	const char 	*itoh_map = "0123456789ABCDEF";
	int			n;
	int			b;
	int			z;
	int			s;
	
	if (sizeof(void*) == 4)
		s = 8;
	if (sizeof(void*) == 8)
		s = 16;
	
	for (z = 0, n = (s - 1); n > -1; --n)
	{
		b = (i >> (n * 4)) & 0xf;
		buf[z] = itoh_map[b];
		++z;
	}
	buf[z] = 0;
	return buf;
}

static void __ksprintf(char *buf, const char *fmt, __builtin_va_list argp)
{
	const char 				*p;
	int 					i;
	char 					*s;
	char 					fmtbuf[256];
	int						x, y;

	//__builtin_va_start(argp, fmt);
	
	x = 0;
	for(p = fmt; *p != '\0'; p++)
	{
		if (*p == '\\') {
			switch (*++p) {
				case 'n':
					buf[x++] = '\n';
					break;
				default:
					break;
			}
			continue;
		}
	
		if(*p != '%')
		{
			buf[x++] = *p;
			continue;
		}

		switch(*++p)
			{
			case 'c':
				i = __builtin_va_arg(argp, int);
				buf[x++] = i;
				break;
			case 's':
				s = __builtin_va_arg(argp, char*);
				for (y = 0; s[y]; ++y) {
					buf[x++] = s[y];
				}
				break;
			case 'x':
				i = __builtin_va_arg(argp, int);
				s = itoh(i, fmtbuf);
				for (y = 0; s[y]; ++y) {
					buf[x++] = s[y];
				}
				break;
			case '%':
				buf[x++] = '%';
				break;
		}
	}
	
	//__builtin_va_end(argp);
	buf[x] = 0;
}

void sprintf(char *buf, const char *fmt, ...) {
	__builtin_va_list		argp;
	
	__builtin_va_start(argp, fmt);
	__ksprintf(buf, fmt, argp);
	__builtin_va_end(argp);
}

//signal(tx->rproc, tx->rthread, tx->signal);
int signal(uintptr proc, uintptr thread, uintptr signal) {
	asm volatile (
		"swi %[code]" 
		: : [code]"i" (KSWI_SIGNAL)
	);
}

void printf(const char *fmt, ...) {
	char					buf[128];
	__builtin_va_list		argp;
	
	__builtin_va_start(argp, fmt);
	__ksprintf(buf, fmt, argp);
	kserdbg_puts(buf);
	__builtin_va_end(argp);
}

uint32 __attribute((naked)) getTicksPerSecond() {
	asm volatile (
		"swi #103\n"
		"bx lr\n"
	);
}

/*
	TODO: Maybe find a more efficent way than doing two system calls back to back. Maybe have a special system
	      call that does both and takes second as an argument. At the moment I am just wanting to keep the kernel
		  minimal and leave optimization for later on down the road if needed.
*/

uint32 __attribute__((noinline)) sleepticks(uint32 timeout) {
	uint32			result;
	asm volatile (
			"mov r0, %[in] \n"
			"swi #101 \n"
			"mov %[result], r0 \n"
			: [result]"=r" (result) : [in]"r" (timeout));
	return result;
}

int sleep(uint32 timeout) {
	int			result;
	uint32		tps;
	
	printf("0:CORE:SLEEP START\n");
	/* convert to ticks */
	tps = getTicksPerSecond();
	
	printf("1:CORE:SLEEP timeout:%x tps:%x\n", timeout, tps);
	timeout = timeout * tps;
	printf("2:CORE:SLEEP timeout:%x\n", timeout);
	
	printf("3:CORE:SLEEP\n");
	result = sleepticks(timeout);
	/* convert from ticks */
	return result / tps;
}

uintptr getsignal() {
	asm("swi %[code]" : : [code]"i" (KSWI_GETSIGNAL));
}

void wakeup(uintptr	proc, uintptr thread) {
	asm volatile (
			"push {r0, r1}\n"
			"swi %[code]\n"
			"pop {r0, r1}\n"
			: : [code]"i" (KSWI_WAKEUP)
	);
}

void notifykserver() {
	asm("swi %[code]" : : [code]"i" (KSWI_KERNELMSG));
}

void yield() {
	asm("swi #102");
}

uintptr __attribute__((naked)) valloc(uintptr cnt) {
	asm("	\
			swi #105 \n\
			bx lr\n\
		");
}

void vfree(uintptr addr, uintptr cnt) {
	asm("\
			swi #106 \n\
			bx lr\n\
		" : : [i1]"r" (addr), [i2]"r" (cnt));
}

ERH				__corelib_rx;
ERH				__corelib_tx;

void __attribute__((naked)) __start() {
	asm("	__localloop:\n\
			mov r1, #0xa0000000\n\
			mov r2, #69\n\
			str r2, [r1]\n\
			b __localloop"
		);
}

//int rb_read_bio(RBM volatile *rbm, void *p, uint32 *sz, uint32 *advance, uint32 timeout) {
void _start(uint32 rxaddr, uint32 txaddr, uint32 txrxsz) {
	printf("rxaddr:%x txaddr:%x txrxsz:%x\n", rxaddr, txaddr, txrxsz);
	
	memset(&__corelib_rx, 0, sizeof(ERH));
	memset(&__corelib_tx, 0, sizeof(ERH));
	
	er_ready(&__corelib_rx, (void*)rxaddr, txrxsz, 16 * 4, 0);
	er_ready(&__corelib_tx, (void*)txaddr, txrxsz, 16 * 4, &katomic_lockspin_yield8nr);
	
	main();
	
	for (;;);
}