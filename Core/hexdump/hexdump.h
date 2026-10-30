// Copyright Jim Merkle, 2/17/2020
// File: hexdump.h
//
// Hexadecimal memory / register display routine

#ifndef _hexdump_h_
#define _hexdump_h_

#include <stdint.h>

// Display count bytes at address, 16 per line with an ASCII column.
// address_value is the number printed at the start of each line (memory address or file offset).
void hexdump(void * address, uint32_t count, uint32_t address_value);
void memory_dump(void * address, uint32_t count); // line numbers = memory addresses
void file_dump(void * address, uint32_t count);   // line numbers start at 0

// Command line interface
int cl_dump(void);

// Set of commands for the command line table (see command_line.c cmd_table[])
#define HEXDUMP_COMMANDS \
{"dump",      "dump <address> [count], display memory, count default 256",  cl_dump}

#endif // _hexdump_h_
