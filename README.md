# Single Cell Battery Charger Monitor

An ESP32-C6 based battery monitoring system for single-cell lithium batteries (18650, LiPo, etc.). Features a web dashboard, real-time voltage monitoring, charge state detection, and optional InfluxDB logging.

## Features

- **Real-time voltage monitoring** with EMA smoothing and median filtering
- **Charge state detection** (Charging, Discharging, Idle)
- **Web dashboard** with live updates and voltage graph
- **Cell tracking** with unique cell IDs
- **InfluxDB integration** for historical data logging
- **Internal temperature monitoring** using ESP32-C6's built-in sensor
- **WiFi provisioning** via SoftAP for easy setup

## Hardware

- **MCU**: ESP32-C6 (tested with Seeed XIAO ESP32-C6)
- **Voltage measurement**: ADC on GPIO1 with voltage divider
- **Charger module**: Any TP4056-based module (or similar)

## Quick Start

1. Wire the voltage divider to GPIO1 (see [WIRING.md](WIRING.md))
2. Build and flash:
   ```bash
   idf.py build flash monitor
   ```
3. Connect to WiFi via the provisioning portal
4. Access the dashboard at `http://<device-ip>/`

## Configuration

Configuration is stored in NVS and can be set via a `.env` file in SPIFFS:

```env
WIFI_SSID=your_ssid
WIFI_PASS=your_password
DEVICE_ID=esp32-singlecharger-001
INFLUXDB_URL=http://your-influxdb:8086
INFLUXDB_TOKEN=your_token
INFLUXDB_ORG=your_org
INFLUXDB_BUCKET=battery_data
```

## Build Commands

```bash
idf.py build          # Build the project
idf.py flash          # Flash to device
idf.py monitor        # Serial monitor
idf.py flash monitor  # Flash and monitor
```

## Documentation

- [WIRING.md](WIRING.md) - Hardware wiring guide
- [USAGE.md](USAGE.md) - Complete usage instructions
- [DEVELOPMENT.md](DEVELOPMENT.md) - Development guide

## License

MIT License
