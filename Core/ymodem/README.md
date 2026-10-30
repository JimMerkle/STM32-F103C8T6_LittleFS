# Y-Modem Protocol Implementation

## Overview

This module provides a complete Y-Modem (CRC) implementation for embedded systems, running on the STM32F103 (Blue Pill) with LittleFS filesystem support. It was ported from the SAME51 Curiosity Nano project; only the hardware abstraction layer changed. The implementation supports both batch-capable file reception and single/multi-file transmission.

## Features

- **Batch Reception**: Receive multiple files in a single session
- **Batch Transmission**: Send multiple files sequentially  
- **CRC Error Detection**: Uses 16-bit CRC for reliable data transfer
- **Variable Block Sizes**: Supports both 128-byte (SOH) and 1024-byte (STX) packets
- **Timeout Handling**: Configurable timeouts for robust communication
- **LittleFS Integration**: Direct file system operations
- **Debug Support**: `dbg_msg()` hooks are in place, compiled out (no second UART wired)

## Hardware Requirements

- **STM32F103C8T6 / CBT6** (Blue Pill), STM32 HAL
- **USART1** (PA9 TX / PA10 RX, 115200 8N1): shared with the command line console
  - TX: blocking `HAL_UART_Transmit()`
  - RX: circular DMA buffer, 1030 bytes (DMA1 Channel 5) - large enough for a complete 1K (STX) packet
- **SysTick**: `HAL_GetTick()` / `HAL_Delay()` for timing and delays

## Protocol Specifications

### Y-Modem Protocol Details
- **Block 0**: Metadata block containing filename and file size
- **Data Blocks**: Sequential numbered blocks (1, 2, 3, ...)
- **End of Transmission**: EOT character followed by ACK handshake
- **Batch Termination**: Empty Block 0 to end session

### Packet Structure
```
SOH/STX | Block# | ~Block# | Data[128/1024] | CRC16_H | CRC16_L
   1       1        1         128/1024         1         1
```

### Control Characters
- `SOH (0x01)`: Start of 128-byte block
- `STX (0x02)`: Start of 1024-byte block  
- `EOT (0x04)`: End of transmission
- `ACK (0x06)`: Acknowledge
- `NAK (0x15)`: Negative acknowledge
- `CAN (0x18)`: Cancel transmission
- `'C' (0x43)`: CRC request

## API Reference

### Core Functions

#### `int ymodem_receive(void)`
Initiates batch-capable Y-Modem reception.

**Returns (ymodem_result_t):**
- `YMODEM_SUCCESS (0)`: All files received successfully
- `YMODEM_ERR_GENERAL (-1)`: File system or protocol error
- `YMODEM_ERR_CANCELLED (-2)`: Transfer cancelled by sender
- `YMODEM_ERR_FILE_EXISTS (-3)`: File already exists (use 'rm' command first)
- `YMODEM_ERR_TIMEOUT (-4)`: No sender started within ~30 seconds, nothing received

**Behavior:**
- Sends periodic 'C' characters to invite transmission
- Creates new LittleFS files from Block 0 metadata
- Handles multiple files in sequence
- Terminates on empty Block 0

#### `int ymodem_transmit(const char *path)`
Transmits a single file via Y-Modem protocol.

**Parameters:**
- `path`: LittleFS path to file for transmission

**Returns (ymodem_result_t):**
- `YMODEM_SUCCESS (0)`: File transmitted successfully
- `YMODEM_ERR_GENERAL (-1)`: File system or protocol error
- `YMODEM_ERR_CANCELLED (-2)`: Transfer cancelled by receiver

**Behavior:**
- Waits for receiver's 'C' request
- Sends Block 0 with filename and size
- Transmits file data in optimal block sizes
- Handles retransmission on NAK

#### `int cl_ymodem(void)`
Command-line interface for Y-Modem operations.

**Usage:**
- No arguments: Receive mode
- One or more arguments: Transmit mode (batch)

**Returns (ymodem_result_t):**
- Same codes as `ymodem_receive()` and `ymodem_transmit()`
- Displays user-friendly error messages for file exists condition

## Configuration Constants

```c
#define YM_BLOCK_SIZE_128  128     // Small block size
#define YM_BLOCK_SIZE_1K   1024    // Large block size  
#define YM_MAX_RETRY       10      // Maximum retry attempts
#define YM_IO_TIMEOUT_MS   3000    // I/O operation timeout
#define YM_INITIAL_C_MS    3000    // Initial 'C' request interval
#define YM_INTERPACKET_MS  10      // Inter-packet delay
```

## Usage Examples

### Receiving Files
```c
// Start Y-Modem reception (waits for sender)
int result = ymodem_receive();
if (result == YMODEM_SUCCESS) {
    printf("All files received successfully\n");
} else if (result == YMODEM_ERR_FILE_EXISTS) {
    printf("File already exists - delete it first\n");
} else if (result == YMODEM_ERR_CANCELLED) {
    printf("Transfer cancelled\n");
} else {
    printf("Reception error: %d\n", result);
}
```

### Transmitting a Single File  
```c
// Send a single file
int result = ymodem_transmit("/config/settings.ini");
if (result == YMODEM_SUCCESS) {
    printf("File sent successfully\n");
}
```

### Command Line Usage
```c
// Setup command line arguments
argc = 2;
argv[0] = "ymodem";
argv[1] = "/data/logfile.txt";

// Execute Y-Modem command
int result = cl_ymodem();
```

## Error Handling

### Return Codes (ymodem_result_t enum)
All functions return standardized codes defined in `ymodem.h`:
- `YMODEM_SUCCESS (0)`: Operation completed successfully
- `YMODEM_ERR_GENERAL (-1)`: File system, timeout, CRC, or protocol error
- `YMODEM_ERR_CANCELLED (-2)`: Transfer cancelled via CAN characters
- `YMODEM_ERR_FILE_EXISTS (-3)`: Receive failed - file already exists
- `YMODEM_ERR_TIMEOUT (-4)`: Receive failed - no sender responded to the 'C' requests

### Common Error Scenarios
- **Timeout Errors**: No response within configured timeouts
- **CRC Errors**: Data corruption detected during transmission  
- **File Already Exists**: User must delete existing file first using 'rm' command
- **Protocol Errors**: Invalid block numbers or packet format
- **Cancellation**: Either party can cancel with CAN characters

### Retry Mechanism
- Automatic retransmission on NAK or timeout
- Maximum retry limit prevents infinite loops
- Exponential backoff for robust recovery

## Integration Requirements

### Dependencies
The module requires these external components:

```c
// System timing (STM32 HAL)
uint32_t HAL_GetTick(void);
void HAL_Delay(uint32_t ms);

// UART communication (USART1, main.c / main.h)
extern UART_HandleTypeDef huart1;  // TX via HAL_UART_Transmit()
uint16_t usart1_rx_count(void);    // bytes waiting in the DMA RX buffer
int __io_getchar(void);            // non-blocking read, EOF when empty

// LittleFS file system
extern lfs_t lfs;
int lfs_file_open(lfs_t *lfs, lfs_file_t *file, const char *path, int flags);
int lfs_file_close(lfs_t *lfs, lfs_file_t *file);
lfs_ssize_t lfs_file_read(lfs_t *lfs, lfs_file_t *file, void *buffer, lfs_size_t size);
lfs_ssize_t lfs_file_write(lfs_t *lfs, lfs_file_t *file, const void *buffer, lfs_size_t size);
lfs_soff_t lfs_file_seek(lfs_t *lfs, lfs_file_t *file, lfs_soff_t off, int whence);
```

### Command line
`ymodem.h` provides the table record, added to `cmd_table[]` in `command_line.c`:
```c
YMODEM_COMMANDS,     /* Y-Modem send/receive, from ymodem/ymodem.h */
```
- `ymodem` - receive one or more files (start a Y-Modem send on the PC)
- `ymodem <file> [file...]` - send file(s) (start a Y-Modem receive on the PC)

In Tera Term: File > Transfer > YMODEM > Send / Receive.

### Build Configuration
CubeIDE compiles everything under `Core/`, so this folder needs no project changes.
Includes are relative (`../ymodem/ymodem.h`, `../Src/command_line.h`), so no extra include path is needed.

Debug output: the SAME51 version printed protocol traces to a second UART (SERCOM2) with `YMODEM_DEBUG`.
Here `dbg_msg()` is compiled out, because USART1 carries the Y-Modem data. USART2 (PA2/PA3) is free if traces are needed.

## Performance Characteristics

### Transfer Rates
- **128-byte blocks**: ~9.6 KB/s at 115200 baud
- **1024-byte blocks**: ~10.4 KB/s at 115200 baud (optimal)
- **Efficiency**: ~90% protocol efficiency with 1K blocks

### Memory Usage
- **Static RAM**: ~2.5KB - the ~1.3KB transfer context is `static` in both `ymodem_receive()` and `ymodem_transmit()` (the F103 has 20KB RAM)
- **Stack**: ~2.2KB worst case while transmitting (`ymodem_transmit()` 1088 + `ymodem_send_packet()` 1072, from `-fstack-usage`)
- **Flash**: ~6KB code size
- **RAM Buffers**: Uses the existing 1030-byte USART1 DMA RX buffer

### Timing Requirements
- **Baud Rate**: Tested at 115200 and 230400 bps
- **Flow Control**: none
- **Latency**: <10ms typical response time

## Troubleshooting

### Common Issues

1. **Transfer Hangs**
   - Check UART configuration and baud rate
   - Verify cable connections
   - Enable flow control if needed

2. **CRC Errors**
   - Check for electrical noise
   - Verify ground connections
   - Reduce baud rate if persistent

3. **File Creation Fails**
   - Ensure LittleFS is properly mounted
   - Check available flash space
   - Verify file path format

4. **Timeout Issues**
   - Increase timeout values for slow systems
   - Check SysTick timer configuration
   - Verify interrupt priorities

### Debug Output
On the SAME51, with `YMODEM_DEBUG` enabled, protocol traces looked like this (compiled out on the Blue Pill, see Build Configuration):

```
[YMRX] start
[YMRX] file done: config.ini (1024 bytes)
[YMRX] end batch
[YMTX] settings.dat size=2048
[YMTX] done
```

## Version History

- **v1.2** (2026-10-29): Ported to STM32F103 Blue Pill (STM32-F103C8T6_LittleFS), replacing X-Modem
  - Hardware layer: SERCOM5 -> USART1 (HAL TX, DMA ring RX), SYSTICK -> HAL_GetTick()/HAL_Delay()
  - `log_msg()` -> `printf()`; SERCOM2 debug output compiled out
  - Transfer context made `static` to keep it off the stack
  - Added `YMODEM_COMMANDS` table record to ymodem.h
  - Receive returns `YMODEM_ERR_TIMEOUT` (with a message) when no sender starts, instead of silent success

- **v1.1** (2026-02-05): Enhanced error handling and API improvements
  - Added `ymodem_result_t` enum with named error codes
  - Added `YMODEM_ERR_FILE_EXISTS` for explicit file conflict detection
  - Improved user feedback in `cl_ymodem()` for common errors
  - Updated public API declarations in ymodem.h
  - Fixed single file receive timeout - now returns to CLI in 500ms instead of 30+ seconds
  - Smart batch detection: distinguishes single file from batch transfers automatically
  
- **v1.0** (2025-11-21): Initial implementation
  - Batch reception support
  - Single file transmission
  - CRC error detection
  - LittleFS integration
  - Command line interface

## License

## Author

**Jim Merkle**  
Date: November 21, 2025