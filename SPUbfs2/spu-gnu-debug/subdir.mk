################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../SPUbfs2.c 

OBJS += \
./SPUbfs2.o 

C_DEPS += \
./SPUbfs2.d 


# Each subdirectory must supply rules for building sources it contributes
%.o: ../%.c
	@echo 'Building file: $<'
	@echo 'Invoking: SPU GNU C Compiler with Debug Options'
	spu-gcc -I"/home/ste/workspace/PPUbfs2" -O0 -g3 -Wall -c -fmessage-length=0 -Winline -Wextra -fno-inline -mtune=cell -mfloat=fast -mdouble=fast -Wno-main -march=cell -mea32 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@:%.o=%.d)" -o"$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


