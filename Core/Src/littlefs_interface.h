/*
 * littlefs_interface.h
 *
 *  Created on: May 26, 2022
 *      Author: Jim Merkle
 *
 *  Using STM32 HAL interface modules, create an interface between the LittleFS file system
 *  and an STM32's unused FLASH Program Memory
 *  In this example, we will use a fixed FLASH block size, with a fixed address.
 */
#include "main.h" // ADDR_FLASH_PAGE_96

/**********************************************************************************************************************
 * Macro definitions
 **********************************************************************************************************************/
/** This macro is used to suppress compiler messages about a parameter not being used in a function. */
#define PARAMETER_NOT_USED(p) (void) ((p))

#if 1 //
// The following are for an STM32-F103RB, with 128K FLASH:
#define FLASH_USER_START_ADDR   ADDR_FLASH_PAGE_96   /* Start @ of user Flash area */
#define FLASH_USER_END_ADDR     ADDR_FLASH_PAGE_127 + FLASH_PAGE_SIZE   /* End @ of user Flash area */
#define STM32F103_SECTOR_SIZE	0x400 /* 1K */
#define STM32F103_SECTOR_COUNT  32
#else
// The following are for an STM32-F103C8T6, with 64K FLASH:
#define FLASH_USER_START_ADDR   ADDR_FLASH_PAGE_44   /* Start @ of user Flash area */
#define FLASH_USER_END_ADDR     ADDR_FLASH_PAGE_63 + FLASH_PAGE_SIZE   /* End @ of user Flash area */
#define STM32F103_SECTOR_SIZE	0x400 /* 1K */
#define STM32F103_SECTOR_COUNT  20
#endif

int lfs_init(void); // Initialization for LittleFS

// Command Line functions implemented within littlefs_interface.c:
int cl_lfs(void);
int cl_dir(void);
int cl_make_dir(void);
int cl_remove(void);
int cl_rmdir(void);
int cl_make_file(void);
//int cl_make_file_4kb(void);
int cl_rename(void);
int cl_cat(void);
int cl_copy(void);
int cl_file_dump(void);
int cl_readspeed(void);

// Records to add into command line interface (command_line.c):
#define LITTLEFS_COMMANDS \
{"dir",        "Directory listing for file system",                         1, cl_dir}, \
{"mkdir",      "Make Directory",                                            2, cl_make_dir}, \
{"remove",     "Remove File/Directory (directory must be empty)",           2, cl_remove}, \
{"makefile",   "Make a file <file name>",                                   2, cl_make_file}, \
{"rename",     "Rename file/directory <current name> <new name>",           3, cl_rename}, \
{"cat",        "Display text file (only printable text)",                   2, cl_cat}, \
{"type",       "Display text file (only printable text)",                   2, cl_cat}, \
{"copy",       "Copy file <source file name> <destination file name>",      3, cl_copy}, \
{"readspeed",  "Display time to open, read, and close <file>",              2, cl_readspeed} \

