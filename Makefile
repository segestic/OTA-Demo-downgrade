# =============================================================================
# Makefile wrapper for PlatformIO
# =============================================================================
# DEFAULT ENVIRONMENT:
#   This should match [platformio] default_envs in platformio.ini.
#   Here it is set to esp32dev, which is your pioarduino environment.
#
# HOW TO USE:
#   make build
#   make upload
#   make monitor
#   make flash
#
# NON-DEFAULT ENVIRONMENT:
#   Pass ENV=dev_board or ENV=prod_board when you want another build target.
#   Example:
#       make upload ENV=prod_board
#       make flash ENV=dev_board
# =============================================================================

.PHONY: all build upload monitor flash clean fs test help

# Default environment. Override at the command line:
#   make upload ENV=prod_board
ENV ?= esp32dev

all: build

help:
	@echo "Available targets:"
	@echo "  make build                Build default env (or ENV=...)"
	@echo "  make upload               Upload default env"
	@echo "  make monitor              Open serial monitor"
	@echo "  make flash                Upload and then monitor"
	@echo "  make clean                Clean build files"
	@echo "  make fs                   Upload LittleFS / filesystem image"
	@echo "  make test                 Run PlatformIO tests"
	@echo ""
	@echo "Examples:"
	@echo "  make build"
	@echo "  make upload ENV=prod_board"
	@echo "  make flash ENV=dev_board"

build:
	pio run -e $(ENV)

upload:
	pio run -t upload -e $(ENV)

monitor:
	pio device monitor -e $(ENV)

flash:
	pio run -t upload -t monitor -e $(ENV)

clean:
	pio run -t clean -e $(ENV)

fs:
	pio run -t uploadfs -e $(ENV)

test:
	pio test -e $(ENV)
