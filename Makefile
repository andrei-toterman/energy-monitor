SKETCH    := EnergyMonitor
BUILD_DIR := build
FQBN      := esp8266:esp8266:nodemcu
BAUD      := 115200
PORT      ?= /dev/ttyUSB0

SKETCH_OUT := $(BUILD_DIR)/$(SKETCH)/$(SKETCH).ino

.PHONY: compile upload monitor flash clean deps

LIBS := "ESP8266 Influxdb@3.13.2" "AceTime@4.1.0" "ArduinoJson@7.4.3"

deps:
	@for lib in $(LIBS); do \
		arduino-cli lib install "$$lib"; \
	done

# --warnings more = -Wall: at default (none) GCC does not flag mismatched
# scanf/printf format strings, which the sscanf timestamp parsing and the
# snprintf date/nonce formatting depend on.
compile: deps $(SKETCH_OUT)
	arduino-cli compile --warnings more --fqbn $(FQBN) $(BUILD_DIR)/$(SKETCH)

upload: $(SKETCH_OUT)
	arduino-cli upload --fqbn $(FQBN) --port $(PORT) $(BUILD_DIR)/$(SKETCH)

monitor:
	arduino-cli monitor --port $(PORT) --config baudrate=$(BAUD)

flash: upload monitor

clean:
	rm -rf $(BUILD_DIR)

HEADERS := AmsterdamTz.h HomeWizardData.h HttpRequest.h APsystemsData.h

# The sketch and plain headers are copied verbatim; secrets.h is
# envsubst-expanded into the build dir so real values never land in the
# repo (build/ is gitignored).
$(SKETCH_OUT): EnergyMonitor.ino $(HEADERS) secrets.h .envrc
	mkdir -p $(BUILD_DIR)/$(SKETCH)
	cp EnergyMonitor.ino $(HEADERS) $(BUILD_DIR)/$(SKETCH)/
	envsubst < secrets.h > $(BUILD_DIR)/$(SKETCH)/secrets.h
