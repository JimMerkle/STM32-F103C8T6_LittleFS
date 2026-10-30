# hexdump

Hexadecimal memory / register display for the command line.
Ported from the SAME51 Curiosity Nano project (`src/littlefs/hexdump.c`); only `log_msg()` changed to `printf()`,
plus an argument count check in `cl_dump()` for the 3-field command record.

## Command

```
dump <address> [count]
```

- `address` and `count` accept decimal or `0x` hex.
- `count` defaults to 256 bytes.

Output format (16 bytes per line, gap after 8, ASCII column):
```
00000000  02 03 1f 00 0d 00 00 00  31 32 33 00 00 00 00 00  |........123.....|
00000010  00 00 00 00 00 00 00                              |.......|
```
Try `dump 0x1FFFF7E0 0x20`: the first 16-bit word is the flash size in KB, and the 96-bit unique ID starts at 0x1FFFF7E8.

### Useful STM32F103 addresses

| Address | What |
|---|---|
| `0x08000000` | Flash (application) |
| `0x08010000` | LittleFS storage (64 x 1K pages) |
| `0x1FFFF000` | System memory (ST bootloader) |
| `0x1FFFF7E0` | Flash size (KB), unique ID at `0x1FFFF7E8` |
| `0x20000000` | SRAM (20K) |
| `0x40010800` | GPIOA registers (GPIOC = `0x40011000`) |
| `0x40021000` | RCC registers |
| `0xE000ED00` | Cortex-M3 SCB (CPUID at `0xE000ED00`) |

**Caution:** addresses with nothing behind them (gaps in the memory map, beyond the end of SRAM or flash)
cause a bus fault, and the board stops in `HardFault_Handler()`. Press RESET to recover.
Reading some peripheral registers has side effects (e.g. reading USART1 `DR` at `0x40013804`
consumes a received byte).

## API

```c
void hexdump(void * address, uint32_t count, uint32_t address_value); // address_value starts each line
void memory_dump(void * address, uint32_t count);                     // lines show memory addresses
void file_dump(void * address, uint32_t count);                       // lines start at offset 0
```

`littlefs_interface.c` uses `hexdump()` for the `filedump <file>` command (`cl_file_dump()`).

## Build

CubeIDE compiles everything under `Core/`, so this folder needs no project changes.
Includes are relative (`../hexdump/hexdump.h`, `../Src/command_line.h`).
