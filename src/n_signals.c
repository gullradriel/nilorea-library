/**\file n_signals.c
 *  signals general handling with stack printing
 *\author Castagnier Mickael, adapted from https://gist.github.com/jvranish/4441299
 *\version 1.0
 *\date 08/11/2018
 */


#include <signal.h>
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <string.h>

#include "nilorea/n_signals.h"

#define LOGFPRT( ... ) fprintf( stderr , "Error: " __VA_ARGS__ )
#define LOGNLOG( ... ) n_log( LOG_ERR , __VA_ARGS__ ) 

#define LOGSIG LOGNLOG

static const char *icky_global_program_name ;

#ifdef _WIN32
#include <windows.h>
#include <imagehlp.h>
#else
#include <err.h>
#include <execinfo.h>
#endif

// void almost_c99_signal_handler(int sig)
// {
//   switch(sig)
//   {
//     case SIGABRT:
//       LOGSIG("Caught SIGABRT: usually caused by an abort() or assert()", stderr);
//       break;
//     case SIGFPE:
//       LOGSIG("Caught SIGFPE: arithmetic exception, such as divide by zero", stderr);
//       break;
//     case SIGILL:
//       LOGSIG("Caught SIGILL: illegal instruction", stderr);
//       break;
//     case SIGINT:
//       LOGSIG("Caught SIGINT: interactive attention signal, probably a ctrl+c", stderr);
//       break;
//     case SIGSEGV:
//       LOGSIG("Caught SIGSEGV: segfault", stderr);
//       break;
//     case SIGTERM:
//     default:
//       LOGSIG("Caught SIGTERM: a termination request was sent to the program", stderr);
//       break;
//   }
//   _Exit(1);
// }

// void set_signal_handler()
// {
//   signal(SIGABRT, almost_c99_signal_handler);
//   signal(SIGFPE,  almost_c99_signal_handler);
//   signal(SIGILL,  almost_c99_signal_handler);
//   signal(SIGINT,  almost_c99_signal_handler);
//   signal(SIGSEGV, almost_c99_signal_handler);
//   signal(SIGTERM, almost_c99_signal_handler);
// }



/* Resolve symbol name and source location given the path to the executable 
   and an address */
int addr2line(char const * const program_name, void const * const addr)
{
	char addr2line_cmd[512] = {0};

	/* have addr2line map the address to the relent line in the code */
#ifdef __APPLE__
	/* apple does things differently... */
	sprintf(addr2line_cmd,"atos -o %.256s %p", program_name, addr); 
#elif defined __windows__
	sprintf(addr2line_cmd,"addr2line -f -p -e %s %p", program_name, addr); 
#else
	sprintf(addr2line_cmd,"addr2line -f -p -e %.256s %p", program_name, addr); 
#endif

	return system(addr2line_cmd);
}


#ifdef _WIN32
void windows_print_stacktrace(CONTEXT* context)
{
	SymInitialize(GetCurrentProcess(), 0, true);

	STACKFRAME frame = { 0 };

	/* setup initial stack frame */
	/* Wrong values for W10 ? 
	frame.AddrPC.Offset         = context->Eip;
	frame.AddrPC.Mode           = AddrModeFlat;
	frame.AddrStack.Offset      = context->Esp;
	frame.AddrStack.Mode        = AddrModeFlat;
	frame.AddrFrame.Offset      = context->Ebp;
	frame.AddrFrame.Mode        = AddrModeFlat; */

	frame.AddrPC.Offset         = context->Rip;
	frame.AddrPC.Mode           = AddrModeFlat;
	frame.AddrStack.Offset      = context->Rsp;
	frame.AddrStack.Mode        = AddrModeFlat;
	frame.AddrFrame.Offset      = context->Rbp;
	frame.AddrFrame.Mode        = AddrModeFlat;



	while (StackWalk(IMAGE_FILE_MACHINE_I386 ,
				GetCurrentProcess(),
				GetCurrentThread(),
				&frame,
				context,
				0,
				SymFunctionTableAccess,
				SymGetModuleBase,
				0 ) )
	{
		addr2line( icky_global_program_name, (void*)frame.AddrPC.Offset);
	}

	SymCleanup( GetCurrentProcess() );
}

LONG WINAPI windows_exception_handler(EXCEPTION_POINTERS * ExceptionInfo)
{
	switch(ExceptionInfo->ExceptionRecord->ExceptionCode)
	{
		case EXCEPTION_ACCESS_VIOLATION:
			LOGSIG("Error: EXCEPTION_ACCESS_VIOLATION", stderr);
			break;
		case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
			LOGSIG("Error: EXCEPTION_ARRAY_BOUNDS_EXCEEDED", stderr);
			break;
		case EXCEPTION_BREAKPOINT:
			LOGSIG("Error: EXCEPTION_BREAKPOINT", stderr);
			break;
		case EXCEPTION_DATATYPE_MISALIGNMENT:
			LOGSIG("Error: EXCEPTION_DATATYPE_MISALIGNMENT", stderr);
			break;
		case EXCEPTION_FLT_DENORMAL_OPERAND:
			LOGSIG("Error: EXCEPTION_FLT_DENORMAL_OPERAND", stderr);
			break;
		case EXCEPTION_FLT_DIVIDE_BY_ZERO:
			LOGSIG("Error: EXCEPTION_FLT_DIVIDE_BY_ZERO", stderr);
			break;
		case EXCEPTION_FLT_INEXACT_RESULT:
			LOGSIG("Error: EXCEPTION_FLT_INEXACT_RESULT", stderr);
			break;
		case EXCEPTION_FLT_INVALID_OPERATION:
			LOGSIG("Error: EXCEPTION_FLT_INVALID_OPERATION", stderr);
			break;
		case EXCEPTION_FLT_OVERFLOW:
			LOGSIG("Error: EXCEPTION_FLT_OVERFLOW", stderr);
			break;
		case EXCEPTION_FLT_STACK_CHECK:
			LOGSIG("Error: EXCEPTION_FLT_STACK_CHECK", stderr);
			break;
		case EXCEPTION_FLT_UNDERFLOW:
			LOGSIG("Error: EXCEPTION_FLT_UNDERFLOW", stderr);
			break;
		case EXCEPTION_ILLEGAL_INSTRUCTION:
			LOGSIG("Error: EXCEPTION_ILLEGAL_INSTRUCTION", stderr);
			break;
		case EXCEPTION_IN_PAGE_ERROR:
			LOGSIG("Error: EXCEPTION_IN_PAGE_ERROR", stderr);
			break;
		case EXCEPTION_INT_DIVIDE_BY_ZERO:
			LOGSIG("Error: EXCEPTION_INT_DIVIDE_BY_ZERO", stderr);
			break;
		case EXCEPTION_INT_OVERFLOW:
			LOGSIG("Error: EXCEPTION_INT_OVERFLOW", stderr);
			break;
		case EXCEPTION_INVALID_DISPOSITION:
			LOGSIG("Error: EXCEPTION_INVALID_DISPOSITION", stderr);
			break;
		case EXCEPTION_NONCONTINUABLE_EXCEPTION:
			LOGSIG("Error: EXCEPTION_NONCONTINUABLE_EXCEPTION", stderr);
			break;
		case EXCEPTION_PRIV_INSTRUCTION:
			LOGSIG("Error: EXCEPTION_PRIV_INSTRUCTION", stderr);
			break;
		case EXCEPTION_SINGLE_STEP:
			LOGSIG("Error: EXCEPTION_SINGLE_STEP", stderr);
			break;
		case EXCEPTION_STACK_OVERFLOW:
			LOGSIG("Error: EXCEPTION_STACK_OVERFLOW", stderr);
			break;
		default:
			LOGSIG("Error: Unrecognized Exception", stderr);
			break;
	}
	fflush(stderr);
	/* If this is a stack overflow then we can't walk the stack, so just show
	   where the error happened */
	if (EXCEPTION_STACK_OVERFLOW != ExceptionInfo->ExceptionRecord->ExceptionCode)
	{
		windows_print_stacktrace(ExceptionInfo->ContextRecord);
	}
	else
	{
		addr2line(icky_global_program_name, (void*)ExceptionInfo->ContextRecord->Rip);
	}

	return EXCEPTION_EXECUTE_HANDLER;
}

void set_signal_handler( const char *progname )
{
	(void)progname ;
	SetUnhandledExceptionFilter(windows_exception_handler);
}
#else

static void *stack_traces[MAX_STACK_FRAMES];
void posix_print_stack_trace()
{
	int i, trace_size = 0;
	char **messages = (char **)NULL;

	trace_size = backtrace(stack_traces, MAX_STACK_FRAMES);
	messages = backtrace_symbols(stack_traces, trace_size);

	/* skip the first couple stack frames (as they are this function and
	   our handler) and also skip the last frame as it's (always?) junk. */
	// for (i = 3; i < (trace_size - 1); ++i)
	for (i = 0; i < trace_size; ++i) // we'll use this for now so you can see what's going on
	{
		if (addr2line(icky_global_program_name, stack_traces[i]) != 0)
		{
			LOGSIG("  error determining line # for: %s", messages[i]);
		}
	}
	if (messages) { free(messages); } 
}

void posix_signal_handler(int sig, siginfo_t *siginfo, void *context)
{
	(void)context;
	switch(sig)
	{
		case SIGSEGV:
			LOGSIG("Caught SIGSEGV: Segmentation Fault", stderr);
			break;
		case SIGINT:
			LOGSIG("Caught SIGINT: Interactive attention signal, (usually ctrl+c)", stderr);
			break;
		case SIGFPE:
			switch(siginfo->si_code)
			{
				case FPE_INTDIV:
					LOGSIG("Caught SIGFPE: (integer divide by zero)", stderr);
					break;
				case FPE_INTOVF:
					LOGSIG("Caught SIGFPE: (integer overflow)", stderr);
					break;
				case FPE_FLTDIV:
					LOGSIG("Caught SIGFPE: (floating-point divide by zero)", stderr);
					break;
				case FPE_FLTOVF:
					LOGSIG("Caught SIGFPE: (floating-point overflow)", stderr);
					break;
				case FPE_FLTUND:
					LOGSIG("Caught SIGFPE: (floating-point underflow)", stderr);
					break;
				case FPE_FLTRES:
					LOGSIG("Caught SIGFPE: (floating-point inexact result)", stderr);
					break;
				case FPE_FLTINV:
					LOGSIG("Caught SIGFPE: (floating-point invalid operation)", stderr);
					break;
				case FPE_FLTSUB:
					LOGSIG("Caught SIGFPE: (subscript out of range)", stderr);
					break;
				default:
					LOGSIG("Caught SIGFPE: Arithmetic Exception", stderr);
					break;
			}
			break;
		case SIGILL:
			switch(siginfo->si_code)
			{
				case ILL_ILLOPC:
					LOGSIG("Caught SIGILL: (illegal opcode)", stderr);
					break;
				case ILL_ILLOPN:
					LOGSIG("Caught SIGILL: (illegal operand)", stderr);
					break;
				case ILL_ILLADR:
					LOGSIG("Caught SIGILL: (illegal addressing mode)", stderr);
					break;
				case ILL_ILLTRP:
					LOGSIG("Caught SIGILL: (illegal trap)", stderr);
					break;
				case ILL_PRVOPC:
					LOGSIG("Caught SIGILL: (privileged opcode)", stderr);
					break;
				case ILL_PRVREG:
					LOGSIG("Caught SIGILL: (privileged register)", stderr);
					break;
				case ILL_COPROC:
					LOGSIG("Caught SIGILL: (coprocessor error)", stderr);
					break;
				case ILL_BADSTK:
					LOGSIG("Caught SIGILL: (internal stack error)", stderr);
					break;
				default:
					LOGSIG("Caught SIGILL: Illegal Instruction", stderr);
					break;
			}
			break;
		case SIGTERM:
			LOGSIG("Caught SIGTERM: a termination request was sent to the program", stderr);
			break;
		case SIGABRT:
			LOGSIG("Caught SIGABRT: usually caused by an abort() or assert()", stderr);
			break;
		default:
			break;
	}
	posix_print_stack_trace();
	_Exit(1);
}

static uint8_t alternate_stack[SIGALTSTACK_SIZE];

void set_signal_handler( const char *progname )
{
	icky_global_program_name = progname ;

#ifdef RLIMIT_STACK
	/* Before starting the endless recursion, try to be friendly to the user's
	   machine.  On some Linux 2.2.x systems, there is no stack limit for user
	   processes at all.  We don't want to kill such systems.  */
	struct rlimit rl;
	rl.rlim_cur = rl.rlim_max = 0x100000; /* 1 MB */
	setrlimit (RLIMIT_STACK, &rl);
#endif

	/* setup alternate stack */
	{
		stack_t ss ;

		/* malloc is usually used here, I'm not 100% sure my static allocation
		   is valid but it seems to work just fine. */
		ss.ss_sp = alternate_stack;
		ss.ss_size = sizeof( alternate_stack );
		ss.ss_flags = SS_ONSTACK ;

		if (sigaltstack(&ss, NULL) != 0) { err(1, "sigaltstack"); }
	}

	/* register our signal handlers */
	{
		struct sigaction sig_action ;
		sigemptyset( &sig_action.sa_mask );
		sig_action.sa_sigaction = posix_signal_handler;

#ifdef __APPLE__
		/* for some reason we backtrace() doesn't work on osx
		   when we use an alternate stack */
		sig_action.sa_flags = SA_SIGINFO;
#else
		sig_action.sa_flags = SA_SIGINFO | SA_ONSTACK;
#endif

		if (sigaction(SIGSEGV, &sig_action, NULL) != 0) { err(1, "sigaction"); }
		if (sigaction(SIGFPE,  &sig_action, NULL) != 0) { err(1, "sigaction"); }
		if (sigaction(SIGINT,  &sig_action, NULL) != 0) { err(1, "sigaction"); }
		if (sigaction(SIGILL,  &sig_action, NULL) != 0) { err(1, "sigaction"); }
		if (sigaction(SIGTERM, &sig_action, NULL) != 0) { err(1, "sigaction"); }
		if (sigaction(SIGABRT, &sig_action, NULL) != 0) { err(1, "sigaction"); }
	}
}
#endif
