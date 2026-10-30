// Copyright Jim Merkle, 2/17/2020
// File: command_line.h
//
// Command Line Parser defines and prototypes

#ifndef _command_line_h_
#define _command_line_h_

#include <stdio.h>   // printf()
#include <stdlib.h>  // strtol()
#include <string.h>  // memcpy(), memset()

// ANSI Examples: To get black letters on white background use ESC[30;47m
// To get red use ESC[31m, to get bright red use ESC[1;31m
// To reset colors to their defaults, use ESC[39;49m (not supported on some terminals),
// or reset all attributes with ESC[0m.

// Define ANSI colors, to be used within printf() text
// These #defines rely on string concatenation to function as desired

//Foreground Colors:
//30: Black, 31: Red, 32: Green, 33: Yellow, 34: Blue, 35: Magenta, 36: Cyan, and 37: White.
#define COLOR_RED           	"\033[31m"   /* red text */
#define COLOR_GREEN         	"\033[32m"   /* green text */
#define COLOR_YELLOW        	"\033[33m"   /* yellow text */
#define COLOR_BLUE          	"\033[34m"   /* blue text */
#define COLOR_MAGENTA			"\033[35m"   /* magenta text */
#define COLOR_CYAN          	"\033[36m"   /* cyan text */
#define COLOR_WHITE         	"\033[37m"   /* white text */

#define COLOR_RESET             "\033[0m"    /* Reset text color to previous color */

#define COLOR_YELLOW_ON_BLACK "\033[93m\033[40m"
#define COLOR_YELLOW_ON_BLUE  "\033[93m\033[44m"
#define COLOR_YELLOW_ON_GREEN "\033[93m\033[42m"
#define COLOR_YELLOW_ON_RED   "\033[93m\033[41m"
#define COLOR_YELLOW_ON_VIOLET "\033[93m\033[45m"
//#define COLOR_GREEN  "\033[92m"   /* Bright Green text */
//#define COLOR_VIOLET "\033[95m"   /* Bright Violet text */
//#define COLOR_YELLOW "\033[93m"   /* Bright Yellow text */


// Cursor movement
#define _BS  '\b' /*(char)8 */
#define _CR  '\r'
#define _LF  '\n'

// Defines
#define MAXWORDS 10     // support up to 10 (command and parameters)
#define MAXSERIALBUF 64 // Our command line will use a 64 byte buffer

// Externs
extern char buffer[]; // holds command strings from user
extern char * argv[]; // pointers into buffer
extern int argc; // number of words (command & arguments)
extern int __io_getchar(void);             // main.c - read serial input

// Forward declarations
int cl_isWhiteSpace(char c);
int cl_parseArgcArgv(char * inBuf,char **words, int count);
void cl_setup(void);
void cl_loop(void);
void cl_process_buffer(void);

// command line functions
int cl_help(void);
int cl_add(void);
int cl_id(void);
int cl_reset(void);
int cl_timer(void);
int cl_info(void);
int cl_version(void);
int cl_blink(void);
int cl_cls(void);

#endif // _command_line_h_
