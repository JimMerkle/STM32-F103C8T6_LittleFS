/*
 * version.h
 *
 *  Created on: Sep 29, 2022
 *      Author: Jim Merkle
 */

#ifndef SRC_VERSION_H_
#define SRC_VERSION_H_

#define VERSION_MAJOR	1
#define VERSION_MINOR   0
#define VERSION_BUILD	2

typedef struct {
	uint32_t major : 8;
	uint32_t minor : 8;
	uint32_t build: 16;
} VERSION_MAJOR_MINOR;


extern const VERSION_MAJOR_MINOR fw_version; // command_line.c


#endif /* SRC_VERSION_H_ */
