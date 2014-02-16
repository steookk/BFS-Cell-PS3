################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../PPUbfs2.c 

OBJS += \
./PPUbfs2.o 

C_DEPS += \
./PPUbfs2.d 


# Each subdirectory must supply rules for building sources it contributes
%.o: ../%.c
	@echo 'Building file: $<'
	@echo 'Invoking: PPU GNU 32 bit C Compiler with Debug Options'
	ppu-gcc -O0 -g3 -Wall -c -fmessage-length=0 -Winline -Wextra -fno-inline -m32 -mabi=altivec -maltivec -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@:%.o=%.d)" -o"$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


