// file: ymodem.h
//
// Y-Modem Protocol Implementation - Public Interface
// Author: Jim Merkle
// Date: November 21, 2025

#ifndef YMODEM_H
#define YMODEM_H

// ============================================================================
// RETURN CODE DEFINITIONS
// ============================================================================

/**
 * Y-Modem Operation Result Codes
 * 
 * These codes are returned by ymodem_receive(), ymodem_transmit(), and
 * cl_ymodem() to indicate the outcome of Y-Modem protocol operations.
 * 
 * SUCCESS AND ERROR CODES:
 * - YMODEM_SUCCESS: Operation completed successfully
 * - YMODEM_ERR_GENERAL: General error (file I/O, protocol violation, timeout)
 * - YMODEM_ERR_CANCELLED: Transfer cancelled by remote party (CAN received)
 * - YMODEM_ERR_FILE_EXISTS: Receive failed - destination file already exists
 * - YMODEM_ERR_TIMEOUT: Receive failed - no sender started before the 'C' requests ran out
 *
 * USAGE:
 * Check return value against these constants rather than using magic numbers.
 * This improves code readability and maintainability.
 */
typedef enum {
    YMODEM_SUCCESS = 0,           // Operation completed successfully
    YMODEM_ERR_GENERAL = -1,      // General error (I/O, protocol, timeout)
    YMODEM_ERR_CANCELLED = -2,    // Transfer cancelled by remote party
    YMODEM_ERR_FILE_EXISTS = -3,  // File already exists (receive mode)
    YMODEM_ERR_TIMEOUT = -4       // No sender responded, nothing received (receive mode)
} ymodem_result_t;

// ============================================================================
// PUBLIC API
// ============================================================================

/**
 * Receive one or more files via Y-Modem batch protocol
 * Returns: ymodem_result_t code
 */
int ymodem_receive(void);

/**
 * Transmit a single file via Y-Modem protocol
 * Parameters:
 *   path - LittleFS file path to transmit
 * Returns: ymodem_result_t code
 */
int ymodem_transmit(const char *path);

/**
 * Command-line interface for Y-Modem operations
 * - No arguments: Receive mode
 * - One or more arguments: Batch transmit mode
 * Returns: ymodem_result_t code
 */
int cl_ymodem(void);

// ============================================================================
// COMMAND TABLE RECORDS
// ============================================================================

// Set of commands for the command line table (see command_line.c cmd_table[])
// "ymodem" alone receives, "ymodem <file> ..." sends
#define YMODEM_COMMANDS \
{"ymodem",     "ymodem <file>... to send file(s), else receive file(s)",   cl_ymodem}

#endif // YMODEM_H

