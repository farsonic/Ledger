/*
 * stack.c - the stack Ledger asks for: vbcc's startup code switches to a stack this big when the
 * shell's is smaller. Formulas are worked out recursively (cells computed inside one another, LAMBDA
 * calls, nested IFs), up to MAXCALL levels deep (calc.c), each level a kilobyte or so.
 */
#include <stddef.h>

size_t __stack = 262144;
