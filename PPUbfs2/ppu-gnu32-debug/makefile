################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Custom includes added by CellDT makefile generator
-include import_spu.mk

# Macros added by the CDT's makefile generator

-include ../makefile.init

RM := rm -fr

# All of the sources participating in the build are defined here
-include sources.mk
-include subdir.mk
-include spu_objs/subdir.mk
-include objects.mk

ifneq ($(MAKECMDGOALS),clean)
ifneq ($(strip $(C_DEPS)),)
-include $(C_DEPS)
endif
endif

-include ../makefile.defs

# Add inputs and outputs from these tool invocations to the build variables 

# All Target
all: PPUbfs2

dependents:
	-cd /home/ste/workspace/SPUbfs2/spu-gnu-debug && $(MAKE) all

# Tool invocations
PPUbfs2: $(OBJS) $(USER_OBJS) /home/ste/workspace/SPUbfs2/spu-gnu-debug/SPUbfs2
	@echo 'Building target: $@'
	@echo 'Invoking: PPU GNU 32 bit C Linker'
	ppu-gcc -m32 -o"PPUbfs2" $(OBJS) $(USER_OBJS) $(LIBS)
	@echo 'Finished building target: $@'
	@echo ' '

# Other Targets
clean:
	-$(RM) $(OBJS)$(C_DEPS)$(EXECUTABLES) PPUbfs2
	-@echo ' '

.PHONY: all clean dependents
.SECONDARY:
/home/ste/workspace/SPUbfs2/spu-gnu-debug/SPUbfs2:

-include ../makefile.targets
