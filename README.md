# STM32-F103C8T6_LittleFS
STM32-F103 with command line, XModem, and LittleFS (using FLASH on the STM32-F103C8T6)

This code base was originally developed for the NUCLEO-F103RB board, where the STM32-F103RB has
  128KB of FLASH.
For unknown reasons, the LittleFS interface code that initializes FLASH at ADDR_FLASH_PAGE_96,
doesn't want to initialize FLASH at ADDR_FLASH_PAGE_44, when using the STM32-F103C8T6 part.
When building the project with "Release", the application is well under 44KBytes in size,
leaving 20KBytes for LittleFS storage.
