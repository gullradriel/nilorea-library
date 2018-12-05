/**\file n_signals.c
 *  signals general handling with stack printing
 *\author Castagnier Mickael
 *\version 1.0
 *\date 08/11/2018
 */
#include <stdint.h>
#ifndef __sun
	#include <inttypes.h>
#endif

#ifdef __windows__
  #include <windows.h>
  #include <imagehlp.h>
#else
	#include <signal.h>
	#include <err.h>
#include <execinfo.h>
#endif

#include "nilorea/n_signals.h"
#include "nilorea/n_log.h"

/*! array to store the stack messages */ 
static void *stack_traces[MAX_STACK_FRAMES];

/*! array to use with sigaltstack to be able to catch sigstack without sigseving */
static uint8_t alternate_stack[SIGSTKSZ];


/*!\fn void posix_print_stack_trace( void )
 *\brief Dump the stack
 *\return Nothing
 */
void posix_print_stack_trace( void )
{
	int i, trace_size = 0;
	char **messages = (char **)NULL;

	trace_size = backtrace(stack_traces, MAX_STACK_FRAMES);
	messages = backtrace_symbols(stack_traces, trace_size);

	/* skip the first couple stack frames (as they are this function and
	   our handler) and also skip the last frame as it's (always?) junk. */
	// for (i = 3; i < (trace_size - 1); ++i)
	n_log( LOG_ERR , "[STACK]:" );
	for (i = 0; i < trace_size; ++i) // we'll use this for now so you can see what's going on
	{
		n_log( LOG_ERR , "%s", messages[i]);
	}
	if (messages) { free(messages); } 
}



/*!\fn void posix_signal_handler(int sig, siginfo_t *siginfo, void *context)
 *\brief Function to handle caught signal, will wall print trace
 *\return Nothing
 */
void posix_signal_handler(int sig, siginfo_t *siginfo, void *context)
{
	(void)context;
	switch(sig)
	{
		case SIGSEGV:
			n_log( LOG_ERR , "Caught SIGSEGV: Segmentation Fault" );
			break;
		case SIGINT:
			n_log( LOG_ERR , "Caught SIGINT: Interactive attention signal, (usually ctrl+c)" );
			break;
		case SIGFPE:
			switch(siginfo->si_code)
			{
				case FPE_INTDIV:
					n_log( LOG_ERR , "Caught SIGFPE: (integer divide by zero)" );
					break;
				case FPE_INTOVF:
					n_log( LOG_ERR , "Caught SIGFPE: (integer overflow)" );
					break;
				case FPE_FLTDIV:
					n_log( LOG_ERR , "Caught SIGFPE: (floating-point divide by zero)" );
					break;
				case FPE_FLTOVF:
					n_log( LOG_ERR , "Caught SIGFPE: (floating-point overflow)" );
					break;
				case FPE_FLTUND:
					n_log( LOG_ERR , "Caught SIGFPE: (floating-point underflow)" );
					break;
				case FPE_FLTRES:
					n_log( LOG_ERR , "Caught SIGFPE: (floating-point inexact result)" );
					break;
				case FPE_FLTINV:
					n_log( LOG_ERR , "Caught SIGFPE: (floating-point invalid operation)" );
					break;
				case FPE_FLTSUB:
					n_log( LOG_ERR , "Caught SIGFPE: (subscript out of range)" );
					break;
				default:
					n_log( LOG_ERR , "Caught SIGFPE: Arithmetic Exception" );
					break;
			}
			break;
		case SIGILL:
			switch(siginfo->si_code)
			{
				case ILL_ILLOPC:
					n_log( LOG_ERR , "Caught SIGILL: (illegal opcode)" );
					break;
				case ILL_ILLOPN:
					n_log( LOG_ERR , "Caught SIGILL: (illegal operand)" );
					break;
				case ILL_ILLADR:
					n_log( LOG_ERR , "Caught SIGILL: (illegal addressing mode)" );
					break;
				case ILL_ILLTRP:
					n_log( LOG_ERR , "Caught SIGILL: (illegal trap)" );
					break;
				case ILL_PRVOPC:
					n_log( LOG_ERR , "Caught SIGILL: (privileged opcode)" );
					break;
				case ILL_PRVREG:
					n_log( LOG_ERR , "Caught SIGILL: (privileged register)" );
					break;
				case ILL_COPROC:
					n_log( LOG_ERR , "Caught SIGILL: (coprocessor error)" );
					break;
				case ILL_BADSTK:
					n_log( LOG_ERR , "Caught SIGILL: (internal stack error)" );
					break;
				default:
					n_log( LOG_ERR , "Caught SIGILL: Illegal Instruction" );
					break;
			}
			break;
		case SIGTERM:
			n_log( LOG_ERR , "Caught SIGTERM: a termination request was sent to the program" );
			break;
		case SIGABRT:
			n_log( LOG_ERR , "Caught SIGABRT: usually caused by an abort() or assert()" );
			break;
		default:
			break;
	}
	posix_print_stack_trace();
	_Exit( 1 );
}



/*!\fn void set_signal_handler( void )
 *\brief Install the signal handler
 *\return nothing
 */
void set_signal_handler( void )
{
	/* setup alternate stack */
	stack_t ss ;
	ss.ss_sp = (void*)alternate_stack;
	ss.ss_size = SIGSTKSZ;
	ss.ss_flags = 0;


	if (sigaltstack(&ss, NULL) != 0)
	{
		n_log( LOG_ERR , "Coudn't setup alternate stack with sigaltstack" );
		_Exit( 1 );
	}
	/* register our signal handlers */
	struct sigaction sig_action ;
	sig_action.sa_sigaction = posix_signal_handler;
	sigemptyset(&sig_action.sa_mask);

	sig_action.sa_flags = SA_SIGINFO | SA_ONSTACK;

	if (sigaction(SIGSEGV, &sig_action, NULL) != 0) { n_log( LOG_ERR , "Could not set sigaction on SIGSEGV"); _Exit( 1 ); }
	if (sigaction(SIGFPE,  &sig_action, NULL) != 0) { n_log( LOG_ERR , "Could not set sigaction on SIGFPE"); _Exit( 1 ); }
	if (sigaction(SIGINT,  &sig_action, NULL) != 0) { n_log( LOG_ERR , "Could not set sigaction on SIGINT"); _Exit( 1 ); }
	if (sigaction(SIGILL,  &sig_action, NULL) != 0) { n_log( LOG_ERR , "Could not set sigaction on SIGILL"); _Exit( 1 ); }
	if (sigaction(SIGTERM, &sig_action, NULL) != 0) { n_log( LOG_ERR , "Could not set sigaction on SIGTERM"); _Exit( 1 ); }
	if (sigaction(SIGABRT, &sig_action, NULL) != 0) { n_log( LOG_ERR , "Could not set sigaction on SIGABRT"); _Exit( 1 ); }
}
