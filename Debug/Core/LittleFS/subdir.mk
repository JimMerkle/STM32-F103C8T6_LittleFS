################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (10.3-2021.10)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Core/LittleFS/lfs.c \
../Core/LittleFS/lfs_util.c 

OBJS += \
./Core/LittleFS/lfs.o \
./Core/LittleFS/lfs_util.o 

C_DEPS += \
./Core/LittleFS/lfs.d \
./Core/LittleFS/lfs_util.d 


# Each subdirectory must supply rules for building sources it contributes
Core/LittleFS/%.o Core/LittleFS/%.su: ../Core/LittleFS/%.c Core/LittleFS/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m3 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32F103xB -c -I../Core/Inc -I../Core/LittleFS -I../Drivers/STM32F1xx_HAL_Driver/Inc/Legacy -I../Drivers/STM32F1xx_HAL_Driver/Inc -I../Drivers/CMSIS/Device/ST/STM32F1xx/Include -I../Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfloat-abi=soft -mthumb -o "$@"

clean: clean-Core-2f-LittleFS

clean-Core-2f-LittleFS:
	-$(RM) ./Core/LittleFS/lfs.d ./Core/LittleFS/lfs.o ./Core/LittleFS/lfs.su ./Core/LittleFS/lfs_util.d ./Core/LittleFS/lfs_util.o ./Core/LittleFS/lfs_util.su

.PHONY: clean-Core-2f-LittleFS

