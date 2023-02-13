# STM32-F103C8T6_LittleFS<br>
**STM32-F103 with command line, XModem, and LittleFS (using FLASH on the STM32-F103C8T6)**
<br>
This code base was originally developed for the NUCLEO-F103RB board (STM32-F103RB has 128KB of FLASH).<br>
When building this project with "Release", the application is well under 44KBytes in size,<br>
leaving 20KBytes for LittleFS storage.<br>
<br>
The linker / locator tools have made it difficult to place a LittleFS image inside the device's 64K of FLASH.<br>
Although the firmware easily fits into 44K of FLASH space, changing the size of the FLASH in the .ld file<br>
from 64K to 44K, produces an errror: "arm-none-eabi\bin\ld.exe: region `FLASH' overflowed by 14832 bytes".<br>
Placing the LittleFS storage in the top end of 128K area appears to work, just like the STM32-F103RB.<br>
<br>
**Wiring Diagram for Blue Pill (STM32-F103C8T6)**  (Wire colors refer to the included picture)<br>
Using the ST-Link V2 - 20 pin JTAG connector <br>
|F103 Pin|Signal|Wire Color|ST-Link Pin|ST-Link Signal|
|---|---|---|---|---|
|JTAG-1|3.3V|White|19|3.3V (VDD)|
|JTAG-2|DIO|Gray|7|TMS_SWDIO|
|JTAG-3|CLK|Violet|9|TCK_SWCLK|
|JTAG-4|GND|Blue|4|3.3V (VDD)|

**Additional wire adding NRST, making JTAG connections considerably faster** <br>
Connecting to STM32F103C8T6 pin 7 - NRST <br>
|F103 Pin|Signal|Wire Color|ST-Link Pin|ST-Link Signal|
|---|---|---|---|---|
|7|NRST|Blue|15|NRST|

**Serial Interface** (Different cable)<br>
FTDI board jumpered for 3.3V <br>
|F103 Pin|Signal|Wire Color|FTDI Board PIN|
|---|---|---|---|
|PA2|USART2_TX|Gray|FTDI_RX|
|PA3|USART2_RX|White|FTDI_TX|
|GND|Ground|Black|FTDI_GND|
 <br>
Reference Information:<br>
https://www.st.com/resource/en/user_manual/um1075-stlinkv2-incircuit-debuggerprogrammer-for-stm8-and-stm32-stmicroelectronics.pdf<br>
https://waterpigs.co.uk/articles/black-blue-pill-stm32-st-link-connection/<br>
