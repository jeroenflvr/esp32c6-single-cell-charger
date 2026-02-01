.PHONY: load-sdk build flash monitor clean fullclean menuconfig erase-flash reconfigure upload-spiffs create-env

load-sdk:
	@echo "Loading SDK..."
	# Add commands to load the SDK here
	. /Users/jeroen/esp/esp-idf/export.sh
	idf.py set-target esp32c6

build: 
	@echo "Building project..."
	idf.py build

flash: upload-spiffs
	@echo "Flashing project to device..."
	idf.py flash

monitor:
	@echo "Starting monitor..."
	idf.py monitor

flash-monitor: upload-spiffs flash
	@echo "Flashing and monitoring..."
	idf.py monitor

clean:
	@echo "Cleaning build artifacts..."
	idf.py clean

fullclean:
	@echo "Performing full clean (removes build directory)..."
	idf.py fullclean

reconfigure:
	@echo "Reconfiguring project..."
	idf.py reconfigure

menuconfig:
	@echo "Opening configuration menu..."
	idf.py menuconfig

erase-flash:
	@echo "Erasing flash memory..."
	idf.py erase-flash

# Utility targets
size:
	@echo "Analyzing binary size..."
	idf.py size

app-flash:
	@echo "Flashing only the app (faster)..."
	idf.py app-flash

# Combined workflow targets
rebuild: clean build

reset: fullclean build

provision: erase-flash flash-monitor

# SPIFFS targets for development mode
upload-spiffs:
	@echo "Creating SPIFFS image and uploading to device..."
	@if [ ! -d "data" ]; then \
		echo "Error: data/ directory not found. Create it and add your .env file."; \
		exit 1; \
	fi
	/Users/jeroen/.espressif/python_env/idf5.1_py3.12_env/bin/python /Users/jeroen/esp/esp-idf/components/spiffs/spiffsgen.py 0x10000 data build/spiffs.bin
	/Users/jeroen/.espressif/python_env/idf5.1_py3.12_env/bin/python /Users/jeroen/esp/esp-idf/components/esptool_py/esptool/esptool.py --chip esp32c6 --port /dev/cu.usbmodem101 write_flash 0x190000 build/spiffs.bin

create-env:
	@echo "Creating .env file from template..."
	@if [ -f "data/.env" ]; then \
		echo "data/.env already exists. Remove it first if you want to recreate."; \
	else \
		cp data/.env.example data/.env; \
		echo "Created data/.env from template. Edit it with your configuration."; \
	fi