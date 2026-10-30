// ymodem.c
// Y-Modem (CRC) transmit/receive for STM32F103 (Blue Pill)
// Ported from the SAME51J20A Curiosity Nano version - only the hardware abstraction layer changed
// Data: USART1 (shared with the command line console)
// USART1 configuration:
//   TX : blocking HAL_UART_Transmit()
//   RX : circular DMA buffer, 1030 bytes (holds a complete 1K STX packet), see main.c
// Filesystem: LittleFS
//
// ARCHITECTURE OVERVIEW:
// This module implements a complete Y-Modem protocol stack with the following layers:
// 1. Hardware Abstraction: USART interface functions for USART1
// 2. Protocol Layer: Packet construction, CRC validation, timeout handling
// 3. File System Layer: LittleFS integration for file operations
// 4. Application Layer: Batch-capable receive and multi-file transmit
//
// PROTOCOL FLOW:
// - Receive: Waits for 'C' requests, processes Block 0 metadata, receives data blocks
// - Transmit: Sends Block 0 with file info, transmits data blocks, handles EOT sequence
//
// ERROR HANDLING STRATEGY:
// - CRC validation on all packets
// - Timeout-based recovery with configurable retry limits
// - Graceful cancellation support via CAN characters
// - File system error propagation with cleanup
//
// This module implements batch-capable receive and one or more file transmit using multiple arguments.
// Empty block 0 ends a batch session.
// - Receive: ymodem_receive()
// - Transmit: ymodem_transmit(const char *path)
//
// Requirements (provided elsewhere):
//   - HAL_Delay(uint32_t ms), HAL_GetTick(void)
//   - dbg_msg(const char *fmt, ...)
//   - LittleFS globals and functions
//   - huart1, usart1_rx_count(), __io_getchar() (main.c)
//
// Author: Jim Merkle
// Date: 11/20/2025

#include <string.h>
#include <stdio.h>
#include <stdlib.h>   // strtoul()
#include <stdarg.h>
#include <stdint.h>
#include <stdbool.h>

// STM32 definitions
#include "main.h"     // HAL functions, huart1, usart1_rx_count()
#include "lfs.h"
#include "../Src/command_line.h"
#include "ymodem.h"

extern lfs_t lfs;     // littlefs_interface.c

// Debug output
// The SAME51 version sends debug output to a second UART (SERCOM2) when YMODEM_DEBUG is defined.
// The Blue Pill has only the console UART wired, and it carries the Y-Modem data,
// so debug messages are discarded.  Route them to USART2 (PA2/PA3) if needed.
#define dbg_msg(fmt, ...) ((void)0)


// ============================================================================
// PROTOCOL CONSTANTS AND CONFIGURATION
// ============================================================================

// Y-Modem Protocol Control Characters
// These characters control the flow and state of the Y-Modem protocol
#define SOH     0x01    // Start of Header - indicates 128-byte data block
#define STX     0x02    // Start of Text - indicates 1024-byte data block  
#define EOT     0x04    // End of Transmission - signals file completion
#define ACK     0x06    // Acknowledge - positive response to packet
#define NAK     0x15    // Negative Acknowledge - request retransmission
#define CAN     0x18    // Cancel - abort current transfer
#define CRC_REQ 'C'     // CRC Request - initiates CRC mode transfer

// Block Size Configuration
// Y-Modem supports two block sizes for optimal throughput vs. overhead
#define YM_BLOCK_SIZE_128  128     // Small blocks for short files or poor links
#define YM_BLOCK_SIZE_1K   1024    // Large blocks for better throughput

// Timeout and Retry Configuration
// These values balance reliability with transfer speed
#define YM_MAX_RETRY       10      // Maximum retransmission attempts
#define YM_IO_TIMEOUT_MS   3000    // Individual I/O operation timeout
#define YM_INITIAL_C_MS    3000    // Interval for sending initial 'C' requests
#define YM_INTERPACKET_MS  10      // Delay between packets for receiver processing

// ============================================================================
// HARDWARE ABSTRACTION LAYER - USART INTERFACE
// ============================================================================

// PURPOSE: Provide a consistent interface to USART1 hardware
// EXPECTATION: All USART operations go through these functions for portability
// ERROR HANDLING: Return 0 on success, -1 on failure for consistent error checking

#define YM_TX_TIMEOUT_MS   1000    // HAL_UART_Transmit() timeout (1K block takes ~90ms at 115200)

/**
 * Send a single character via USART1
 * Used for control characters (ACK, NAK, CRC_REQ, etc.)
 * Returns: 0 on success, -1 on failure
 */
static inline int ymodem_putc(uint8_t c) {
    return (HAL_UART_Transmit(&huart1, &c, 1, YM_TX_TIMEOUT_MS) == HAL_OK) ? 0 : -1;
}

/**
 * Send a buffer of data via USART1
 * Used for packet headers, data blocks, and CRC bytes
 * Returns: 0 on success, -1 on failure
 */
static inline int ymodem_write(const uint8_t *buf, size_t len) {
    return (HAL_UART_Transmit(&huart1, (uint8_t*)buf, (uint16_t)len, YM_TX_TIMEOUT_MS) == HAL_OK) ? 0 : -1;
}

/**
 * Read up to len bytes already waiting in the USART1 DMA RX buffer
 * Equivalent of SERCOM5_USART_Read() - never blocks
 * Returns: number of bytes copied
 */
static size_t usart1_read(uint8_t *buf, size_t len) {
    size_t n = 0;
    int c;
    while (n < len && (c = __io_getchar()) != EOF) {
        buf[n++] = (uint8_t)c;
    }
    return n;
}

/**
 * Read data with timeout support
 * PURPOSE: Non-blocking read with timeout for protocol timing requirements
 * EXPECTATION: May return fewer bytes than requested if timeout occurs
 * BEHAVIOR: Polls USART until data available or timeout expires
 * Returns: Number of bytes actually read (may be < len if timeout)
 */
static int ymodem_read(uint8_t *buf, size_t len, uint32_t timeout_ms) {
    uint32_t start = HAL_GetTick();
    size_t got = 0;
    while (got < len) {
        size_t avail = usart1_rx_count();
        if (avail > 0) {
            size_t n = usart1_read(buf + got, len - got);
            got += n;
            if (got == len) break;
        } else {
            if ((HAL_GetTick() - start) >= timeout_ms) break;
            HAL_Delay(1);
        }
    }
    return (int)got;
}

/**
 * Read a single character with timeout
 * Convenience wrapper for reading control characters and responses
 * Returns: 0 on success, -1 on timeout or error
 */
static inline int ymodem_getc(uint8_t *c, uint32_t timeout_ms) {
    return (ymodem_read(c, 1, timeout_ms) == 1) ? 0 : -1;
}

/**
 * Calculate CRC-16 checksum for Y-Modem protocol
 * PURPOSE: Implements the standard CRC-16-CCITT polynomial (0x1021)
 * EXPECTATION: Used for all packet validation in Y-Modem protocol
 * ALGORITHM: Bit-by-bit CRC calculation with polynomial 0x1021
 * Returns: 16-bit CRC value for the input data
 */
static uint16_t ymodem_crc16(const uint8_t *data, size_t len) {
    uint16_t crc = 0;  // Initialize CRC accumulator
    
    // Process each byte in the data buffer
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;  // XOR byte into high byte of CRC
        
        // Process each bit using CRC-16-CCITT polynomial (0x1021)
        for (int j = 0; j < 8; j++) {
            if (crc & 0x8000) {
                // High bit set: shift and XOR with polynomial
                crc = (uint16_t)((crc << 1) ^ 0x1021);
            } else {
                // High bit clear: just shift left
                crc <<= 1;
            }
        }
    }
    return crc;  // Return final 16-bit CRC value
}

// ============================================================================
// PROTOCOL STATE MANAGEMENT
// ============================================================================

/**
 * Y-Modem Transfer Context Structure
 * PURPOSE: Maintains state for current file transfer operation
 * LIFECYCLE: Created at start of transfer, destroyed at completion
 * THREAD SAFETY: Single-threaded use only, not reentrant
 */
typedef struct {
    uint8_t payload[YM_BLOCK_SIZE_1K];  // Buffer for largest possible block
    uint8_t blk_num;                     // Current block number (wraps at 256)
    uint32_t file_size;                  // Total file size from Block 0
    uint32_t bytes_done;                 // Bytes transferred so far
    char filename[128];                  // Filename from Block 0 metadata
    lfs_file_t file;                     // LittleFS file handle
} ymodem_ctx_t;

// ============================================================================
// PROTOCOL RESPONSE FUNCTIONS
// ============================================================================

// PURPOSE: Provide consistent protocol responses
// EXPECTATION: These functions handle the low-level protocol signaling
// ERROR HANDLING: ACK/NAK return status, CAN is fire-and-forget

/** Send ACK (acknowledge) - indicates successful packet reception */
static int ymodem_ack(void) { return ymodem_putc(ACK); }

/** Send NAK (negative acknowledge) - requests packet retransmission */
static int ymodem_nak(void) { return ymodem_putc(NAK); }

/** Send CRC request - initiates or continues CRC mode transfer */
static int ymodem_send_crc_req(void) { return ymodem_putc(CRC_REQ); }

/** Send double CAN - forcefully cancels transfer and flush receive buffer */
static void ymodem_can2(void) { 
    (void)ymodem_putc(CAN); 
    (void)ymodem_putc(CAN); 
    // Give sender time to stop transmitting, then flush receive buffer
    HAL_Delay(100);
    // Drain any incoming data from sender
    uint8_t dummy;
    while (usart1_rx_count() > 0) {
        usart1_read(&dummy, 1);
    }
}

/**
 * Construct and transmit a Y-Modem protocol packet
 * PURPOSE: Builds complete packet with header, data, and CRC for transmission
 * PACKET FORMAT: [SOH/STX][BLK][~BLK][DATA...][CRC_H][CRC_L]
 * 
 * PARAMETERS:
 * - type: SOH (128-byte) or STX (1024-byte) packet type
 * - blk: Block number (0-255, wraps around)
 * - data: Payload data (may be NULL for padding-only blocks)
 * - len: Actual data length (packet will be padded to full size)
 * 
 * BEHAVIOR:
 * - Pads short data with CP/M EOF character (0x1A)
 * - Calculates CRC over entire padded payload
 * - Sends complete packet atomically
 * 
 * Returns: 0 on success, negative on transmission error
 */
static int ymodem_send_packet(uint8_t type, uint8_t blk, const uint8_t *data, size_t len) {
    uint8_t hdr[3] = { type, blk, (uint8_t)(~blk) };
    if (ymodem_write(hdr, 3) != 0) return -1;

    size_t payload_len = (type == SOH) ? YM_BLOCK_SIZE_128 : YM_BLOCK_SIZE_1K;
    uint8_t tmp[YM_BLOCK_SIZE_1K];
    memset(tmp, 0x1A, payload_len); // CP/M EOF padding
    if (len > payload_len) len = payload_len;
    if (data && len) memcpy(tmp, data, len);

    if (ymodem_write(tmp, payload_len) != 0) return -2;

    uint16_t crc = ymodem_crc16(tmp, payload_len);
    uint8_t crc_bytes[2] = { (uint8_t)(crc >> 8), (uint8_t)(crc & 0xFF) };
    if (ymodem_write(crc_bytes, 2) != 0) return -3;

    return 0;
}

/**
 * Receive and validate a Y-Modem protocol packet
 * PURPOSE: Reads incoming packet, validates format and CRC
 * 
 * PACKET VALIDATION SEQUENCE:
 * 1. Read packet type (SOH/STX/EOT/CAN)
 * 2. Read block number and complement
 * 3. Read payload data
 * 4. Read and verify CRC
 * 
 * PARAMETERS:
 * - ctx: Transfer context for payload buffer
 * - ptype: Returns packet type (SOH/STX/EOT/CAN)
 * - blk: Returns block number
 * - plen: Returns payload length
 * 
 * SPECIAL CASES:
 * - EOT/CAN: Handled immediately, no further data read
 * - Invalid packet type: Returns error immediately
 * - CRC mismatch: Returns error after reading complete packet
 * 
 * Returns: 0 on success, negative error codes for various failures
 *   -1: Timeout reading packet type
 *   -2: Invalid packet type
 *   -3: Timeout reading header
 *   -4: Block number complement mismatch
 *   -5: Timeout reading payload
 *   -6: Timeout reading CRC
 *   -7: CRC validation failure
 */
static int ymodem_read_packet(ymodem_ctx_t *ctx, uint8_t *ptype, uint8_t *blk, size_t *plen) {
    uint8_t c;
    if (ymodem_getc(&c, YM_IO_TIMEOUT_MS) != 0) return -1;

    if (c == EOT || c == CAN) {
        *ptype = c;
        return 0;
    }

    if (c != SOH && c != STX) return -2;
    *ptype = c;

    size_t payload_len = (c == SOH) ? YM_BLOCK_SIZE_128 : YM_BLOCK_SIZE_1K;

    uint8_t hdr[2];
    if (ymodem_read(hdr, 2, YM_IO_TIMEOUT_MS) != 2) return -3;
    *blk = hdr[0];
    uint8_t blk_inv = hdr[1];
    if ((uint8_t)(~(*blk)) != blk_inv) return -4;

    if (ymodem_read(ctx->payload, payload_len, YM_IO_TIMEOUT_MS) != (int)payload_len) return -5;

    uint8_t crc_bytes[2];
    if (ymodem_read(crc_bytes, 2, YM_IO_TIMEOUT_MS) != 2) return -6;
    uint16_t recv_crc = ((uint16_t)crc_bytes[0] << 8) | crc_bytes[1];
    uint16_t calc_crc = ymodem_crc16(ctx->payload, payload_len);
    if (recv_crc != calc_crc) return -7;

    *plen = payload_len;
    return 0;
}

/**
 * Parse Y-Modem Block 0 metadata
 * PURPOSE: Extract filename and file size from initial metadata block
 * 
 * BLOCK 0 FORMAT: [filename]\0[size_decimal]\0[optional_fields]...
 * 
 * PARSING SEQUENCE:
 * 1. Check for empty block (indicates end of batch)
 * 2. Extract null-terminated filename
 * 3. Extract null-terminated size string
 * 4. Convert size string to integer
 * 
 * VALIDATION:
 * - Filename length within buffer limits
 * - Proper null termination
 * - Valid size field present
 * 
 * PARAMETERS:
 * - ctx: Transfer context to store filename and size
 * - len: Length of Block 0 payload data
 * 
 * Returns:
 *   0: Successfully parsed filename and size
 *   1: Empty block (end of batch transfer)
 *  -1: Invalid filename (too long or missing)
 *  -2: Missing size field
 *  -3: Invalid size field (empty)
 */
static int parse_block0(ymodem_ctx_t *ctx, size_t len) {
    const char *p = (const char *)ctx->payload;
    if (len == 0 || *p == '\0') return 1; // empty => end batch

    size_t fn_len = strnlen(p, len);
    if (fn_len == 0 || fn_len >= sizeof(ctx->filename)) return -1;

    memcpy(ctx->filename, p, fn_len);
    ctx->filename[fn_len] = '\0';

    // size field
    if (fn_len + 1 >= len) return -2;
    p += fn_len + 1;
    size_t sz_len = strnlen(p, len - (fn_len + 1));
    if (sz_len == 0) return -3;

    ctx->file_size = (uint32_t)strtoul(p, NULL, 10);
    return 0;
}

// ============================================================================
// PUBLIC API IMPLEMENTATION
// ============================================================================

/**
 * Y-Modem Batch File Reception
 * PURPOSE: Receive one or more files via Y-Modem protocol
 * 
 * PROTOCOL STATE MACHINE:
 * 1. INVITATION PHASE:
 *    - Send periodic 'C' characters to invite transmission
 *    - Wait for Block 0 (metadata) or timeout
 * 
 * 2. METADATA PHASE (Block 0):
 *    - Parse filename and file size
 *    - Create new LittleFS file (exclusive create)
 *    - Send ACK and 'C' to request first data block
 * 
 * 3. DATA TRANSFER PHASE:
 *    - Receive sequential data blocks (1, 2, 3, ...)
 *    - Validate block numbers and CRC
 *    - Write data to LittleFS file
 *    - Handle duplicate blocks (re-ACK)
 *    - Trim final block to actual file size
 * 
 * 4. FILE COMPLETION PHASE:
 *    - Receive EOT (End of Transmission)
 *    - Send ACK and close file
 *    - Send 'C' to invite next file
 * 
 * 5. BATCH COMPLETION:
 *    - Receive empty Block 0 (end of batch marker)
 *    - Send final ACK and terminate
 * 
 * ERROR HANDLING:
 * - File creation failures cause transfer cancellation
 * - Write errors cause transfer cancellation  
 * - Protocol errors (CRC, sequence) cause NAK and retry
 * - Sender cancellation (CAN) causes graceful termination
 * 
 * BATCH SUPPORT:
 * - Handles multiple files in single session
 * - Each file has independent Block 0 → Data → EOT sequence
 * - Empty Block 0 terminates entire batch
 * 
 * Returns:
 *   YMODEM_SUCCESS (0): All files received successfully
 *   YMODEM_ERR_GENERAL (-1): File system error or protocol failure
 *   YMODEM_ERR_CANCELLED (-2): Sender issued cancellation
 *   YMODEM_ERR_FILE_EXISTS (-3): File already exists
 *   YMODEM_ERR_TIMEOUT (-4): No sender responded, no files received
 */
int ymodem_receive(void) {
    static ymodem_ctx_t ctx;  // static: ~1.3K, keep it off the 20K part's stack
    memset(&ctx, 0, sizeof(ctx));

    dbg_msg("[YMRX] start\n");

    // INVITATION PHASE: Send periodic 'C' to invite transmission
    uint32_t last_c = 0;
    int c_retry_count = 0;
    const int MAX_C_RETRIES = 10;  // Maximum number of 'C' requests before timeout
    int files_received = 0;        // a timeout after at least one file is a normal end of batch

    while (1) {
        // Send 'C' periodically to invite sender or request next file
        uint32_t now = HAL_GetTick();
        if ((now - last_c) > YM_INITIAL_C_MS) {
            ymodem_send_crc_req();  // Invite transmission with CRC mode
            last_c = now;
            c_retry_count++;
            
            // If we've sent too many 'C' requests without response, assume batch is complete
            if (c_retry_count > MAX_C_RETRIES) {
                dbg_msg("[YMRX] timeout waiting for Block 0, sender not responding\n");
                return files_received ? YMODEM_SUCCESS : YMODEM_ERR_TIMEOUT;
            }
        }

        // Attempt to read incoming packet (Block 0 expected)
        uint8_t type = 0, blk = 0;
        size_t len = 0;
        int r = ymodem_read_packet(&ctx, &type, &blk, &len);
        if (r == 0 && (type == SOH || type == STX) && blk == 0) {
            // METADATA PROCESSING: Handle Block 0 with file information
            int pr = parse_block0(&ctx, len);
            if (pr == 1) {
                // Empty Block 0 signals end of batch transmission
                ymodem_ack();
                dbg_msg("[YMRX] end batch\n");
                return YMODEM_SUCCESS;  // Successful batch completion
            }
            if (pr < 0) {
                // Invalid Block 0 format - request retransmission
                dbg_msg("[YMRX] bad block0 (%d)\n", pr);
                ymodem_nak();
                continue;  // Stay in invitation loop
            }

            // FILE CREATION: Open new file for writing (must not exist)
            if (lfs_file_open(&lfs, &ctx.file, ctx.filename,
                              LFS_O_WRONLY | LFS_O_CREAT | LFS_O_EXCL) < 0) {
                dbg_msg("[YMRX] open fail: %s\n", ctx.filename);
                ymodem_can2();  // Cancel transfer - file already exists
                return YMODEM_ERR_FILE_EXISTS;  // File already exists error
            }

            ctx.bytes_done = 0;
            ctx.blk_num = 1;

            ymodem_ack();
            ymodem_send_crc_req(); // prompt first data block
            
            // Reset retry counter for data phase
            c_retry_count = 0;

            // DATA TRANSFER PHASE: Receive sequential data blocks
            while (1) {
                r = ymodem_read_packet(&ctx, &type, &blk, &len);
                if (r == 0 && (type == SOH || type == STX)) {
                    if (blk == ctx.blk_num) {
                        // EXPECTED BLOCK: Process new data block
                        
                        // Calculate bytes to write (respect file size limit)
                        size_t to_write = len;
                        if (ctx.file_size > 0) {
                            uint32_t remaining = ctx.file_size - ctx.bytes_done;
                            if (remaining < to_write) to_write = remaining;
                        }

                        // Write data to LittleFS file
                        if (to_write > 0) {
                            if (lfs_file_write(&lfs, &ctx.file, ctx.payload, to_write) < 0) {
                                dbg_msg("[YMRX] write error\n");
                                ymodem_can2();  // Cancel on file system error
                                lfs_file_close(&lfs, &ctx.file);
                                return YMODEM_ERR_GENERAL;
                            }
                        }
                        ctx.bytes_done += (uint32_t)to_write;

                        // Acknowledge successful block and advance sequence
                        ymodem_ack();
                        ctx.blk_num++;  // Expect next sequential block
                    } else if (blk == (uint8_t)(ctx.blk_num - 1)) {
                        // DUPLICATE BLOCK: Sender didn't receive our ACK
                        ymodem_ack();  // Re-acknowledge without processing
                    } else {
                        // SEQUENCE ERROR: Unexpected block number
                        dbg_msg("[YMRX] unexpected blk %u (expect %u)\n", blk, ctx.blk_num);
                        ymodem_nak();  // Request correct block
                    }
                } else if (r == 0 && type == EOT) {
                    // End-of-file
                    ymodem_ack();
                    lfs_file_close(&lfs, &ctx.file);
                    files_received++;
                    dbg_msg("[YMRX] file done: %s (%lu bytes)\n", ctx.filename, (unsigned long)ctx.bytes_done);

                    // SINGLE vs BATCH DETECTION:
                    // Wait briefly for sender to indicate next file or batch termination
                    // - Batch sender: sends next Block 0 immediately or empty Block 0 to terminate
                    // - Single file sender: goes silent after EOT/ACK handshake
                    // Strategy: Short timeout (500ms) to detect single file transfers quickly
                    
                    ymodem_send_crc_req();  // Invite next file or batch termination
                    uint32_t wait_start = HAL_GetTick();
                    const uint32_t SINGLE_FILE_TIMEOUT_MS = 500;  // Quick timeout for better UX
                    
                    // Brief wait for Block 0 response
                    while ((HAL_GetTick() - wait_start) < SINGLE_FILE_TIMEOUT_MS) {
                        // Check if Block 0 is incoming
                        if (usart1_rx_count() > 0) {
                            // Data available - likely Block 0 for batch continuation
                            last_c = HAL_GetTick();
                            c_retry_count = 0;
                            goto continue_batch;  // Jump to outer loop to process Block 0
                        }
                        
                        HAL_Delay(10);  // Small delay between checks
                    }
                    
                    // Timeout: No Block 0 received - assume single file transfer complete
                    dbg_msg("[YMRX] single file complete (no batch continuation)\n");
                    return YMODEM_SUCCESS;
                    
                    continue_batch:
                    break; // go back to outer loop for next block 0
                } else if (r == 0 && type == CAN) {
                    dbg_msg("[YMRX] cancelled by sender\n");
                    lfs_file_close(&lfs, &ctx.file);
                    return YMODEM_ERR_CANCELLED;
                } else {
                    dbg_msg("[YMRX] pkt read error (%d), NAK\n", r);
                    ymodem_nak();
                }
            }

            // continue for next file
            continue;
        } else if (r == 0 && type == CAN) {
            dbg_msg("[YMRX] cancelled\n");
            return YMODEM_ERR_CANCELLED;
        } else if (r < 0) {
            // Still waiting; keep sending 'C' periodically
            HAL_Delay(50);
        } else {
            // Non-metadata packet while idle ? ignore
            HAL_Delay(10);
        }
    }
}

/**
 * Y-Modem Single File Transmission
 * PURPOSE: Transmit a single file via Y-Modem protocol
 * 
 * TRANSMISSION STATE MACHINE:
 * 1. FILE PREPARATION PHASE:
 *    - Open file for reading
 *    - Determine file size by scanning entire file
 *    - Reset file pointer to beginning
 * 
 * 2. INVITATION WAITING PHASE:
 *    - Wait for receiver's 'C' request
 *    - Timeout after maximum retry attempts
 * 
 * 3. METADATA TRANSMISSION PHASE (Block 0):
 *    - Send Block 0 with filename and size
 *    - Wait for ACK from receiver
 *    - Wait for 'C' request for data blocks
 * 
 * 4. DATA TRANSMISSION PHASE:
 *    - Read file data in optimal block sizes
 *    - Send data blocks with sequential numbers
 *    - Handle receiver responses (ACK/NAK/CAN)
 *    - Retransmit on NAK with file pointer reset
 *    - Abort on CAN or excessive retries
 * 
 * 5. FILE COMPLETION PHASE:
 *    - Send EOT (End of Transmission)
 *    - Handle NAK after first EOT (send second EOT)
 *    - Wait for final ACK
 * 
 * BLOCK SIZE OPTIMIZATION:
 * - Uses 128-byte blocks for data ≤ 128 bytes
 * - Uses 1024-byte blocks for larger data
 * - Automatic selection for optimal throughput
 * 
 * ERROR RECOVERY:
 * - File I/O errors cause immediate termination
 * - Transmission errors trigger retransmission
 * - Receiver cancellation causes graceful termination
 * - Timeout protection prevents infinite waiting
 * 
 * PARAMETERS:
 * - path: LittleFS file path for transmission
 * 
 * Returns:
 *   0: Success - file transmitted completely
 *  -1: Error - file I/O error or protocol failure
 *  -2: Cancelled - receiver issued cancellation
 */
int ymodem_transmit(const char *path) {
    static ymodem_ctx_t ctx;  // static: ~1.3K, keep it off the 20K part's stack
    memset(&ctx, 0, sizeof(ctx));

    // FILE PREPARATION: Open file and determine size
    if (lfs_file_open(&lfs, &ctx.file, path, LFS_O_RDONLY) < 0) {
        dbg_msg("[YMTX] open fail: %s\n", path);
        return -1;  // File not found or access error
    }

    // SIZE DETERMINATION: Scan entire file to get accurate size
    // Note: LittleFS doesn't provide direct file size query
    ctx.file_size = 0;
    {
        uint8_t tmp[512];  // Temporary buffer for size calculation
        int n;
        while ((n = lfs_file_read(&lfs, &ctx.file, tmp, sizeof(tmp))) > 0) {
            ctx.file_size += (uint32_t)n;  // Accumulate total bytes
        }
        // Reset file pointer to beginning for actual transmission
        if (lfs_file_seek(&lfs, &ctx.file, 0, LFS_SEEK_SET) < 0) {
            dbg_msg("[YMTX] seek start error\n");
            lfs_file_close(&lfs, &ctx.file);
            return -1;
        }
    }

    strncpy(ctx.filename, path, sizeof(ctx.filename) - 1);
    ctx.filename[sizeof(ctx.filename) - 1] = '\0';

    dbg_msg("[YMTX] %s size=%lu\n", ctx.filename, (unsigned long)ctx.file_size);

    // Wait for receiver's 'C'
    int retry = 0;
    while (1) {
        uint8_t c;
        if (ymodem_getc(&c, YM_INITIAL_C_MS) == 0 && c == CRC_REQ) break;
        if (++retry > YM_MAX_RETRY) {
            dbg_msg("[YMTX] timeout waiting 'C'\n");
            lfs_file_close(&lfs, &ctx.file);
            return -1;
        }
        HAL_Delay(50);
    }

    // Send block 0 metadata
    {
        uint8_t meta[YM_BLOCK_SIZE_128] = {0};
        size_t p = 0;
        size_t fn = strnlen(ctx.filename, sizeof(ctx.filename));
        memcpy(&meta[p], ctx.filename, fn); p += fn; meta[p++] = '\0';

        char sz[16];
        snprintf(sz, sizeof(sz), "%lu", (unsigned long)ctx.file_size);
        size_t sl = strnlen(sz, sizeof(sz));
        memcpy(&meta[p], sz, sl); p += sl; meta[p++] = '\0';

        if (ymodem_send_packet(SOH, 0, meta, p) != 0) {
            dbg_msg("[YMTX] send block0 failed\n");
            lfs_file_close(&lfs, &ctx.file);
            return -1;
        }

        uint8_t rcv;
        if (ymodem_getc(&rcv, YM_IO_TIMEOUT_MS) != 0 || rcv != ACK) {
            dbg_msg("[YMTX] no ACK for block0\n");
            lfs_file_close(&lfs, &ctx.file);
            return -1;
        }
        if (ymodem_getc(&rcv, YM_IO_TIMEOUT_MS) != 0 || rcv != CRC_REQ) {
            dbg_msg("[YMTX] no 'C' after block0 ACK\n");
            lfs_file_close(&lfs, &ctx.file);
            return -1;
        }
    }

    // DATA TRANSMISSION PHASE: Send file contents as sequential blocks
    ctx.blk_num = 1;      // Start with block number 1 (Block 0 was metadata)
    ctx.bytes_done = 0;   // Track transmission progress
    retry = 0;            // Reset retry counter

    while (1) {
        // Read next chunk of file data (up to 1K)
        uint8_t buf[YM_BLOCK_SIZE_1K];
        int n = lfs_file_read(&lfs, &ctx.file, buf, sizeof(buf));
        if (n < 0) {
            dbg_msg("[YMTX] read error\n");
            lfs_file_close(&lfs, &ctx.file);
            return -1;  // File system error
        }
        if (n == 0) break;  // End of file reached

        // BLOCK SIZE OPTIMIZATION: Choose optimal packet type
        uint8_t type = (n <= YM_BLOCK_SIZE_128) ? SOH : STX;
        
        // Transmit data block with current block number
        if (ymodem_send_packet(type, ctx.blk_num, buf, (size_t)n) != 0) {
            dbg_msg("[YMTX] send data blk %u failed\n", ctx.blk_num);
            lfs_file_close(&lfs, &ctx.file);
            return -1;  // Transmission hardware error
        }

        uint8_t resp;
        if (ymodem_getc(&resp, YM_IO_TIMEOUT_MS) != 0) {
            if (++retry > YM_MAX_RETRY) {
                dbg_msg("[YMTX] timeout waiting ACK\n");
                lfs_file_close(&lfs, &ctx.file);
                return -1;
            }
            if (lfs_file_seek(&lfs, &ctx.file, (long)ctx.bytes_done, LFS_SEEK_SET) < 0) {
                dbg_msg("[YMTX] seek back error\n");
                lfs_file_close(&lfs, &ctx.file);
                return -1;
            }
            continue;
        }

        if (resp == ACK) {
            ctx.bytes_done += (uint32_t)n;
            ctx.blk_num++;
            retry = 0;
        } else if (resp == NAK) {
            if (lfs_file_seek(&lfs, &ctx.file, (long)ctx.bytes_done, LFS_SEEK_SET) < 0) {
                dbg_msg("[YMTX] seek back error\n");
                lfs_file_close(&lfs, &ctx.file);
                return -1;
            }
            if (++retry > YM_MAX_RETRY) {
                dbg_msg("[YMTX] too many NAKs\n");
                lfs_file_close(&lfs, &ctx.file);
                return -1;
            }
        } else if (resp == CAN) {
            dbg_msg("[YMTX] cancelled by receiver\n");
            lfs_file_close(&lfs, &ctx.file);
            return -2;
        }
    }

    // End-of-file: double-EOT handshake
    retry = 0;
    while (1) {
        if (ymodem_putc(EOT) != 0) {
            dbg_msg("[YMTX] EOT send fail\n");
            lfs_file_close(&lfs, &ctx.file);
            return -1;
        }
        uint8_t resp;
        if (ymodem_getc(&resp, YM_IO_TIMEOUT_MS) == 0) {
            if (resp == NAK) {
                dbg_msg("[YMTX] got NAK after EOT, sending EOT again\n");
                if (ymodem_putc(EOT) != 0) {
                    lfs_file_close(&lfs, &ctx.file);
                    return -1;
                }
                if (ymodem_getc(&resp, YM_IO_TIMEOUT_MS) == 0 && resp == ACK) {
                    break;
                }
            } else if (resp == ACK) {
                break;
            } else if (resp == CAN) {
                dbg_msg("[YMTX] cancelled at EOT\n");
                lfs_file_close(&lfs, &ctx.file);
                return -2;
            }
        }
        if (++retry > YM_MAX_RETRY) {
            dbg_msg("[YMTX] no ACK for EOT\n");
            lfs_file_close(&lfs, &ctx.file);
            return -1;
        }
    }
#if 0
    // Use this for single file transmit only..  It stops Y-Modem batch
    // After EOT/ACK, receiver sends 'C' to invite next file or end batch
    {
        uint8_t resp;
        if (ymodem_getc(&resp, YM_IO_TIMEOUT_MS) == 0 && resp == CRC_REQ) {
            // Send empty block0 to terminate session
            uint8_t empty[YM_BLOCK_SIZE_128] = {0};
            if (ymodem_send_packet(SOH, 0, empty, YM_BLOCK_SIZE_128) != 0) {
                dbg_msg("[YMTX] send empty block0 failed\n");
                lfs_file_close(&lfs, &ctx.file);
                return -1;
            }
            if (ymodem_getc(&resp, YM_IO_TIMEOUT_MS) != 0 || resp != ACK) {
                dbg_msg("[YMTX] no ACK for empty block0\n");
                lfs_file_close(&lfs, &ctx.file);
                return -1;
            }
        }
    }
#endif
    
    lfs_file_close(&lfs, &ctx.file);
    dbg_msg("[YMTX] done\n");
    return 0;
}

/**
 * Command Line Interface for Y-Modem Operations
 * PURPOSE: Provide CLI access to Y-Modem functionality with batch support
 * 
 * COMMAND MODES:
 * 
 * 1. RECEIVE MODE (no arguments):
 *    - Executes ymodem_receive()
 *    - Waits for sender to initiate transfer
 *    - Accepts multiple files in single session
 *    - Creates files in current LittleFS directory
 * 
 * 2. TRANSMIT MODE (one or more arguments):
 *    - Transmits each specified file sequentially
 *    - Implements Y-Modem batch protocol
 *    - Sends empty Block 0 to terminate batch
 *    - Requires receiver to be ready for batch reception
 * 
 * BATCH TRANSMISSION PROTOCOL:
 * - Each file sent via individual ymodem_transmit() call
 * - After last file, waits for receiver's 'C' request
 * - Sends empty Block 0 to signal end of batch
 * - Waits for final ACK from receiver
 * 
 * GLOBAL VARIABLE DEPENDENCIES:
 * - argc: Command line argument count
 * - argv: Command line argument vector
 * 
 * ERROR HANDLING:
 * - Individual file transmission errors abort entire batch
 * - Batch termination errors are reported but not fatal
 * - Protocol errors are propagated to caller
 * 
 * USAGE EXAMPLES:
 * - cl_ymodem()                       # Receive mode
 * - argv=["ymodem", "file1.txt"]      # Single file transmit
 * - argv=["ymodem", "f1", "f2", "f3"] # Multi-file batch transmit
 * 
 * Returns (ymodem_result_t):
 *   YMODEM_SUCCESS (0): Operation completed successfully
 *   YMODEM_ERR_GENERAL (-1): File system or protocol error
 *   YMODEM_ERR_CANCELLED (-2): Transfer cancelled by remote party
 *   YMODEM_ERR_FILE_EXISTS (-3): Receive mode only - file already exists
 */
int cl_ymodem(void) {
    dbg_msg("+%s\n", __func__);

    if (argc > 1) {
        // Multi-file transmit
        printf("Y-Modem Transmit - Begin Y-Modem Receive on the PC\n");
        for (int i = 1; i < argc; i++) {
            dbg_msg("Y-Modem Transmit, file: %s\n", argv[i]);
            int rc = ymodem_transmit(argv[i]);
            if (rc != 0) {
                dbg_msg("Transmit error on %s (rc=%d)\n", argv[i], rc);
                return rc;
            }
        }

        // BATCH TERMINATION: Send empty Block 0 to signal end of batch
        uint8_t resp;
        if (ymodem_getc(&resp, YM_IO_TIMEOUT_MS) == 0 && resp == CRC_REQ) {
            // Receiver is requesting next file - send empty Block 0 to terminate
            uint8_t empty[YM_BLOCK_SIZE_128] = {0};  // All zeros = empty filename
            if (ymodem_send_packet(SOH, 0, empty, YM_BLOCK_SIZE_128) != 0) {
                dbg_msg("[YMTX] send empty block0 failed\n");
                return -1;
            }
            // Wait for receiver's acknowledgment of batch termination
            if (ymodem_getc(&resp, YM_IO_TIMEOUT_MS) != 0 || resp != ACK) {
                dbg_msg("[YMTX] no ACK for empty block0\n");
                return -1;
            }
        }

        dbg_msg("[YMTX] batch transmit done\n");
        return 0;
    } else {
        // No args ? receive mode
        dbg_msg("Y-Modem Receive\n");
        printf("Y-Modem Receive - Begin Y-Modem Transmit on the PC\n");
        int result = ymodem_receive();
        if (result == YMODEM_ERR_FILE_EXISTS) {
            printf("Y-Modem receive failed: File already exists. Delete it first using 'rm' command.\n");
        } else if (result == YMODEM_ERR_TIMEOUT) {
            printf("\nY-Modem receive timed out: no sender started (start the PC transfer within ~30 seconds)\n");
        } else if (result < 0) {
            printf("Y-Modem receive failed with error code: %d\n", result);
        }
        return result;
    }
}

