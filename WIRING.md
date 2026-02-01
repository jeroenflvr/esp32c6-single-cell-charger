# Wiring Guide

## Voltage Divider Circuit

The ESP32-C6 ADC can only measure up to ~3.3V, but lithium cells can reach 4.2V when fully charged. A voltage divider is required to scale the voltage down.

### Circuit Diagram

```
Battery +  ──────┬──────── Charger IN+
                 │
                ┌┴┐
                │ │ R1 (200kΩ)
                │ │
                └┬┘
                 │
                 ├──────── GPIO1 (ADC input)
                 │
                ┌┴┐
                │ │ R2 (100kΩ)
                │ │
                └┬┘
                 │
Battery -  ──────┴──────── GND ──────── Charger IN-
```

### Component Values

| Component | Value | Purpose |
|-----------|-------|---------|
| R1 | 200kΩ | Upper resistor |
| R2 | 100kΩ | Lower resistor |
| Divider Ratio | 3.33 | (R1 + R2) / R2 |

With these values:
- 4.2V battery → ~1.26V at ADC
- 3.0V battery → ~0.90V at ADC

### Pin Connections

| ESP32-C6 Pin | Connection |
|--------------|------------|
| GPIO1 | Voltage divider center point |
| GND | Battery negative / Charger negative |
| 3V3 | (Optional) Power from USB |

## Complete Setup with TP4056 Charger

```
                    ┌─────────────────┐
USB Power ────────▶ │     TP4056      │
                    │  Charger Module │
                    │                 │
                    │ B+          OUT+├────────┐
                    │ B-          OUT-├────┐   │
                    └─────────────────┘    │   │
                           │  │            │   │
                           │  │            │   │
                    ┌──────┴──┴─────┐      │   │
                    │    Battery    │      │   │
                    │   (18650)     │      │   │
                    └───────────────┘      │   │
                                           │   │
                                    GND ◀──┘   │
                                               │
    ┌──────────────────────────────────────────┘
    │
    ├───[200kΩ]───┬───[100kΩ]───GND
                  │
                  └───────▶ GPIO1 (ESP32-C6)
```

## Notes

1. **Resistor tolerance**: Use 1% resistors for better accuracy
2. **Calibration**: Measure actual resistor values and adjust `VOLTAGE_DIVIDER` in `sensor.c` if needed
3. **Safety**: Never exceed 4.2V input to the divider
4. **Filtering**: A 100nF capacitor between GPIO1 and GND can reduce noise (optional)
