CC65_HOME = /usr/local/share/cc65/
SYS	= nes

croper = croper
CL   = cl65
CC   = cc65
AS   = ca65
LD   = ld65

INC = $(CC65_HOME)/include
ASMINC = $(CC65_HOME)/asminc
# This one comes with VICE

# --static-locals
CFLAGS		:= -O
AFLAGS		:= -t $(SYS)
BDIR		:=build

# ===== Mapper selection =====
# Set MAPPER to one of: none (default), vrc6, vrc7, s5b, n163
MAPPER ?= none

ifeq ($(MAPPER),vrc6)
    DEFINE_FLAG = -DUSE_VRC6
    EXP_SRC = vrc6.c
    ROM_NAME = n8-midi-vrc6.nes
else ifeq ($(MAPPER),vrc7)
    DEFINE_FLAG = -DUSE_VRC7
    EXP_SRC = vrc7.c
    ROM_NAME = n8-midi-vrc7.nes
else ifeq ($(MAPPER),s5b)
    DEFINE_FLAG = -DUSE_S5B
    EXP_SRC = sunsoft.c
    ROM_NAME = n8-midi-s5b.nes
else ifeq ($(MAPPER),n163)
    DEFINE_FLAG = -DUSE_N163
    EXP_SRC = n163.c
    ROM_NAME = n8-midi-n163.nes
else
    DEFINE_FLAG =
    EXP_SRC =
    ROM_NAME = n8-midi.nes
endif

vpath %.o $(BDIR)/


SRC_S := crt0.s everdrive_asm.s
# Build only the base C sources plus the selected expansion chip
SRC_C := main.c sys.c everdrive.c $(EXP_SRC)

OBJ = $(SRC_S:.s=.o)
OBJ += $(SRC_C:.c=.o)
OFILES = $(OBJ:%.o=$(BDIR)/%.o)

# --------------------------------------------------------------------------
all: $(OBJ)
	$(LD) -C nes.cfg -o $(ROM_NAME) $(OFILES) nes.lib -m $(BDIR)/mem.map

# Build all mapper variants
all-mappers:
	$(MAKE) MAPPER=none clean all
	$(MAKE) MAPPER=vrc6 clean all
	$(MAKE) MAPPER=vrc7 clean all
	$(MAKE) MAPPER=s5b  clean all
	$(MAKE) MAPPER=n163 clean all

%.o: %.s
	$(AS) $(AFLAGS) $(DEFINE_FLAG) -o $(basename $(BDIR)/$<).o $<

%.o: %.c
	$(CC) $(CFLAGS) $(DEFINE_FLAG) -o $(basename $(BDIR)/$<).s $<
	$(AS) $(AFLAGS) $(basename $(BDIR)/$<).s



ifeq ($(OS),Windows_NT)
clean:
	-powershell -NoProfile -Command "Remove-Item -Force -ErrorAction SilentlyContinue '$(BDIR)/*.o','$(BDIR)/*.s'"
else
clean:
	rm -f $(BDIR)/*.o $(BDIR)/*.s
endif
