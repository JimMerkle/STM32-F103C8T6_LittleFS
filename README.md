# STM32-F103C8T6_LittleFS</br>
**STM32-F103 with command line, XModem, and LittleFS (using FLASH on the STM32-F103C8T6)**
</br>
This code base was originally developed for the NUCLEO-F103RB board, where the STM32-F103RB has
  128KB of FLASH.</br>
For unknown reasons, the LittleFS interface code that initializes FLASH at ADDR_FLASH_PAGE_96,</br>
doesn't want to initialize FLASH at ADDR_FLASH_PAGE_44, when using the STM32-F103C8T6 part.</br>
When building the project with "Release", the application is well under 44KBytes in size,</br>
leaving 20KBytes for LittleFS storage.</br>
</br>
**Wiring Diagram for Blue Pill (STM32-F103C8T6)**  (Wire colors refer to the included picture)</br>
|F103 Pin|Signal|Wire Color|ST-Link V2 (20-pin connector - CN3)|
|---|---|---|---|---|
|JTAG-1|3.3V|White|19|3.3V (VDD)|
|JTAG-2|DIO|Gray|7|TMS_SWDIO|
|JTAG-3|CLK|Violet|9|TCK_SWCLK|
|JTAG-4|GND|Blue|4|3.3V (VDD)|
</br>
**Additional wire added NRST, making JTAG connections considerably faster:**</br>
  7        NRST      Blue      ---    15    NRST   (STM32F103C8T6 pin 7)</br>
</br>
**Serial Interface** (Different cable)</br>
F103 Pin  Signal    Wire Color      FTDI Board    (FTDI board jumpered for 3.3V)</br>
 PA2     USART2_TX    Gray     ---    FTDI_RX</br>
 PA3     USART2_RX    White    ---    FTDI_TX</br>
 GND     Ground       Black    ---    FTDI_GND</br>
 </br>
 </br>
Reference Information:</br>
https://www.st.com/resource/en/user_manual/um1075-stlinkv2-incircuit-debuggerprogrammer-for-stm8-and-stm32-stmicroelectronics.pdf</br>
https://waterpigs.co.uk/articles/black-blue-pill-stm32-st-link-connection/</br>
