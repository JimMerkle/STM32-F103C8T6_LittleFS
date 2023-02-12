// Initial code from: https://github.com/kelvinlawson/xmodem-1k/blob/master/xmodem.c
/*
 * Copyright 2001-2010 Georges Menie (www.menie.org)
 * All rights reserved.
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the University of California, Berkeley nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE REGENTS AND CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* this code needs standard functions memcpy() and memset()
   and input/output functions _inbyte() and _outbyte().

   the prototypes of the input/output functions are:
     int _inbyte(unsigned short timeout); // msec timeout
     void _outbyte(int c);

 */
#include <stdint.h>
#include <stdio.h>
#include <string.h> // memcpy(), memset()
#include "crc16.h"
#include "main.h"   // STM32 HAL APIs
#include "command_line.h" // argc, argv
#include "lfs.h"	// LittleFS APIs

#define SOH  0x01
#define STX  0x02
#define EOT  0x04
#define ACK  0x06
#define NAK  0x15
#define CAN  0x18
#define CTRLZ 0x1A

#define DLY_1S 1000
#define MAXRETRANS 25

// Disable xmodem-1k   Use 128 byte blocks instead (Expect to make this software selectable)
//#define TRANSMIT_XMODEM_1K
static lfs_file_t * file; // our lfs file structure


//=================================================================================================
// Interface functions to merge this X-Modem code with the current STM32 platform code available
//=================================================================================================
extern int __io_getchar(void); // main.c
extern int __io_putchar(int ch); // main.c
extern lfs_t lfs; // littlefs_interface.c
#define _outbyte(c) __io_putchar(c)

// Blocking character input function with timeout:
int _inbyte(uint32_t timeout_ms)
{
	// Get millisecond tick count when entering this function
    uint32_t entry_ticks = HAL_GetTick();
    int ser_data;
    // Loop until we receive a character or timeout occures
    do {
        ser_data =  __io_getchar();
        if(ser_data >=0) return ser_data; // character received (not EOF)
	} while ((HAL_GetTick() - entry_ticks) < timeout_ms);
    return ser_data; // return EOF received from previous __io_getchar() call
}

//=================================================================================================
// Interface functions end
//=================================================================================================

static int check(int crc, const unsigned char *buf, int sz)
{
	if (crc) {
		unsigned short crc = crc16_ccitt(buf, sz);
		unsigned short tcrc = (buf[sz]<<8)+buf[sz+1];
		if (crc == tcrc)
			return 1;
	}
	else {
		int i;
		unsigned char cks = 0;
		for (i = 0; i < sz; ++i) {
			cks += buf[i];
		}
		if (cks == buf[sz])
		return 1;
	}

	return 0;
}

static void flushinput(void)
{
	while (_inbyte(((DLY_1S)*3)>>1) >= 0)
		;
}

// Use File I/O vs using buffers for testing
#define USE_FILE_IO 1

// Return number of bytes received, or negative value for error
int xmodemReceive(void)
{
	unsigned char xbuff[1030]; /* 1024 for XModem 1k + 3 head chars + 2 crc + nul */
	unsigned char *p;
	int bufsz, crc = 0;
	unsigned char trychar = 'C';
	unsigned char packetno = 1;
	int i, c, len = 0; // 'len' is total bytes receives as well as index within the destination buffer for each pass
	int retry, retrans = MAXRETRANS;

	for(;;) {
		for( retry = 0; retry < 16; ++retry) {
			if (trychar) _outbyte(trychar);
			if ((c = _inbyte((DLY_1S)<<1)) >= 0) {
				switch (c) {
				case SOH:
					// Original X-Modem
					bufsz = 128;
					goto start_recv;
				case STX:
					// X-Modem 1K
					bufsz = 1024;
					goto start_recv;
				case EOT:
					flushinput();
					_outbyte(ACK);
					return len; /* normal end */
				case CAN:
					if ((c = _inbyte(DLY_1S)) == CAN) {
						flushinput();
						_outbyte(ACK);
						return -1; /* canceled by remote */
					}
					break;
				default:
					break;
				}
			}
		}
		if (trychar == 'C') { trychar = NAK; continue; }
		flushinput();
		_outbyte(CAN);
		_outbyte(CAN);
		_outbyte(CAN);
		return -2; /* sync error */

	start_recv:
		if (trychar == 'C') crc = 1;
		trychar = 0;
		p = xbuff;
		*p++ = c;
		// Receive a buffer of data (128 or 1024 bytes)
		for (i = 0;  i < (bufsz+(crc?1:0)+3); ++i) {
			if ((c = _inbyte(DLY_1S)) < 0) goto reject;
			*p++ = c; // store character in buffer then increment pointer
		}
		// Validate the buffer of data
		if (xbuff[1] == (unsigned char)(~xbuff[2]) &&
			(xbuff[1] == packetno || xbuff[1] == (unsigned char)packetno-1) &&
			check(crc, &xbuff[3], bufsz)) {
			if (xbuff[1] == packetno)	{
#ifdef USE_FILE_IO
				lfs_file_write(&lfs, file, &xbuff[3], bufsz);
				len += bufsz; // Update total number of bytes received
#else
				// Transfer bytes from xbuff to client's buffer passed in

				register int count = destsz - len;
				// Determine number of bytes to copy to client's buffer
				if (count > bufsz) count = bufsz;
				if (count > 0) {
					memcpy (&dest[len], &xbuff[3], count);
					len += count; // Update total number of bytes received
				}
#endif
				++packetno;
				retrans = MAXRETRANS+1;
			}
			if (--retrans <= 0) {
				flushinput();
				_outbyte(CAN);
				_outbyte(CAN);
				_outbyte(CAN);
				return -3; /* too many retry error */
			}
			_outbyte(ACK);
			continue;
		}
	reject:
		flushinput();
		_outbyte(NAK);
	}
	return -1;
}

int xmodemTransmit(void)
{
	unsigned char xbuff[1030]; /* 1024 for XModem 1k + 3 head chars + 2 crc + nul */
	int bufsz, crc = -1;
	unsigned char packetno = 1;
	int i, c, len = 0;
	int retry;

	for(;;) {
		for( retry = 0; retry < 16; ++retry) {
			if ((c = _inbyte((DLY_1S)<<1)) >= 0) {
				switch (c) {
				case 'C':
					crc = 1;
					goto start_trans;
				case NAK:
					crc = 0;
					goto start_trans;
				case CAN:
					if ((c = _inbyte(DLY_1S)) == CAN) {
						_outbyte(ACK);
						flushinput();
						return -1; /* canceled by remote */
					}
					break;
				default:
					break;
				}
			}
		}
		_outbyte(CAN);
		_outbyte(CAN);
		_outbyte(CAN);
		flushinput();
		return -2; /* no sync */

		for(;;) {
		start_trans:
#ifdef TRANSMIT_XMODEM_1K
			xbuff[0] = STX; bufsz = 1024;
#else
			xbuff[0] = SOH; bufsz = 128;
#endif
			xbuff[1] = packetno;
			xbuff[2] = ~packetno;
#ifdef USE_FILE_IO
			// Determine number of bytes to send this pass
			memset (&xbuff[3], 0, bufsz); // clear the buffer
	    	// Read data from source file
	        c = lfs_file_read(&lfs, file, &xbuff[3], bufsz); // data copied directly into transmit buffer
			if (c > 0) {
#else
			c = srcsz - len; // determine number of bytes to send this pass
			if (c > bufsz) c = bufsz;
			if (c > 0) {
				memset (&xbuff[3], 0, bufsz); // clear the buffer
				memcpy (&xbuff[3], &src[len], c); // copy this pass's data into transmit buffer
#endif
				if (c < bufsz) xbuff[3+c] = CTRLZ;
				if (crc) {
					unsigned short ccrc = crc16_ccitt(&xbuff[3], bufsz);
					xbuff[bufsz+3] = (ccrc>>8) & 0xFF;
					xbuff[bufsz+4] = ccrc & 0xFF;
				}
				else {
					unsigned char ccks = 0;
					for (i = 3; i < bufsz+3; ++i) {
						ccks += xbuff[i];
					}
					xbuff[bufsz+3] = ccks;
				}
				for (retry = 0; retry < MAXRETRANS; ++retry) {
					for (i = 0; i < bufsz+4+(crc?1:0); ++i) {
						_outbyte(xbuff[i]);
					}
					if ((c = _inbyte(DLY_1S)) >= 0 ) {
						switch (c) {
						case ACK:
							++packetno;
							len += bufsz;
							goto start_trans;
						case CAN:
							if ((c = _inbyte(DLY_1S)) == CAN) {
								_outbyte(ACK);
								flushinput();
								return -1; /* canceled by remote */
							}
							break;
						case NAK:
						default:
							break;
						}
					}
				}
				_outbyte(CAN);
				_outbyte(CAN);
				_outbyte(CAN);
				flushinput();
				return -4; /* xmit error */
			}
			else {
				for (retry = 0; retry < 10; ++retry) {
					_outbyte(EOT);
					if ((c = _inbyte((DLY_1S)<<1)) == ACK) break;
				}
				flushinput();
				return (c == ACK)?len:-5;
			}
		}
	}
}


// This command line function manages the file open, file close, and file write
int cl_xmodem_receive(void)
{
	int status;
	int lfs_status;
	char * filename=NULL;

	lfs_file_t file_rx; // use temporary stack space
	file=&file_rx; // assign file pointer to our file_rx structure

	// Check number of command line arguments.  Should have at least two: command, <filename>
	if(argc <2 ) {
		printf("Not enough arguments.  Need <filename>\n");
		return -1;
	}
	filename = argv[1]; // use a name instead of some indexed string

	// Create file for writing (file must not already exist) - return negative error code on failure.
	lfs_status = lfs_file_open(&lfs, file, filename, LFS_O_WRONLY | LFS_O_CREAT | LFS_O_EXCL);
    if(lfs_status != LFS_ERR_OK) {
        printf("%s: Error creating XModem receive file \"%s\"\n",__func__,filename);
        return lfs_status;
    }

    //printf("%s: File \"%s\" open for data\n",__func__,filename);
	status = xmodemReceive(); // use static file structure, 'file'
	lfs_file_close(&lfs, file); // close open file "handle", performing final flush to FLASH

	if (status < 0) {
		printf ("Xmodem receive error: status: %d\n", status);
	}
	else  {
		printf ("Xmodem successfully received %d bytes\n", status);
	}

	return 0;
}

// This command line function manages the file open, file close, and file read
int cl_xmodem_send(void)
{
	int status;
	int lfs_status;
	char * filename=NULL;

	lfs_file_t file_tx; // use temporary stack space
	file=&file_tx; // assign file pointer to our file_tx structure

	// Check number of command line arguments.  Should have at least two: command, <filename>
	if(argc <2 ) {
		printf("Not enough arguments.  Need <filename>\n");
		return -1;
	}
	filename = argv[1]; // use a name instead of some indexed string

	// Create file for reading (file must already exist) - return negative error code on failure.
	lfs_status = lfs_file_open(&lfs, file, filename, LFS_O_RDONLY);
    if(lfs_status != LFS_ERR_OK) {
        printf("%s: Error creating XModem transmit file \"%s\"\n",__func__,filename);
        return lfs_status;
    }
	status = xmodemTransmit(); // Send the file, returning number of bytes sent
	lfs_file_close(&lfs, file); // close open file "handle"

	if (status < 0) {
		printf ("Xmodem transmit error: status: %d\n", status);
	}
	else  {
		printf ("Xmodem successfully transmitted %d bytes\n", status);
	}
	return status;
}




