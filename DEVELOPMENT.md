# Development Guide

## Prerequisites

- ESP-IDF v5.1.2 or later
- Python 3.8+
- USB cable for ESP32-C6

## Project Structure

```
esp32c6-single-cell-charger/
├── main/
│   ├── CMakeLists.txt      # Component build config
│   ├── Kconfig.projbuild   # Menu config options
│   ├── main.c              # Application entry point
│   ├── sensor.c/h          # ADC & voltage monitoring
│   ├── wifi_manager.c/h    # WiFi connection handling
│   ├── webserver.c/h       # HTTP server & dashboard
│   ├── influxdb.c/h        # InfluxDB client
│   ├── config.c/h          # NVS & .env configuration
│   ├── provisioning.c/h    # WiFi provisioning portal
│   ├── time_manager.c/h    # NTP time synchronization
│   └── *.html              # Web UI templates
├── data/                   # SPIFFS filesystem content
├── partitions.csv          # Custom partition table
├── sdkconfig               # ESP-IDF configuration
└── CMakeLists.txt          # Project build config
```

## Key Configuration Constants

### sensor.c

```c
#define BATTERY_ADC_GPIO        GPIO_NUM_1      // ADC input pin
#define VOLTAGE_DIVIDER         3.33f           // Voltage divider ratio
#define VOLTAGE_EMA_ALPHA       0.1f            // Smoothing factor
#define BATTERY_ADC_SAMPLES     16              // Samples per reading
#define VOLTAGE_RISING_THRESHOLD  0.020f        // 20mV for charging detection
#define VOLTAGE_FALLING_THRESHOLD 0.020f        // 20mV for discharging
#define VOLTAGE_STABLE_COUNT    10              // Readings for trend detection
#define NO_CELL_VOLTAGE_THRESHOLD 0.5f          // Voltage below = no cell
```

### main.c

```c
#define READING_INTERVAL_MS     1000            // Sensor polling interval
#define INFLUXDB_SEND_INTERVAL  60000           // InfluxDB reporting interval
```

## Building

```bash
# Configure target
idf.py set-target esp32c6

# Build
idf.py build

# Flash and monitor
idf.py flash monitor
```

## Common Development Tasks

### Adjusting Voltage Calibration

1. Connect a known voltage source
2. Read ESP32 measurement from logs
3. Calculate correction factor:
   ```
   new_divider = current_divider × (measured_voltage / actual_voltage)
   ```
4. Update `VOLTAGE_DIVIDER` in `sensor.c`

### Adding New API Endpoints

1. Add handler in `webserver.c`:
   ```c
   static esp_err_t my_handler(httpd_req_t *req) {
       httpd_resp_sendstr(req, "Response");
       return ESP_OK;
   }
   ```

2. Register in `start_webserver()`:
   ```c
   httpd_uri_t my_uri = {
       .uri = "/api/myendpoint",
       .method = HTTP_GET,
       .handler = my_handler
   };
   httpd_register_uri_handler(server, &my_uri);
   ```

### Modifying Charge State Detection

The algorithm in `sensor_update_charge_state()`:
1. Compares current voltage against 10 readings ago
2. Requires 3 consistent readings for state change
3. Uses thresholds to filter noise

### Adding InfluxDB Fields

Modify `influxdb_send_data()` in `influxdb.c`:
```c
snprintf(line, sizeof(line),
    "battery_charging,device=%s,cell_id=%s "
    "voltage=%.3f,new_field=%.2f,... %lld",
    device_id, cell_id, voltage, new_value, timestamp_ns);
```

## Partition Table

Custom partition layout (`partitions.csv`):

| Name | Type | Offset | Size |
|------|------|--------|------|
| nvs | data | 0x9000 | 24KB |
| phy_init | data | 0xf000 | 4KB |
| factory | app | 0x10000 | 1.5MB |
| storage | data | 0x190000 | 64KB |

## Debug Logging

Enable verbose logging in `sdkconfig`:
```
CONFIG_LOG_DEFAULT_LEVEL_DEBUG=y
```

Or per-module:
```c
esp_log_level_set("sensor", ESP_LOG_DEBUG);
```

## Testing

### Simulate Charging

Connect a variable power supply (3.0V - 4.2V) to the voltage divider input and slowly increase voltage.

### API Testing

```bash
# Get status
curl http://<device-ip>/api/status

# Watch real-time (with jq)
watch -n1 'curl -s http://<device-ip>/api/status | jq'
```

## Troubleshooting Development Issues

### Build Errors

```bash
# Clean build
idf.py fullclean
idf.py build
```

### Flash Issues

```bash
# Erase flash completely
idf.py erase-flash
idf.py flash
```

### Stack Overflow

Increase task stack size in `main.c`:
```c
xTaskCreate(task_func, "name", 8192, NULL, 5, NULL);
```

## VS Code Tasks

Available tasks (Ctrl+Shift+B):
- Build
- Flash
- Monitor
- Flash and Monitor
- Clean / Full Clean
- Upload SPIFFS
