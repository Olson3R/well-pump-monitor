# Well Pump Monitor - Complete Wiring Diagram

## Components List
- ESP32 Development Board (30-pin)
- 0.91" OLED Display 128x32 (SSD1306, I2C)
- DHT11 Temperature/Humidity Sensor
- 1x ADS1115 16-bit ADC module (4-channel for current/voltage sensing)
- 2x Current transformers/sensors
- Optional: Additional sensors for pressure, etc.
- Micro USB Breakout Board (for 5V power input)
- Breadboard or PCB
- Jumper wires
- Pull-up resistors (if needed)

## Power Distribution

### Micro USB Breakout Board
```
Micro USB Breakout    Description
VCC/5V        →       5V Power Rail
GND           →       Ground Rail
D+            →       Not used
D-            →       Not used
```

### Power Rails
```
5V Rail: Micro USB VCC → ESP32 VIN (or 5V pin if available)
3.3V Rail: ESP32 3.3V output → All sensor VCC connections
GND Rail: Micro USB GND → ESP32 GND → All component grounds
```

## ESP32 Pin Assignments

### GPIO Pin Mapping
```
ESP32 Pin     Function            Connected To
---------     --------            ------------
GPIO2         Built-in LED        Onboard LED
GPIO21        I2C SDA             OLED SDA, ADS1115 SDA
GPIO22        I2C SCL             OLED SCL, ADS1115 SCL
GPIO23        DHT11 Data          DHT11 Data Pin
GND           Ground              Common ground rail
3.3V          3.3V Power          Sensor power rail
VIN           5V Input            Micro USB VCC (5V)
```

## Component Wiring Details

### OLED Display (SSD1306 128x32)
```
OLED Pin      ESP32 Pin          Notes
--------      ---------          -----
VCC           3.3V               Power (3.3V)
GND           GND                Ground
SDA           GPIO21             I2C Data
SCL           GPIO22             I2C Clock
```

### DHT11 Temperature/Humidity Sensor
```
DHT11 Pin     ESP32 Pin          Notes
---------     ---------          -----
VCC           3.3V               Power (3.3V)
GND           GND                Ground
DATA          GPIO23             Digital data pin
NC            Not connected      Not used
```

### ADS1115 ADC Module (4-Channel)
```
ADS1115 Pin   ESP32 Pin          Notes
-----------   ---------          -----
VDD           3.3V               Power (3.3V)
GND           GND                Ground
SCL           GPIO22             I2C Clock (shared)
SDA           GPIO21             I2C Data (shared)
A0            Current Sensor 1   Analog input from CT1
A1            Current Sensor 2   Analog input from CT2
A2            Pressure Sensor    Optional pressure transducer
A3            Spare Input        Available for additional sensor
ADDR          GND                I2C Address 0x48 (default)
ALRT          Not connected      Alert pin (optional)
```

### Current Transformers/Sensors
```
Current Sensor 1:
- Primary: In series with pump motor line 1 (AC line)
- Secondary: Connected to ADS1115 A0 input
- Burden resistor: As required by CT specifications

Current Sensor 2:
- Primary: In series with pump motor line 2 (if dual-phase)
- Secondary: Connected to ADS1115 A1 input
- Burden resistor: As required by CT specifications

Optional Pressure Sensor:
- Analog output: Connected to ADS1115 A2 input
- Power: 3.3V or 5V depending on sensor requirements
- Range: Typically 0-5V or 4-20mA (with shunt resistor)
```

## I2C Address Configuration

### I2C Device Addresses
```
Device                I2C Address    Configuration
------                -----------    -------------
OLED SSD1306          0x3C           Default address
ADS1115               0x48           ADDR pin to GND (default)
DHT11                 N/A            Uses GPIO23 (digital)

Note: ADS1115 can use addresses 0x48-0x4B by configuring ADDR pin:
- ADDR to GND: 0x48
- ADDR to VDD: 0x49  
- ADDR to SDA: 0x4A
- ADDR to SCL: 0x4B
```

## Physical Connection Layout

### Breadboard Layout (Top View)
```
                    ESP32 Development Board
                ┌─────────────────────────────┐
                │  3.3V  GND  GPIO21  GPIO22  │ ← Right side pins
                │                             │
                │          ESP32              │
                │                             │
                │  VIN   GND  GPIO23  GPIO2   │ ← Left side pins
                └─────────────────────────────┘
                       │     │     │      │
                       │     │     │      └─── LED (built-in)
                       │     │     └────────── DHT11 Data
                       │     └──────────────── Ground rail
                       └────────────────────── 5V from USB breakout

Power Rails:
5V Rail:  ═══════════════════════════════════
3.3V Rail: ═══════════════════════════════════
GND Rail:  ═══════════════════════════════════

I2C Bus (SDA/SCL):
         GPIO21 ─────┬─── OLED SDA
                     └─── ADS1115 SDA

         GPIO22 ─────┬─── OLED SCL
                     └─── ADS1115 SCL

ADS1115 Analog Inputs:
         A0 ───── Current Sensor 1
         A1 ───── Current Sensor 2  
         A2 ───── Pressure Sensor (optional)
         A3 ───── Spare (available)
```

### Connection Summary Table
```
Component         Pin    ESP32 Pin    Power    Ground
---------         ---    ---------    -----    ------
Micro USB Brkout  VCC    VIN          5V       -
Micro USB Brkout  GND    GND          -        GND
OLED Display      VCC    -            3.3V     -
OLED Display      GND    -            -        GND
OLED Display      SDA    GPIO21       -        -
OLED Display      SCL    GPIO22       -        -
DHT11 Sensor      VCC    -            3.3V     -
DHT11 Sensor      GND    -            -        GND
DHT11 Sensor      DATA   GPIO23       -        -
ADS1115           VDD    -            3.3V     -
ADS1115           GND    -            -        GND
ADS1115           SDA    GPIO21       -        -
ADS1115           SCL    GPIO22       -        -
ADS1115           ADDR   -            -        GND
ADS1115           A0     Current CT1  -        -
ADS1115           A1     Current CT2  -        -
ADS1115           A2     Pressure     -        -
ADS1115           A3     Spare        -        -
```

## Installation Notes

### Current Sensor Installation
1. **Safety First**: Turn off power before installing current transformers
2. **CT Installation**: Clamp current transformers around the AC power lines going to the pump motor
3. **Polarity**: Note the directional arrow on CTs for consistent readings
4. **Burden Resistors**: Install appropriate burden resistors based on CT specifications
5. **Isolation**: Ensure proper electrical isolation between AC mains and low-voltage circuits

### Enclosure Considerations
1. Use weatherproof enclosure for outdoor installation
2. Ensure adequate ventilation for ESP32
3. Protect connections from moisture
4. Label all wires for maintenance
5. Use strain relief for cable entries

### Testing Checklist
- [ ] Verify 5V power from USB breakout
- [ ] Check 3.3V output from ESP32
- [ ] Test I2C communication with OLED
- [ ] Verify DHT11 sensor readings
- [ ] Test ADS1115 ADC readings
- [ ] Confirm current sensor installations
- [ ] Test WiFi connectivity
- [ ] Verify web interface access
- [ ] Check MongoDB logging (if configured)

## Troubleshooting

### Common Issues
1. **Display not working**: Check I2C connections and address (0x3C)
2. **No sensor readings**: Verify power connections and DHT11 wiring
3. **Current readings wrong**: Check CT orientation and burden resistor values
4. **WiFi issues**: Ensure antenna is not blocked by enclosure
5. **Power problems**: Verify USB breakout provides stable 5V

### I2C Debugging
Use the I2C scanner in Arduino IDE to detect connected devices:
- OLED should appear at 0x3C
- ADS1115 should appear at 0x48 (with ADDR to GND)

### ADS1115 Channel Usage
```
Channel    Purpose           Sensor Type        Range
-------    -------           -----------        -----
A0         Current 1         CT/SCT-013         ±4.096V
A1         Current 2         CT/SCT-013         ±4.096V  
A2         Pressure          0-5V Transducer    0-5V
A3         Spare             Available          ±4.096V

Gain Settings:
- ±6.144V (gain = 2/3) for 0-5V pressure sensors
- ±4.096V (gain = 1) for current transformers
- ±2.048V (gain = 2) for lower voltage sensors
- ±1.024V (gain = 4) for precision measurements
```