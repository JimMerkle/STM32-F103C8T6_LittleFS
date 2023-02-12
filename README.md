# STM32-F103C8T6_LittleFS
**STM32-F103 with command line, XModem, and LittleFS (using FLASH on the STM32-F103C8T6)**

This code base was originally developed for the NUCLEO-F103RB board, where the STM32-F103RB has
  128KB of FLASH.
For unknown reasons, the LittleFS interface code that initializes FLASH at ADDR_FLASH_PAGE_96,
doesn't want to initialize FLASH at ADDR_FLASH_PAGE_44, when using the STM32-F103C8T6 part.
When building the project with "Release", the application is well under 44KBytes in size,
leaving 20KBytes for LittleFS storage.

**Wiring Diagram for Blue Pill (STM32-F103C8T6)**  (Wire colors refer to the included picture)
F103 Pin   Signal   Wire Color       ST-Link V2 (20-pin connector - CN3)   Comment
JTAG-1     3.3V      White     ---    19    3.3V (VDD)
JTAG-2     DIO       Gray      ---    7     TMS_SWDIO
JTAG-3     CLK       Violet    ---    9     TCK_SWCLK
JTAG-4     GND       Blue      ---    4     3.3V (VDD)

**Additional wire added NRST, making JTAG connections considerably faster:**
  7        NRST      Blue      ---    15    NRST   (STM32F103C8T6 pin 7)

**Serial Interface**
F103 Pin  Signal    Wire Color      FTDI Board    (FTDI board jumpered for 3.3V)
 PA2     USART2_TX    Gray     ---    FTDI_RX
 PA3     USART2_RX    White    ---    FTDI_TX
 GND     Ground       Black    ---    FTDI_GND
 
 
Reference Information:
https://www.st.com/resource/en/user_manual/um1075-stlinkv2-incircuit-debuggerprogrammer-for-stm8-and-stm32-stmicroelectronics.pdf
https://waterpigs.co.uk/articles/black-blue-pill-stm32-st-link-connection/
