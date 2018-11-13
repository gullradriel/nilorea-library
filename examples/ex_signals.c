#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <signal.h>
#include <assert.h>

#include "nilorea/n_log.h"
#include "nilorea/n_signals.h"

int  divide_by_zero(void);
void cause_segfault(void);
void stack_overflow(void);
void infinite_loop(void);
void illegal_instruction(void);
void cause_calamity(void);

int main( int argc, char * argv[])
{
	set_log_level( LOG_DEBUG );
	set_log_level( LOG_STDERR );

	(void)argc;
	(void)argv[0];

	set_signal_handler();

	cause_calamity();

	return 0;
}

void cause_calamity(void)
{
	/* uncomment one of the following error conditions to cause a calamity of 
	   your choosing! */
	//stack_overflow();
	//(void)divide_by_zero();
	cause_segfault();
	assert(false);
	infinite_loop();
	illegal_instruction();
}



int divide_by_zero(void)
{
	int a = 1;
	int b = 0; 
	return a / b;
}

void cause_segfault(void)
{
	int * p = (int*)0x12345678;
	*p = 0;
}

void stack_overflow(void)
{
	int foo[1000];
	(void)foo;
	stack_overflow();
}

/* break out with ctrl+c to test SIGINT handling */
void infinite_loop(void)
{
	while(1) {};
}

void illegal_instruction(void)
{
	/* I couldn't find an easy way to cause this one, so I'm cheating */
	raise(SIGILL);
}

