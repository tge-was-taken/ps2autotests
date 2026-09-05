ifeq ($(shell command -v ee-gcc 2>/dev/null),)
EE_PREFIX = mips64r5900el-ps2-elf-
else
EE_PREFIX = ee-
endif

ifeq ($(shell command -v iop-gcc 2>/dev/null),)
IOP_PREFIX = mipsel-none-elf-
else
IOP_PREFIX = iop-
endif

EE_CC = $(EE_PREFIX)gcc
EE_CXX= $(EE_PREFIX)g++
EE_AS = $(EE_PREFIX)as
EE_LD = $(EE_PREFIX)ld
EE_AR = $(EE_PREFIX)ar
EE_OBJCOPY = $(EE_PREFIX)objcopy
EE_STRIP = $(EE_PREFIX)strip

IOP_CC = $(IOP_PREFIX)gcc
IOP_AS = $(IOP_PREFIX)as
IOP_LD = $(IOP_PREFIX)ld
IOP_AR = $(IOP_PREFIX)ar
IOP_OBJCOPY = $(IOP_PREFIX)objcopy
IOP_STRIP = $(IOP_PREFIX)strip

DVP_PREFIX = dvp-
DVP_AS = $(DVP_PREFIX)as

RM=rm
