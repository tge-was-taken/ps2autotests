# _____     ___ ____     ___ ____
#  ____|   |    ____|   |        | |____|
# |     ___|   |____ ___|    ____| |    \    PS2DEV Open Source Project.
#-----------------------------------------------------------------------
# Copyright 2001-2004, ps2dev - http://www.ps2dev.org
# Licenced under Academic Free License version 2.0
# Review ps2sdk README & LICENSE files for further details.
#
# $Id: Makefile.eeglobal_cpp_sample 1525 2009-01-23 00:24:13Z radad $

# Define pefixes for the various toolchain binaries.
include $(COMMON_DIR)/common-defines.mk

# -miop is gone from the toolchain, and the irx is now assembled from a
# relocatable link plus iopfixup rather than by the linker alone.
CFLAGS_TARGET  = -D_IOP -fno-builtin -msoft-float -mno-explicit-relocs
ASFLAGS_TARGET = -march=r3000
LDFLAGS_TARGET =
IOP_FIXUP = $(PS2SDK)/bin/iopfixup

# include dir
IOP_INCS := -I$(PS2SDK)/iop/include -I$(PS2SDK)/common/include -I$(COMMON_DIR) \
	-I. $(IOP_INCS)
# C compiler flags
IOP_CFLAGS := $(CFLAGS_TARGET) -O2 -G0 -fno-toplevel-reorder $(IOP_INCS) $(IOP_CFLAGS)

# linker flags
IOP_LDFLAGS := $(LDFLAGS_TARGET) -nostdlib -dc -r -T$(PS2SDK)/iop/startup/linkfile -L$(PS2SDK)/iop/lib $(IOP_LDFLAGS)

# asssembler flags
IOP_ASFLAGS := $(ASFLAGS_TARGET) -EL -G0 $(IOP_ASFLAGS)

# link with following libraries (libs need to be defined multiple times in order for linking to work!!)
IOP_LIBS += -lkernel -lgcc

EXTRA_OBJS += $(COMMON_DIR)/common-iop.o $(COMMON_DIR)/xprintf.o $(COMMON_DIR)/common-imports.o

# Externally defined variables: IOP_BIN, IOP_OBJS, IOP_LIB

%.o : %.c
	$(IOP_CC) $(IOP_CFLAGS) -c $< -o $@

%.o : %.S
	$(IOP_CC) $(IOP_CFLAGS) -c $< -o $@

%.o : %.s
	$(IOP_AS) $(IOP_ASFLAGS) $< -o $@

%.o : %.lst
	echo "#include \"irx_imports.h\"" > build-imports.c
	cat $< >> build-imports.c
	$(IOP_CC) $(IOP_CFLAGS) -fno-toplevel-reorder -c build-imports.c -o $@
	rm -f build-imports.c

%.irx : %.o $(EXTRA_OBJS)
	$(IOP_CC) $(IOP_LDFLAGS) -o $*.linked $< $(EXTRA_OBJS) $(IOP_LIBS)
	$(IOP_FIXUP) --rb --irx1 --allow-zero-text -o $@ $*.linked

all: $(TARGETS:=.irx)

clean:
	-$(RM) -f $(TARGETS:=.irx) $(TARGETS:=.o) $(TARGETS:=.linked)

rebuild: clean all
