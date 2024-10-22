#define UART		0xF0001000

//#define printf mprintf
//#define printf(...)
//#define mprintf(...)
//#define exit mexit_spin

// make sure to run main first
int main(void)  __attribute__ ((section ("_main_section")));

char* itoa(int num);
char* ltoa(long long num);
char* i2h(unsigned int i, int caps);
char* l2h(unsigned long long i, int caps);
int printf (const char *fmt, ...);
void print_uart(const unsigned char* ptr);
//void mexit_spin(int a);
void exit(int a);

#ifdef SIM
	void printc_uart(const unsigned char ch);
	
	#define mexit(...)		(*((volatile unsigned char *) HALT_ADDRESS) = 0)
#else
	void printc_uart(const unsigned char c);
	#define print_hex (val) (*(volatile unsigned int*) HEX = val)
	#define print_ledr(val) (*(volatile unsigned int*) RED_LED = val)
	#define print_ledg(val) (*(volatile unsigned int*) GREEN_LED = val)
	
//	#define mexit(a) mexit_spin(a)
#endif


// eventually will be a command line argument
#define COUNT

#ifdef COUNT
       #define START_COUNTER() \
               PERF_RESET (PERF_UNIT_BASE);\
               PERF_START_MEASURING (PERF_UNIT_BASE)
               
       #define STOP_COUNTER() \
               PERF_STOP_MEASURING (PERF_UNIT_BASE)

       #define OUTPUT_COUNTER() \
               printf("\ncounter = %d\n", perf_get_total_time((void*)PERF_UNIT_BASE))
               
#else
       #define START_COUNTER()
       #define STOP_COUNTER()
       #define OUTPUT_COUNTER()
#endif


