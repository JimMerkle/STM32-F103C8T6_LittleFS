# Changelog

Version is set in `Core/Src/version.h` and shown by the `version` command and the startup banner.

## 1.1.0 - 2026-10-29
- Console moved to USART1 (PA9/PA10), for the Raspberry Pi Pico debugprobe UART bridge
- Y-Modem replaces X-Modem (`Core/ymodem`); receive reports a timeout when no sender starts
- Command line: history (up/down) and line editing (left/right, Home/End, Delete)
- New commands: `cls`, `dump`, `filedump`, `makefile <name> [size]`
- Renamed: `dir` -> `ls`, `rename` -> `mv`, `copy` -> `cp`, `remove` -> `rm`; `type` removed (use `cat`)
- FLASH split 64K app / 64K LittleFS at 0x08010000 (was 32K at 0x08018000).
  **Existing files are lost** - the file system is reformatted on first boot.
- Fixes: `lfs_prog` could report a failed write as success, `filedump` looped on a read error,
  `makefile` wrote into an existing file

## 1.0.2 - 2023-02-11
- Works with Blue Pill (STM32-F103C8T6)
