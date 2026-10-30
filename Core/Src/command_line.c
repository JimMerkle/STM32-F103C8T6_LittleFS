// Copyright Jim Merkle, 2/17/2020
// Module: command_line.c
//
// Command Line Parser
//
// Using serial interface, receive commands with parameters.
// Parse the command and parameters, look up the command in a table, execute the command.
// Since the command/argument buffer is global with global pointers, each command will parse its own arguments.
// Since no arguments are passed in the function call, all commands will have int command_name(void) prototype.

// Notes:
// The stdio library's stdout stream is buffered by default.  This makes printf() and putchar() work strangely
// for character I/O.  Buffering needs to be disabled for this code module.  See setvbuf() in cl_setup().

#include <string.h>
#include <stdlib.h>
#include <stdint.h>                     // uint8_t
#include <stdio.h>                      // EOF
#include "command_line.h"
#include "main.h"                       // HAL functions and defines
#include "littlefs_interface.h"
#include "version.h"
#include "../ymodem/ymodem.h"
#include "../hexdump/hexdump.h"

// Typedefs
typedef struct {
  char * command;
  char * comment;
  int (*function)(void); // pointer to command function
} COMMAND_ITEM;

const COMMAND_ITEM cmd_table[] = {
    {"?",         "display help menu",                                      cl_help},
    {"help",      "display help menu",                                      cl_help},
    {"cls",       "clear screen",                                           cl_cls},
//  {"add",       "add <number> <number>",                                  cl_add},  // example template
    {"id",        "unique ID",                                              cl_id},
    {"reset",     "reset processor",                                        cl_reset},
    {"info",      "processor info",                                         cl_info},
    {"timer",     "timer test - measure 50ms HAL_Delay()",                  cl_timer},
    {"blink",     "blink <number of blinks>",                               cl_blink},
    HEXDUMP_COMMANDS,
    YMODEM_COMMANDS,
    {"version",   "display firmware version",                               cl_version},
    {"==========","==========================================",             NULL},
    LITTLEFS_COMMANDS,
    {NULL,NULL,NULL}, /* end of table */
};

// Globals:
char buffer[MAXSERIALBUF]; // holds command strings from user
char * argv[MAXWORDS]; // pointers into buffer
int argc; // number of words (command & arguments)

const VERSION_MAJOR_MINOR fw_version = {VERSION_MAJOR, VERSION_MINOR, VERSION_BUILD};

// Enable command line history and line editing.  Comment out to use the simple editor.
// Keys (VT100 / xterm sequences, as sent by Tera Term and PuTTY):
//   Up / Down     - recall previous / next command (the line being typed is kept as a draft)
//   Left / Right  - move cursor within the line, typing inserts at the cursor
//   Home / End    - move cursor to start / end of line
//   Backspace     - delete character before cursor (0x08 or 0x7F)
//   Delete        - delete character at cursor
#define COMMAND_LINE_HISTORY 1

#ifdef COMMAND_LINE_HISTORY
#define HISTORY_DEPTH 10 // remember the last 10 commands (10 * MAXSERIALBUF bytes of RAM)

static char history[HISTORY_DEPTH][MAXSERIALBUF];
static int history_count = 0; // number of valid entries, 0..HISTORY_DEPTH
static int history_head = 0;  // slot that receives the next entry

// Save a command line, skipping empty lines and repeats of the newest entry
static void history_save(const char * line)
{
    if(!line[0]) return;
    if(history_count) {
        int newest = (history_head + HISTORY_DEPTH - 1) % HISTORY_DEPTH;
        if(strcmp(line, history[newest]) == 0) return;
    }
    strncpy(history[history_head], line, MAXSERIALBUF - 1);
    history[history_head][MAXSERIALBUF - 1] = 0;
    history_head = (history_head + 1) % HISTORY_DEPTH;
    if(history_count < HISTORY_DEPTH) history_count++;
}

// Return a history entry by age: 1 = newest, history_count = oldest
static const char * history_get(int age)
{
    return history[(history_head - age + HISTORY_DEPTH) % HISTORY_DEPTH];
}
#endif // COMMAND_LINE_HISTORY

void cl_setup(void) {
  // The STM32 development environment's stdio library provides buffering of stdout stream by default.  Turn it off!
  setvbuf(stdout, NULL, _IONBF, 0);
  // Turn on yellow text, print greeting, reset attributes
  printf("\n" COLOR_YELLOW "Command Line parser, Version: %u.%u.%u, Date: %s" COLOR_RESET "\n", fw_version.major, fw_version.minor, fw_version.build, __DATE__);
  printf(COLOR_YELLOW "Enter \"help\" or \"?\" for list of commands" COLOR_RESET "\n");
  printf(">"); // initial prompt
}

#ifdef COMMAND_LINE_HISTORY
// Line editor state.  All of it is static, so an escape sequence split across
// cl_loop() calls (bytes still arriving) is handled correctly.
static int line_len = 0;  // characters in buffer[], buffer[line_len] is always 0
static int cursor = 0;    // cursor position, 0..line_len
static int history_age = 0; // 0 = editing a new line, else age of the recalled entry
static char draft[MAXSERIALBUF]; // the new line, saved while browsing history

// Escape sequence parser states
enum { ESC_NONE, ESC_START, ESC_CSI, ESC_SS3 };
static int esc_state = ESC_NONE;
static int esc_param = 0; // numeric parameter of ESC [ n ~ sequences

// Redraw the whole line after the prompt, erase anything left over, then put the cursor back
static void cl_redraw(void)
{
    printf("\r>%s\033[K", buffer);
    if(line_len > cursor) printf("\033[%dD", line_len - cursor);
}

// Replace the line with a copy of str, cursor at the end
static void cl_set_line(const char * str)
{
    strncpy(buffer, str, MAXSERIALBUF - 1);
    buffer[MAXSERIALBUF - 1] = 0;
    line_len = cursor = strlen(buffer);
    cl_redraw();
}

static void cl_history_up(void)
{
    if(history_age >= history_count) return; // already at the oldest entry
    if(history_age == 0) strcpy(draft, buffer); // keep the line being typed
    history_age++;
    cl_set_line(history_get(history_age));
}

static void cl_history_down(void)
{
    if(history_age == 0) return; // not browsing
    history_age--;
    cl_set_line(history_age ? history_get(history_age) : draft);
}

// Delete the character at position pos, shifting the rest of the line left
static void cl_delete_at(int pos)
{
    memmove(&buffer[pos], &buffer[pos + 1], line_len - pos); // includes the terminating 0
    line_len--;
}

// Handle the final character of an escape sequence
static void cl_escape_key(int c)
{
    switch(c) {
        case 'A': cl_history_up(); break;    // Up arrow
        case 'B': cl_history_down(); break;  // Down arrow
        case 'C':                            // Right arrow
            if(cursor < line_len) { cursor++; printf("\033[C"); }
            break;
        case 'D':                            // Left arrow
            if(cursor > 0) { cursor--; printf("\033[D"); }
            break;
        case 'H':                            // Home
            cursor = 0; cl_redraw();
            break;
        case 'F':                            // End
            cursor = line_len; cl_redraw();
            break;
        case '~':                            // ESC [ n ~ keys
            if(esc_param == 1 || esc_param == 7) { cursor = 0; cl_redraw(); }               // Home
            else if(esc_param == 4 || esc_param == 8) { cursor = line_len; cl_redraw(); }   // End
            else if(esc_param == 3 && cursor < line_len) { cl_delete_at(cursor); cl_redraw(); } // Delete
            break;
        default: // unsupported key - ignore
            break;
    }
}

// Check for data available from USART interface.  If none present, just return.
// If data available, process it (add it to character buffer if appropriate)
void cl_loop(void)
{
    int c;

    // Spin, reading characters until EOF character is received (no data), or
    // a <carriage return> / <line feed> character is received.
    while(1) {
      c = __io_getchar();
      if(c == EOF) return; // non-blocking - return

      // Escape sequences: ESC [ <digits> <final>  or  ESC O <final>
      if(esc_state == ESC_START) {
          if(c == '[') { esc_state = ESC_CSI; esc_param = 0; }
          else if(c == 'O') esc_state = ESC_SS3;
          else esc_state = ESC_NONE; // not a sequence we know - drop it
          continue;
      }
      if(esc_state == ESC_CSI || esc_state == ESC_SS3) {
          if(esc_state == ESC_CSI && c >= '0' && c <= '9') {
              esc_param = esc_param * 10 + (c - '0');
              continue;
          }
          esc_state = ESC_NONE;
          cl_escape_key(c);
          continue;
      }

      switch(c) {
          case 0x1B: // ESC - start of an escape sequence
            esc_state = ESC_START;
            break;
          case _CR:
          case _LF:
            if(line_len) {
                printf("\n"); // newline
                history_save(buffer);  // save before parsing, the parser splits buffer[] in place
                cl_process_buffer();   // process the null terminated buffer
            }
            printf("\n>");
            line_len = cursor = 0;
            buffer[0] = 0;
            history_age = 0;
            esc_state = ESC_NONE;
            return;
          case _BS:
          case 0x7F: // DEL - most terminals send this for the Backspace key
            if(cursor < 1) break;
            cursor--;
            cl_delete_at(cursor);
            if(cursor == line_len) printf("\b \b"); // at end of line - simple erase
            else cl_redraw();
            break;
          default:
            // If printable character and room in the buffer, insert it at the cursor
            if(line_len < (MAXSERIALBUF - 1) && c >= ' ' && c <= '~') {
                if(cursor == line_len) {
                    buffer[line_len++] = (char) c;
                    buffer[line_len] = 0;
                    cursor++;
                    printf("%c",c); // write character to terminal
                } else {
                    memmove(&buffer[cursor + 1], &buffer[cursor], line_len - cursor + 1);
                    buffer[cursor++] = (char) c;
                    line_len++;
                    cl_redraw();
                }
            }
      } // switch
  } // while(1)
} // cl_loop()

#else // simple editor

// Check for data available from USART interface.  If none present, just return.
// If data available, process it (add it to character buffer if appropriate)
void cl_loop(void)
{
    static int index = 0; // index into global buffer
    int c;

    // Spin, reading characters until EOF character is received (no data), buffer is full, or
    // a <line feed> character is received.  Null terminate the global string, don't return the <LF>
    while(1) {
      c = __io_getchar();
      switch(c) {
          case EOF:
              return; // non-blocking - return
          case _CR:
          case _LF:
            buffer[index] = 0; // null terminate
            if(index) {
        		printf("\n"); // newline
            	cl_process_buffer(); // process the null terminated buffer
            }
    		printf("\n>");
            index = 0; // reset buffer index
            return;
          case _BS:
            if(index<1) continue;
            printf("\b \b"); // remove the previous character from the screen and buffer
            index--;
            break;
          default:
        	if(index<(MAXSERIALBUF - 1) && c >= ' ' && c <= '~') {
                printf("%c",c); // write character to terminal
                buffer[index] = (char) c;
                index++;
        	}
      } // switch
  } // while(1)
  return;
} // cl_loop()
#endif // COMMAND_LINE_HISTORY

void cl_process_buffer(void)
{
    argc = cl_parseArgcArgv(buffer, argv, MAXWORDS);
    // Display each of the "words" / command and arguments
    //for(int i=0;i<argc;i++)
    //  printf("%d >%s<\n",i,argv[i]);
    if (argc) {
        // At least one "word" / argument found
        // See if command has a match in the command table
        // If null command pointer found, exit for-loop
        // Separator rows (null function pointer) are skipped, so they can't be called
        int cmdIndex;
        for (cmdIndex = 0; cmd_table[cmdIndex].command; cmdIndex++) {
            if (cmd_table[cmdIndex].function && strcmp(argv[0], cmd_table[cmdIndex].command) == 0) {
                // We found a match in the table
                // Call the function associated with the command
                (*cmd_table[cmdIndex].function)();
                break; // exit for-loop
            }
        } // for-loop
          // If we compared all the command strings and didn't find the command, or we want to fake that event
        if (!cmd_table[cmdIndex].command) {
            printf("Command \"%s\" not found\n", argv[0]);
        }
    } // At least one "word" / argument found
}

// Return true (non-zero) if character is a white space character
int cl_isWhiteSpace(char c) {
  if(c==' ' || c=='\t' ||  c=='\r' || c=='\n' )
    return 1;
  else
    return 0;
}

// Parse string into arguments/words, returning count
// Required an array of char pointers to store location of each word, and number of strings available
// "count" is the maximum number of words / parameters allowed
int cl_parseArgcArgv(char * inBuf,char **words, int count)
{
  int wordcount = 0;
  while(*inBuf) {
    // We have at least one character
    while(cl_isWhiteSpace(*inBuf)) inBuf++; // remove leading whitespace
    if(*inBuf) {// have a non-whitespace
      if(wordcount < count) {
        // If pointing at a double quote, need to remove/advance past the first " character
        // and find the second " character that goes with it, and remove/advance past that one too.
        if(*inBuf == '\"' && inBuf[1]) {
            // Manage double quoted word
            inBuf++; // advance past first double quote
            words[wordcount]=inBuf; // point at this new word
            wordcount++;
            while(*inBuf && *inBuf != '\"') inBuf++; // move to end of word (next double quote)
        } else {
            // normal - not double quoted string
            words[wordcount]=inBuf; // point at this new word
            wordcount++;
            while(*inBuf && !cl_isWhiteSpace(*inBuf)) inBuf++; // move to end of word
        }
        if(cl_isWhiteSpace(*inBuf) || *inBuf == '\"') { // null terminate this word
          *inBuf=0;
          inBuf++;
        }
      } // if(wordcount < count)
      else {
        *inBuf=0; // null terminate string
        break; // exit while-loop
      }
    }
  } // while(*inBuf)
  return wordcount;
} // parseArgcArgv()

// We may want to add a comment/description field to the table to describe each command
int cl_help(void) {
    printf("Help - command list\n");
    printf("Command     Comment\n");
    // Walk the command array, displaying each command
    // Continue until null function pointer found
    for (int i = 0; cmd_table[i].command; i++) {
        printf("%-12s%s\n",cmd_table[i].command,cmd_table[i].comment);
    }
    printf("\n");
    return 0;
}

int cl_add(void) {
    // verify argument count
    if(argc < 3) {
        printf("\r\nInvalid Arg cnt: %d Expected: %d\n", argc - 1, 2);
        return 0;
    }
    printf("add..  A: %s  B: %s\n", argv[1], argv[2]);
    int A = (int) strtol(argv[1], NULL, 0); // allow user to use decimal or hex
    int B = (int) strtol(argv[2], NULL, 0);
    int ret = A + B;
    printf("returning %d\n\n", ret);
    return ret;
}

//Unique device ID register U_ID (96 bits)
//Base address: 0x1FFFF7E8 (STM32-F103RB), or 0x1FF0F420 (STM32-F767ZI)
//TODO: Make the address selection determined at compile time or run time
int cl_id(void)
{
	//volatile uint8_t * p_id = (uint8_t *)0x1FF0F420; // STM32-F767ZI
#ifdef STM32F103xB
	volatile uint8_t * p_id = (uint8_t *)0x1FFFF7E8; // STM32-F103RB
	printf("Unique ID: 0x");
	for(int i=11;i>=0;i--)
		printf("%02X",p_id[i]); // display bytes from high byte to low byte
	printf("\n");
#else
	printf("Unique ID: - Not STM32F103xB\n");
#endif // STM32F103xB

	return 0;
}

//Memory size register
//30.1.1 Flash size register
//Base address: 0x1FFF F7E0
//Contains number of K bytes of FLASH, IE: 0x80 = 128K bytes flash

//Address: 0x1FFFF7E0 (STM32-F103RB), or 0x1FF0F442 (STM32-F767ZI)
//Only 16-bits access supported. Read-only.
//TODO: Make the address selection determined at compile time or run time
int cl_info(void)
{
	//volatile uint16_t * p_k_bytes_flash = (uint16_t *)0xE0042000; // STM32-F767ZI
#ifdef STM32F103xB
	volatile uint16_t * p_k_bytes_flash = (uint16_t *)0x1FFFF7E0; // STM32-F103RB
#endif // STM32F103xB
	printf("Processor Flash: %uK bytes\n",*p_k_bytes_flash);
	return 0;
}

// Return appropriate error code string
// Yes, we could just return the HAL strings vs copy them...
char * PrintHalStatus(int status)
{
    static char s_status[20];
    switch(status) {
        case HAL_OK:
          sprintf(s_status,"HAL_OK");
          break;
        case HAL_ERROR:
          sprintf(s_status,"HAL_ERROR");
          break;
        case HAL_BUSY:
          sprintf(s_status,"HAL_BUSY");
          break;
        case HAL_TIMEOUT:
          sprintf(s_status,"HAL_TIMEOUT");
          break;
        default:
          sprintf(s_status,"Error %d",status);
          break;
    } // switch
    return s_status;
} // PrintHalStatus()

// Reset the processor
int cl_reset(void) {
    NVIC_SystemReset(); // CMSIS Cortex-M3 function - see Drivers/CMSIS/Include/core_cm3.h
    // The above function has its own spin loop to wait for completion
    //while (1) ; // wait here until reset completes
    return 0;
}

// Blink the LED <number of times>
int cl_blink(void)
{
	// verify argument count
	if(argc < 2) {
		printf("\nExpect <number of blinks>\n");
		return 0;
	}
	unsigned blinks = strtol(argv[1],NULL,0); // allow user to use decimal or hex

	for(unsigned i=0;i<blinks;i++)
	{
		HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_RESET); // Active low with Blue Pill, active high with NUCLEO
		HAL_Delay(500);
		HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_SET);
		HAL_Delay(500);
	}
	return 0;
}

// Perform a timer4 test.  Is the us timer tracking System Ticks?
// Alternatively, if used a GPIO, we could toggle a pin after X micro-seconds
int cl_timer(void)
{
    printf("%s(), Timing HAL_Delay(50)\n",__func__);
    volatile TIM_TypeDef *TIMx = TIM4;
    uint32_t start_ticks = HAL_GetTick();
    uint32_t start_us = TIMx->CNT; // read us hardware timer
    HAL_Delay(50); // delay 50ms
    uint32_t stop_us = TIMx->CNT; // read us hardware timer
    uint32_t stop_ticks = HAL_GetTick();
    // Report results
    printf("HAL_GetTick() time: %lu ms\n",stop_ticks-start_ticks);
    if(stop_us < start_us) stop_us += 1<<16; // roll-over, add 16-bit roll-over offset
    printf("TIMx->CNT time: %lu us\n",stop_us - start_us);
    return 0;
}

// Clear the screen
int cl_cls(void) {
   // VT-100 command to clear the screen and move cursor to upper left corner
   printf("\x1B[2J\x1B[H");
   return 0;
}

int cl_version(void)
{
	printf("Version %u.%u.%u\n",fw_version.major,fw_version.minor,fw_version.build);
	return 0;
}
