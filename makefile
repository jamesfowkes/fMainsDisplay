NAME=test

AVR_DIR=C:/WinAVR-20100110/bin

CC=$(AVR_DIR)/avr-gcc
MCU_TARGET=attiny84
LIBS_DIR = C:/Users/fowkes_james/Documents/GitHub/Code-Library

OPT_LEVEL=3

ERROR_FILE="error.txt"

INCLUDE_DIRS = \
	-I$(LIBS_DIR)/AVR \
	-I$(LIBS_DIR)/Common \
	-I$(LIBS_DIR)/Devices \
	-I$(LIBS_DIR)/Generics \
	-I$(LIBS_DIR)/Utility

MAIN_FILE = main.c
CFILES = \
	$(LIBS_DIR)/AVR/lib_clk.c \
	$(LIBS_DIR)/AVR/lib_fuses.c \
	$(LIBS_DIR)/AVR/lib_tmr8.c \
	$(LIBS_DIR)/AVR/lib_tmr8_tick.c \
	$(LIBS_DIR)/AVR/lib_io.c \
	$(LIBS_DIR)/AVR/lib_shiftregister.c \
	$(LIBS_DIR)/Devices/lib_tlc5916.c \
	$(LIBS_DIR)/Generics/memorypool.c \
	$(LIBS_DIR)/Generics/ringbuf.c \
	$(LIBS_DIR)/Generics/seven_segment_map.c
	
CFILES += $(MAIN_FILE)

OPTS = \
	-g \
	-Wall \
	-Wextra \
	-DF_CPU=8000000 \

LDFLAGS = \
	-Wl

OBJDEPS=$(CFILES:.c=.o)

all: init $(NAME).elf errors

process:
	$(CC) -E $(INCLUDE_DIRS) $(MAIN_FILE)
	
init:
	@rm -f $(ERROR_FILE)
	
$(NAME).elf: $(OBJDEPS)
	$(CC) $(INCLUDE_DIRS) $(OPTS) $(LDFLAGS) -O$(OPT_LEVEL) -mmcu=$(MCU_TARGET) -o $@ $^

%.o:%.c
	$(CC) $(INCLUDE_DIRS) $(OPTS) -O$(OPT_LEVEL) -mmcu=$(MCU_TARGET) -c $< -o $@ 2>>$(ERROR_FILE)

errors:
	@echo "Errors and Warnings:"
	@cat $(ERROR_FILE)
clean:
	rm -rf $(NAME).elf
	rm -rf $(OBJDEPS)