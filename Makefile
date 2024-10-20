# Назва проекту
PROJECT_NAME = LAB4
# Модель мікроконтролера
MCU = atmega328p
# Частота тактового генератора
F_CPU = 16000000UL
# Порт для програмування
UPLOAD_PORT = /dev/ttyUSB0
# Швидкість передачі даних
UPLOAD_PORT_BAUD = 115200
# Абсолютний шлях до крос-компілятора (тулчейна) 
TOOLCHAIN_PATH = 

# Абсолютний шлях до програми-програматора 
AVRDUDE = $(TOOLCHAIN_PATH)/bin/avrdude
AVRDUDE_CONF = $(TOOLCHAIN_PATH)/etc/avrdude.conf

# Sources files needed for building the application 
SRC = main.c

# The headers files needed for building the application
INCLUDE = -I.

#--------------------------------------------------------------
PROJECT_NAME := $(strip $(PROJECT_NAME))
THIS_DIR := $(dir $(abspath $(firstword $(MAKEFILE_LIST))))

# Define TARGET_DIR based on OS
TARGET_DIR = $(THIS_DIR)build
RM = rm -rf

CC = $(TOOLCHAIN_PATH)/bin/avr-gcc
OBJCOPY = $(TOOLCHAIN_PATH)/bin/avr-objcopy

# Define object files
OBJS := $(addsuffix .o,$(basename $(SRC)))
OBJS := $(addprefix $(TARGET_DIR)/,$(OBJS))

all: $(TARGET_DIR) build/$(PROJECT_NAME).hex sizedummy

build/$(PROJECT_NAME).hex: build/$(PROJECT_NAME).elf
	@echo Create exec file $@
	@$(OBJCOPY) -O ihex -R .eeprom $< $@
	@$(TOOLCHAIN_PATH)/bin/avr-objdump -h -S $<  > build/$(PROJECT_NAME).lss
	@echo BUILD SUCCESS!

build/$(PROJECT_NAME).elf: $(OBJS)	
	@echo Building target: $@
	@$(CC) -Os -mmcu=$(MCU) -o build/$(PROJECT_NAME).elf $^

$(TARGET_DIR)/%.o: %.c # | $(TARGET_DIR)
	@echo Compile file: $<
	@$(CC) -Wall -Os -fpack-struct -fshort-enums -ffunction-sections -fdata-sections -std=gnu99 -funsigned-char -funsigned-bitfields -mmcu=$(MCU) -DF_CPU=$(F_CPU) -o $@ -c $<

$(TARGET_DIR):
	mkdir -p $(TARGET_DIR)

.PHONY: all clean sizedummy

sizedummy:
	@echo
	@$(TOOLCHAIN_PATH)/bin/avr-size --format=avr --mcu=$(MCU) build/$(PROJECT_NAME).elf
	
flash:
	@$(AVRDUDE) -C$(AVRDUDE_CONF) -F -v -p$(MCU) -carduino -P$(UPLOAD_PORT) -b$(UPLOAD_PORT_BAUD) -D -Uflash:w:build/$(PROJECT_NAME).hex:i

clean:
	@$(RM) build/*.o build/$(PROJECT_NAME).elf build/$(PROJECT_NAME).hex

serial:
	python3 -m serial.tools.miniterm $(UPLOAD_PORT) 9600
