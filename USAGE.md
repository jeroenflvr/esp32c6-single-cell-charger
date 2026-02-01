# Usage Guide

## Initial Setup

### First Boot (WiFi Provisioning)

1. Power on the ESP32-C6
2. Connect to the WiFi network: `charger-setup`
3. Open a browser and go to `http://192.168.4.1`
4. Enter your WiFi credentials
5. The device will reboot and connect to your network

### Alternative: Configuration File

Create a `.env` file in the SPIFFS `data/` folder:

```env
WIFI_SSID=your_network
WIFI_PASS=your_password
DEVICE_ID=esp32-singlecharger-001
INFLUXDB_URL=http://influxdb:8086
INFLUXDB_TOKEN=your_token
INFLUXDB_ORG=your_org
INFLUXDB_BUCKET=batteries
```

Upload with:
```bash
python -m spiffsgen 0x10000 data build/spiffs.bin
python -m esptool --chip esp32c6 --port /dev/cu.usbmodem101 write_flash 0x190000 build/spiffs.bin
```

## Web Dashboard

Access the dashboard at `http://<device-ip>/` (IP is shown in serial monitor on boot).

### Features

- **Real-time voltage display** with percentage
- **Charge state indicator** (Charging, Discharging, Idle)
- **Voltage history graph** (last 60 readings)
- **Device temperature** from internal sensor
- **Cell ID** for tracking different batteries
- **Charging timer** showing time since cell connected

### API Endpoints

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/` | GET | Web dashboard |
| `/api/status` | GET | JSON status data |

Example `/api/status` response:
```json
{
  "voltage": 3.70,
  "percentage": 50.0,
  "temperature": 27.0,
  "charge_state": "Idle",
  "charging_time_sec": 120,
  "cell_id": "CELL-00000008EC5C",
  "cell_present": true
}
```

## Charging States

| State | Description |
|-------|-------------|
| **Charging** | Voltage rising steadily (>20mV increase over 10 readings) |
| **Discharging** | Voltage falling steadily (>20mV decrease over 10 readings) |
| **Idle** | Voltage stable (no cell, or cell at rest) |

## InfluxDB Integration

Data is sent in InfluxDB line protocol format:

```
battery_charging,device=esp32-singlecharger-001,cell_id=CELL-00000008EC5C voltage=3.70,percentage=50.0,temp=27.0,charge_state="Idle",charging_time_sec=120i,cell_present=true 1769937277568966000
```

### Fields

| Field | Type | Description |
|-------|------|-------------|
| voltage | float | Battery voltage in volts |
| percentage | float | Estimated capacity percentage |
| temp | float | Device temperature in °C |
| charge_state | string | "Charging", "Discharging", or "Idle" |
| charging_time_sec | integer | Seconds since cell was connected |
| cell_present | boolean | Whether a cell is detected |

### Tags

| Tag | Description |
|-----|-------------|
| device | Device ID from configuration |
| cell_id | Unique ID for current cell |

## Voltage-to-Percentage Mapping

The percentage is estimated based on a typical Li-ion discharge curve:

| Voltage | Percentage |
|---------|------------|
| 4.20V | 100% |
| 4.06V | 90% |
| 3.98V | 80% |
| 3.92V | 70% |
| 3.87V | 60% |
| 3.82V | 50% |
| 3.79V | 40% |
| 3.77V | 30% |
| 3.74V | 20% |
| 3.68V | 10% |
| 3.50V | 5% |
| 3.00V | 0% |

## Troubleshooting

### Voltage Reading Incorrect

1. Verify voltage divider resistor values
2. Measure actual voltage with multimeter
3. Adjust `VOLTAGE_DIVIDER` constant in `sensor.c`:
   ```c
   #define VOLTAGE_DIVIDER 3.33f  // (R1 + R2) / R2
   ```

### Charge State Flapping

The firmware includes smoothing to prevent rapid state changes:
- EMA filter (alpha=0.1)
- Median filtering (16 samples)
- 20mV threshold for state changes
- 3 consistent readings required before state change

### WiFi Connection Issues

1. Check credentials in `.env` or provisioning
2. Ensure router is 2.4GHz (ESP32-C6 supports WiFi 6 on 2.4GHz)
3. Check serial monitor for connection status

### InfluxDB 404 Errors

1. Verify InfluxDB URL is correct
2. Check bucket exists
3. Verify token has write permissions
4. Ensure organization name matches
