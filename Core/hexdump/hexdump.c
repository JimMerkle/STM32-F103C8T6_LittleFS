// Copyright Jim Merkle, 2/17/2020
// Module: hexdump.c
//
// Hexadecimal memory / register display routine
// Ported from the SAME51 Curiosity Nano project (src/littlefs/hexdump.c): log_msg() -> printf()

#include <stdio.h>  // printf()
#include <stdlib.h> // strtol()
#include <stdint.h>
#include "../Src/command_line.h"  // argc, argv
#include "hexdump.h"

// Here's the output I want from a hexdump(address, 23) routine:
//00000000  02 03 1f 00 0d 00 00 00  31 32 33 00 00 00 00 00  |........123.....|
//00000010  00 00 00 00 00 00 00                              |.......|
// "address_value" is the number starting each line of the dump.
// This may be actual memory address or file offset
void hexdump(void * address, uint32_t count, uint32_t address_value)
{
	uint8_t* data = (uint8_t*)address;
	int32_t remaining = count;
	uint8_t i;
	while(remaining > 0) {
		uint8_t bytes_this_line = remaining < 16? remaining:16; // use 16 or smaller value
		// Start by displaying the address for this line
		printf("%08lX  ",(uint32_t)address_value);
        // Display a row of data
        for(i=0;i<bytes_this_line;i++)
            printf("%02X %s",data[i],i==7?(char*)" ":(char*)""); // print hex byte, and if needed, the gap between two groups of 8 bytes
        // If we didn't display a complete line, write spaces
        if(bytes_this_line < 16) {
            for(i=bytes_this_line;i<16;i++)
                printf("   ");
            if(bytes_this_line < 8)
                printf(" "); // add another space for that little gap
        }

        // Add some space before ASCII data display, and the starting '|'
        printf("  |");
        // Display ASCII
        for(i=0;i<bytes_this_line;i++) {
            if(data[i] >= ' ' && data[i] <= '~')
                printf("%c",(char)data[i]); // printable ascii
            else
                printf(".");  // non-printable
        } // for-loop

        // Print ending '|' and new line
        printf("|\n");

        // Line by line updates
        data += bytes_this_line;
        address_value += bytes_this_line;
        remaining -= bytes_this_line;
    }
    // Add an additional line feed before returning
    //printf("\n");
} // hexdump()

void memory_dump(void * address, uint32_t count)
{
	return hexdump(address, count, (uint32_t) address);
}

void file_dump(void * address, uint32_t count)
{
	return hexdump(address, count, 0);
}



// Command Line interface to hexdump() routine
// Table record: HEXDUMP_COMMANDS in hexdump.h
int cl_dump(void)
{
	// verify argument count
	if(argc < 2) {
		printf("\nExpect <address> [count]\n");
		return 0;
	}
	uint32_t address = strtoul(argv[1],NULL,0); // allow user to use decimal or hex
	uint32_t count = 0x100;
	// Set default count to 256 bytes, else allow client to over-ride
	if(argc > 2)
		count = strtoul(argv[2],NULL,0); // allow user to use decimal or hex

	memory_dump((void *) address,count);
	return 0;
}
