/*
 * littlefs_interface.c
 *
 *  Created on: May 26, 2022
 *      Author: Jim Merkle
 *
 *  Using STM32 HAL interface modules, create an interface between the LittleFS file system
 *  and an STM32's unused FLASH Program Memory
 *  In this example, we will use a fixed FLASH block size, with a fixed address.
 */

#include <stdio.h> // printf()
// #include <stdlib.h> // malloc()
#include <string.h> // memcpy()
#include "main.h"
#include "lfs.h"
#include "littlefs_interface.h"
#include "command_line.h" // arc, argv[]

// global variables used by the file system
extern lfs_t lfs;
//extern  lfs_file_t file;
//extern  lfs_dir_t dir;
//extern  lfs_info info;
extern  void init_lfs_cfg(void);
extern  struct lfs_config lfs_cfg;

void memory_dump(void * address, uint32_t count); // hexdump.c
void file_dump(void * address, uint32_t count);  // hexdump.c
//void hexdump(void * address, uint32_t count, uint32_t address_value); // hexdump.c

// Read a region in a FLASH block. Negative error codes are propagated to the user.
int lfs_read(const struct lfs_config *c, lfs_block_t block, lfs_off_t off, void *buffer, lfs_size_t size)
{
	PARAMETER_NOT_USED(c);
	if(!buffer || !size) {
		printf("%s Invalid parameter!\r\n",__func__);
		return LFS_ERR_INVAL;
	}

	lfs_block_t address = FLASH_USER_START_ADDR + (block * STM32F103_SECTOR_SIZE + off);
	//printf("+%s(Addr 0x%06lX, Len 0x%04lX)\r\n",__func__,address,size);
	//hexdump((void *)address,size);
	memcpy(buffer, (void *)address, size);

	return LFS_ERR_OK;
}

// Use 64 bit (8 byte) long data blocks to help increase efficiency
int lfs_prog(const struct lfs_config *c, lfs_block_t block, lfs_off_t off, const void *buffer, lfs_size_t size)
{
	PARAMETER_NOT_USED(c);
	lfs_block_t address = FLASH_USER_START_ADDR + (block * STM32F103_SECTOR_SIZE + off);
	HAL_StatusTypeDef hal_rc = HAL_OK;
	uint32_t block_count = size / 8;

	//printf("+%s(Addr 0x%06lX, Len 0x%04lX)\r\n",__func__,address,size);
	//hexdump((void *)address,size);
  	/* Program the user Flash area word by word
  	(area defined by FLASH_USER_START_ADDR and FLASH_USER_END_ADDR) ***********/

  	uint64_t data_source;

  	for(uint32_t i=0;i<block_count;i++)
  	{
  		memcpy(&data_source,buffer,8); // load the 64-bit source from the buffer
  		hal_rc = HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, address, data_source);
  		if (hal_rc == HAL_OK)
  		{
  			address += 8;
  			buffer = (uint8_t *)buffer + 8;
  		}
  		else
  		{
  		  /* Error occurred while writing data in Flash memory.
  			 User can add here some code to deal with this error */
  			printf("Program Error, 0x%X\n",hal_rc);

  		} // else
  	} // for
  	//printf("-%s\n",__func__);

  	return hal_rc == HAL_OK?LFS_ERR_OK:LFS_ERR_IO; // If HAL_OK, return LFS_ERR_OK, else return LFS_ERR_IO
}

// Erase a single FLASH sector (block)
int lfs_erase(const struct lfs_config *c, lfs_block_t block)
{
	PARAMETER_NOT_USED(c);
	lfs_block_t address = FLASH_USER_START_ADDR + (block * STM32F103_SECTOR_SIZE);
	//printf("+%s(Addr 0x%06lX)\r\n",__func__,address);

	HAL_StatusTypeDef hal_rc;
	FLASH_EraseInitTypeDef EraseInitStruct;
	uint32_t PAGEError = 0;

	/* Fill EraseInit structure*/
	EraseInitStruct.TypeErase   = FLASH_TYPEERASE_PAGES;
	EraseInitStruct.PageAddress = address;
	EraseInitStruct.NbPages     = 1;
	hal_rc = HAL_FLASHEx_Erase(&EraseInitStruct, &PAGEError);
//	if (hal_rc != HAL_OK)
//	{
//		printf("%s ERROR 0x%X\n",__func__,hal_rc);
//	}
//	else
//		printf("%s SUCCESS\n",__func__);

  	return hal_rc == HAL_OK?LFS_ERR_OK:LFS_ERR_IO; // If HAL_OK, return LFS_ERR_OK, else return LFS_ERR_IO
}


int lfs_sync(const struct lfs_config *c)
{
  PARAMETER_NOT_USED(c);
  // write function performs no caching.  No need for sync.
  //printf("+%s()\r\n",__func__);
  return LFS_ERR_OK;
  //return LFS_ERR_IO;
}

#define READ_SIZE       			1   // Minimum size of a block read. All read operations will be a multiple of this value.
#define PROGRAM_SIZE				8   // Minimum size of a block program. All program operations will be a multiple of this value.
#define CACHE_SIZE 				64 // Must be a multiple of the read and program sizes, and a factor of the block size.
#define LOOKAHEAD_CACHE_SIZE 	64 // Must be a multiple of 8.

// variables used by the file system
lfs_t lfs;

// Buffers used by this LittleFS implementation:
uint32_t read_buffer[CACHE_SIZE/sizeof(uint32_t)];              // Uses CACHE_SIZE for size, align the buffer to 4 byte boundary
uint32_t program_buffer[CACHE_SIZE/sizeof(uint32_t)];           // Uses CACHE_SIZE for size, align the buffer to 4 byte boundary
uint32_t lookahead_buffer[LOOKAHEAD_CACHE_SIZE/sizeof(uint32_t)]; // 32-bit alignment, multiple of 8 bytes

struct lfs_config lfs_cfg =
{
    .context = NULL,     // not implemented
    .read = lfs_read,    // read function
    .prog = lfs_prog,    // program function
    .erase = lfs_erase,  // erase function
    .sync = lfs_sync,    // sync function
    .read_size = 1,      // minimum read size (Our Flash interface supports single byte reads)
    .prog_size = 8,      // minimum program size (Our Flash interface supports 8 byte writes)
    .block_size = STM32F103_SECTOR_SIZE,       // block_size - 4KByte erase sectors
    .block_count = STM32F103_SECTOR_COUNT,     // block_count - number of sectors
    .block_cycles = 256,                 // block_cycles - suggested value: 100 - 1000
    .cache_size = CACHE_SIZE,            // cache_size - multiple of read and program block size
    .lookahead_size = LOOKAHEAD_CACHE_SIZE, // lookahead_size (multiple of 8)

	// The following three buffer pointers, if set to NULL, a buffer will be malloc'ed, using the size provided
    .read_buffer = &read_buffer, 		// read_buffer
    .prog_buffer = &program_buffer, 	// prog_buffer
    .lookahead_buffer = &lookahead_buffer,  // lookahead_buffer (if null, a buffer will be malloc'ed)

    .name_max = LFS_NAME_MAX,           // name_max
    .file_max = LFS_FILE_MAX,           // file_max
    .attr_max = LFS_ATTR_MAX,           // attr_max
};

// Initialize file system
int lfs_init(void) {
    //printf("+%s()\r\n",__func__);
    int err;
//    char buf[30]; // read bootcount.txt file into this buffer
//    lfs_file_t file;

    // Test the buffers.  Are they all non-zero?
    if(!lfs_cfg.read_buffer || !lfs_cfg.prog_buffer) {
        printf("Buffer problems!! read_buffer: 0x%08lX, program_buffer: 0x%08lX\r\n",
          (uint32_t)lfs_cfg.read_buffer, (uint32_t)lfs_cfg.prog_buffer);
        return -1;
    }

    // mount the filesystem
    //printf("%s, mounting filesystem\n",__func__);
    err = lfs_mount(&lfs, &lfs_cfg);

    // reformat if we can't mount the filesystem
    // this should only happen on the first boot
    if (err) {
        printf("%s: lfs_mount() error, reformatting FS\r\n",__func__);
        err = lfs_format(&lfs, &lfs_cfg);
        printf("lfs_format - returned: %d\r\n",err);
        err = lfs_mount(&lfs, &lfs_cfg);
        printf("lfs_mount - returned: %d\r\n",err);
    }

#if 0
    // The following block of code implements a "Boot Count", using a file in the file system.
    // It reads a number (as a string) from a file, displays the value, increments the number,
    // and writes the number back to the file (as a string).
    // read current count
    uint32_t boot_count = 0; // initial default value
    err = lfs_file_open(&lfs, &file, "boot_count.txt", LFS_O_RDWR | LFS_O_CREAT);
    printf("lfs_file_open - returned: %d\r\n",err);
    err = lfs_file_read(&lfs, &file, &buf, sizeof(buf));
    // Convert boot count string in "buf" into number
    boot_count = (uint32_t)strtol(buf,NULL,10); // assume decimal number
    printf("lfs_file_read - returned: %d\r\n",err);
    printf("%s() read boot_count: %s\r\n",__func__,buf);
    // update boot count
    boot_count += 1;
    printf("%s() writing boot_count: %u\r\n",__func__,boot_count);
    lfs_file_rewind(&lfs, &file);
    sprintf(buf,"%lu",boot_count);
    lfs_file_write(&lfs, &file, &buf, strlen(buf)); // Don't write null termination

    // remember, the storage is not updated until the file is closed successfully
    lfs_file_close(&lfs, &file);

    // print the new boot count
    printf("boot_count: %u\r\n", boot_count);
#endif

//    // release any resources we were using
//    lfs_unmount(&lfs);

    //printf("-%s()\r\n",__func__);
    return err;
}

//=================================================================================================
// Command Line functions that interface with LittleFS
//=================================================================================================

// Display a file system directory
int cl_dir(void)
{
//	InitFlashInterface(); // Make sure QSPI FLASH interface is configured
//    init_lfs_cfg();
//	lfs_mount(&lfs,&lfs_cfg);
	lfs_dir_t dir;
	struct lfs_info info;
	char * directory = (char *)"/";

	// By default (no parameters), display root directory, else, display contents of directory name provided
	if(argc > 1)
		directory = argv[1];

	// Once open, a directory can be used with read to iterate over files.
	// Returns a negative error code on failure.
	int lfs_status = lfs_dir_open(&lfs, &dir, directory);
	if(LFS_ERR_OK != lfs_status) {
		printf("Directory \"%s\" not found\n",directory);
		return 1;
	}

	printf("Directory of \"%s\":\n",directory);
	uint32_t total_bytes = 0;
	uint32_t file_count = 0;
	// When iterating through files & directories within a directory, LittleFS will always present
	// "." and ".." as directories within the current directory.
	while(lfs_dir_read(&lfs, &dir, &info) > 0) {
		// Directory info update, display data
		if(info.type == LFS_TYPE_DIR)       printf("<DIR>         %s\n",info.name);
		else if(info.type == LFS_TYPE_REG)  {printf("%13lu %s\n",info.size,info.name); total_bytes += info.size; file_count++;} // display file info, add to total_bytes, add to file count
		else printf("unknown type: %u\n",info.type);
	}
	lfs_dir_close(&lfs, &dir);

	// Display totals and expected space remaining
	// Calculate number of 1024 byte blocks used and subtract from number of blocks allocated for the file system
	lfs_ssize_t blocks_used = lfs_fs_size(&lfs);
	uint32_t bytes_remaining = (STM32F103_SECTOR_COUNT - blocks_used) * STM32F103_SECTOR_SIZE;
	//printf("\nFile count: %lu\nBytes total: %lu\nBytes remaining %lu\n",file_count,total_bytes,bytes_remaining);
	printf("\nFile count: %lu\nBytes total: %lu\nSectors Used: %lu\nBytes remaining %lu\n",file_count,total_bytes,blocks_used,bytes_remaining);
 	return 0;
}

// Make a directory..  Required 1 argument, the directory name
int cl_make_dir(void)
{
    int retval = lfs_mkdir(&lfs, argv[1]);
    if(retval != LFS_ERR_OK) {
        printf("%s: Error creating directory \"%s\"\n",__func__,argv[1]);
        return retval;
    }

    printf("Created directory: \"%s\"\n",argv[1]);
    return LFS_ERR_OK;
}

// Remove a file / directory..  Required 1 argument, the directory name
int cl_remove(void)
{
    int retval = lfs_remove(&lfs, argv[1]);
    if(retval != LFS_ERR_OK) {
        printf("%s: Error removing \"%s\"\n",__func__,argv[1]);
        return retval;
    }

    printf("File / directory, \"%s\", removed\n",argv[1]);
    return LFS_ERR_OK;
}

// Make a file..  Required 1 argument, the file name
int cl_make_file(void)
{
    lfs_file_t file;
    char buffer[LFS_NAME_MAX]; // to store the stuff we will write to the file

    // Returns a negative error code on failure.
    int retval = lfs_file_open(&lfs, &file,
        argv[1], LFS_O_RDWR | LFS_O_CREAT);

    if(retval != LFS_ERR_OK) {
        printf("%s: Error creating file \"%s\"\n",__func__,argv[1]);
        return retval;
    }

    // Let's put the files name into the file
    sprintf(buffer,"Filename: %s",argv[1]);
    int length = strlen(buffer);
    printf("Writing: \"%s\", len %d, into file: \"%s\"\n",buffer,length,argv[1]);
    // return value will be number of bytes written to file
    retval = lfs_file_write(&lfs, &file, buffer, length);
    if(retval < LFS_ERR_OK) {
        printf("%s: Error writing file \"%s\"\n",__func__,argv[1]);
    }

    // remember the storage is not updated until the file is closed successfully
    // even if error writing, close file before returning
    lfs_file_close(&lfs, &file);

    printf("Created file: \"%s\"\n",argv[1]);
    return LFS_ERR_OK;
}
#if 0
// Make a 4KByte file..  Required 1 argument, the file name
// Record the time to create and store the file
int cl_make_file_4kb(void)
{
    lfs_file_t file;

    // Returns a negative error code on failure.
    int retval = lfs_file_open(&lfs, &file,
        argv[1], LFS_O_RDWR | LFS_O_CREAT);

    if(retval != LFS_ERR_OK) {
        printf("%s: Error creating file \"%s\"\n",__func__,argv[1]);
        return retval;
    }

    // Load the file with incrementing data
    // Create a 256byte buffer with a know data pattern
    uint8_t buf[256];
    for(unsigned i=0;i<sizeof(buf);i++)
    		buf[i] = (uint8_t)i;

    volatile TIM_TypeDef *TIMx = TIM4;
    uint32_t start_us = TIMx->CNT; // read us hardware timer

    // Write the buffer to the file 16 times
    for(unsigned count=0;count<16;count++) {
		retval = lfs_file_write(&lfs, &file, buf, sizeof(buf));
		if(retval < LFS_ERR_OK) {
			printf("%s: Error writing file \"%s\"\n",__func__,argv[1]);
			break;
		}
    }

    // remember the storage is not updated until the file is closed successfully
    // even if error writing, close file before returning
    lfs_file_close(&lfs, &file);

    uint32_t stop_us = TIMx->CNT; // read us hardware timer
    if(stop_us < start_us) stop_us += 1<<16; // roll-over, add 16-bit roll-over offset

    printf("Created file: \"%s\", Time: %lu us\n",argv[1],stop_us-start_us);
    return retval;
} // cl_make_file_4kb()
#endif

// Display file - Type...  Requires 1 argument, the filename
int cl_cat(void)
{
    lfs_file_t file;
    char buffer[120]; // buffer to hold a line+ from the file

    // Returns a negative error code on failure.
    int retval = lfs_file_open(&lfs, &file,
        argv[1], LFS_O_RDONLY);

    if(retval != LFS_ERR_OK) {
        printf("%s: Error opening file \"%s\"\n",__func__,argv[1]);
        return retval;
    }
    printf("Displaying file \"%s\":\n",argv[1]);
    // Begin looping, loading the line buffer and displaying text from it,
    //  until we have displayed all the text from the file.
    int bytesread;
    char c;
    do {
        bytesread = lfs_file_read(&lfs, &file, buffer, sizeof(buffer));
        if(bytesread < LFS_ERR_OK) {
            printf("%s: Error reading file \"%s\"\n",__func__,argv[1]);
        }
        int i=0; // index within line buffer
        while(i<bytesread) {
            c = buffer[i++]; // post increment
            if((c >= ' ' && c <= '~') || c=='\r' || c=='\n' || c=='\t')
                printf("%c",c);
            // for now, ignore everything else
        } // display the characters held in buffer
    } while(bytesread); // keep looping as long as we keep getting data from file

    // Close file before returning
    lfs_file_close(&lfs, &file);
    printf("\n\n");

    return LFS_ERR_OK;
}

#if 0
// Display file data as hexadecimal - Requires 1 argument, the filename
int cl_file_dump(void)
{
    lfs_file_t file;
    uint32_t file_offset = 0;
    char buffer[512]; // buffer to hold file data

    // Returns a negative error code on failure.
    int retval = lfs_file_open(&lfs, &file,
        argv[1], LFS_O_RDONLY);

    if(retval != LFS_ERR_OK) {
        printf("%s: Error opening file \"%s\"\n",__func__,argv[1]);
        return retval;
    }
    printf("Displaying file \"%s\":\n",argv[1]);
    // Begin looping, loading the file buffer and displaying hex data from it,
    //  until we have displayed the whole file.
    int bytesread;
    do {
        bytesread = lfs_file_read(&lfs, &file, buffer, sizeof(buffer));
        if(bytesread < LFS_ERR_OK) {
            printf("%s: Error reading file \"%s\"\n",__func__,argv[1]);
        }
    	// Use our favorite Hex Dump routine to display the memory
        hexdump(buffer, bytesread, file_offset); // hexdump.c
        file_offset += bytesread; // update offset for next pass
    } while(bytesread); // keep looping as long as we keep getting data from file

    // Close file before returning
    lfs_file_close(&lfs, &file);
    printf("\n\n");

    return LFS_ERR_OK;
}
#endif

// Copy file - Source - Destination
int cl_copy(void)
{
    lfs_file_t source;
    lfs_file_t destination;
    char buffer[1024]; // buffer to copy data

    // Open source file, read only
    // Returns a negative error code on failure.
    int32_t retval = lfs_file_open(&lfs, &source, argv[1], LFS_O_RDONLY);
    if(retval != LFS_ERR_OK) {
        printf("%s: Error opening source file \"%s\", %ld\n",__func__,argv[1],retval);
        return retval;
    }

    // Open destination file, write only, file must not already exist
    // Returns a negative error code on failure.
    retval = lfs_file_open(&lfs, &destination, argv[2], LFS_O_WRONLY | LFS_O_CREAT | LFS_O_EXCL);
    if(retval != LFS_ERR_OK) {
        printf("%s: Error opening destination file \"%s\", %ld\n",__func__,argv[2],retval);
        // Close source file
        lfs_file_close(&lfs, &source);
        return retval;
    }

    // Both files are open and ready to begin the copy process
    int32_t bytes_read;

    do {
    	// read data from source file
        bytes_read = lfs_file_read(&lfs, &source, buffer, sizeof(buffer));
        if(bytes_read < LFS_ERR_OK) {
            printf("%s: Error reading file \"%s\"\n",__func__,argv[1]);
        	break; // exit do-while loop
        }
        // write data to destination file
		retval = lfs_file_write(&lfs, &destination, buffer, bytes_read);
		if(retval < LFS_ERR_OK) {
			printf("%s: Error writing file \"%s\"\n",__func__,argv[2]);
			break;
		}
    } while(bytes_read); // continue looping while lfs_file_read() returns data

    // Close files before returning
    lfs_file_close(&lfs, &source);
    lfs_file_close(&lfs, &destination);
    printf("\n\n");

    return LFS_ERR_OK;
} // cl_copy()


// Rename a file / directory.  If directory, it must be empty.
// This command requires 2 command line arguments, <current name> <new name>
int cl_rename(void)
{
    // Returns a negative error code on failure.
    int retval = lfs_rename(&lfs, argv[1], argv[2]);

    if(retval != LFS_ERR_OK) {
        printf("Unable to rename \"%s\" to \"%s\"\n",argv[1],argv[2]);
        printf("If directory, it MUST be empty!\n");
        return retval;
    }

    printf("Rename success\n");
    return retval;
} // cl_rename()

// Assume timer 4 is enabled and configured to clock at 1us (1MHz) rate
// This command requires 1 command line argument, <file name>
// The time it takes to open the file, read the file, and close the file will be measured and reported.
int cl_readspeed(void)
{
    lfs_file_t file;
    volatile TIM_TypeDef *TIMx = TIM4;
    uint32_t start_us = TIMx->CNT; // read us hardware timer

    // Returns a negative error code on failure.
    int retval = lfs_file_open(&lfs, &file,
        argv[1], LFS_O_RDONLY);

    if(retval != LFS_ERR_OK) {
        printf("%s: Error opening file \"%s\"\n",__func__,argv[1]);
        return retval;
    }

    int bytes_read = 0;
    int total_bytes_read = 0;
    uint8_t buf[1024];
    // Loop, reading the file into a 1K buffer, until the entire file has been read
    do {
    	bytes_read = lfs_file_read(&lfs, &file, buf, sizeof(buf));
        if(bytes_read < LFS_ERR_OK) {
            printf("%s: Error reading file \"%s\"\n",__func__,argv[1]);
            break; // done reading
        }
        total_bytes_read += bytes_read;

    } while(bytes_read > 0); // keep looping as long as we keep getting data from file

    lfs_file_close(&lfs, &file);

    uint32_t stop_us = TIMx->CNT; // read us hardware timer
    if(stop_us < start_us) stop_us += 1<<16; // roll-over, add 16-bit roll-over offset

    printf("Read file: \"%s\", Time: %lu us\n",argv[1],stop_us-start_us);
    return retval;
} // cl_readspeed()



// Read line of text from file into buffer until new-line character (LF) is found, add null-termination to buffer and return character count.
// Returns non-negative value for number of characters read into buffer
// Returns EOF ( -1 ) for end of file
int lfs_gets(lfs_file_t * file, char * buffer, unsigned buffsize);
int lfs_gets(lfs_file_t * file, char * buffer, unsigned buffsize)
{
	int32_t count=0; // characters copied into buffer
	char c;
	// Begin with empty buffer
	memset(buffer,0,buffsize);
	while(count+1 < (int32_t)buffsize) {
		int lfs_ret = lfs_file_read(&lfs, file, &c, sizeof(c)); // non-efficient, but easy - read 1 character at a time
		if(lfs_ret == 0) {
			// EOF condition. If we have copied characters into buffer, return count value, else return EOF
			if(count) break;  // NULL terminate and return number of characters copied
			else return EOF;
		} else if(lfs_ret < LFS_ERR_OK) {
			printf("%s:Error reading file\n",__func__);
			return lfs_ret; // return negative error value
		}
		if(c == '\n')  {
			// Line Feed.  copy character, terminate line, return count
			buffer[count] = c;
			count++;
			return count;
		} else {
			// Normal character to be copied
			buffer[count] = c;
			count++;
		}
	} // while()
	// Terminate line and return count
	buffer[count] = 0;
	//printf("%s, \"%s\", %d\n",__func__,buffer,count);
	return count;
} //lfs_gets()



