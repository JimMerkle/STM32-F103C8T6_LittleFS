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
// 128K FLASH (STM32-F103RB, or a C8T6 with 128K): application in the lower 64K, LittleFS in the upper 64K
// Must match the LITTLEFS region in STM32F103C8TX_FLASH.ld
#define FLASH_USER_START_ADDR   ADDR_FLASH_PAGE_64   /* Start @ of user Flash area */
#define FLASH_USER_END_ADDR     ADDR_FLASH_PAGE_127 + FLASH_PAGE_SIZE   /* End @ of user Flash area */
#define STM32F103_SECTOR_SIZE	0x400 /* 1K */
#define STM32F103_SECTOR_COUNT  64
#elif 0
// 128K FLASH, LittleFS in the upper 32K only (previous layout)
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

int cl_rename(void);
int cl_cat(void);
int cl_copy(void);
int cl_file_dump(void);
int cl_readspeed(void);

// Records to add into command line interface (command_line.c):
#define LITTLEFS_COMMANDS \
{"ls",         "Directory listing for file system",                         cl_dir}, \
{"mkdir",      "Make Directory",                                            cl_make_dir}, \
{"rm",         "Remove File/Directory (directory must be empty)",           cl_remove}, \
{"makefile",   "Make a file <file name> [size], size bytes of 00..FF pattern",cl_make_file}, \
{"mv",         "Rename file/directory <current name> <new name>",           cl_rename}, \
{"cat",        "Display text file (only printable text)",                   cl_cat}, \
{"filedump",   "Display <file> data as Hexadecimal",                        cl_file_dump}, \
{"cp",         "Copy file <source file name> <destination file name>",      cl_copy}, \
{"readspeed",  "Display time to open, read, and close <file>",              cl_readspeed} \

