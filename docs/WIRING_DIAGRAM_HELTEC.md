# Well Pump Monitor - Heltec LoRa32 V2 Wiring Diagram

## Components List
- Heltec WiFi LoRa 32 V2 Development Board
- Built-in 0.96" OLED Display 128x64 (SSD1306, I2C) - Integrated
- AHT10 Temperature/Humidity Sensor (I2C)
- 1x ADS1115 16-bit ADC module (4-channel for current/voltage sensing)
- 2x Current transformers/sensors
- Optional: Additional sensors for pressure, etc.
- Micro USB Breakout Board (for 5V power input)
- Breadboard or PCB
- Jumper wires
- **Level Shifting Components:**
  - 1x Bidirectional I2C Level Shifter (e.g., TXS0102, PCA9306, or similar)
  - Alternative: 4-channel level shifter module (TXS0104E)

## Heltec LoRa32 V2 Specifications
- **MCU**: ESP32 (dual-core, 240MHz)
- **Flash**: 8MB
- **RAM**: 320KB
- **Built-in OLED**: 128x64 SSD1306 (I2C: SDA=GPIO4, SCL=GPIO15, RST=GPIO16)
- **LoRa Module**: SX1276/78 (433/868/915MHz)
- **WiFi/Bluetooth**: Integrated ESP32 capabilities
- **USB**: Built-in USB-C connector for programming/power

## Power Distribution

### Power Options
```
Option 1: USB-C (Built-in)
USB-C Connector  →  5V/3.3V Power Rails

Option 2: Micro USB Breakout Board (External)
Micro USB Breakout    Description
VCC/5V        →       Heltec VIN or 5V pin
GND           →       Ground Rail
D+            →       Not used
D-            →       Not used
```

### Power Rails
```
5V Rail: USB power → Heltec VIN → ADS1115 VDD
3.3V Rail: Heltec 3.3V output → AHT10 VCC, I2C level shifter reference
GND Rail: Heltec GND → All component grounds
```

## Heltec LoRa32 V2 Pin Assignments

### GPIO Pin Mapping
```
Heltec Pin    Function            Connected To
----------    --------            ------------
GPIO2         Built-in LED        Onboard LED
GPIO4         OLED SDA            Built-in OLED (fixed)
GPIO15        OLED SCL            Built-in OLED (fixed)
GPIO16        OLED RST            Built-in OLED (fixed)
GPIO21        I2C SDA             AHT10 SDA, ADS1115 SDA (external sensors)
GPIO22        I2C SCL             AHT10 SCL, ADS1115 SCL (external sensors)
GND           Ground              Common ground rail
3.3V          3.3V Power          Sensor power rail
VIN           5V Input            External 5V (if using external power)
```

### LoRa Module Pins (Available for Future Use)
```
Heltec Pin    LoRa Function       Notes
----------    -------------       -----
GPIO5         LoRa SCK            SPI Clock
GPIO19        LoRa MISO           SPI MISO
GPIO27        LoRa MOSI           SPI MOSI
GPIO18        LoRa CS             Chip Select
GPIO14        LoRa RST            Reset
GPIO26        LoRa IRQ            Interrupt
```

## Component Wiring Details

### Built-in OLED Display (Pre-wired)
```
OLED Function     Heltec Pin         Notes
-------------     ----------         -----
VCC               3.3V               Internal connection
GND               GND                Internal connection
SDA               GPIO4              Internal connection (I2C Bus 0)
SCL               GPIO15             Internal connection (I2C Bus 0)
RST               GPIO16             Internal connection
```

### AHT10 Temperature/Humidity Sensor (I2C)
```
AHT10 Pin     Heltec Pin         Notes
---------     ----------         -----
VCC           3.3V               Power (3.3V)
GND           GND                Ground
SDA           GPIO21             I2C Data (via level shifter - see below)
SCL           GPIO22             I2C Clock (via level shifter - see below)

Note: AHT10 operates at 3.3V but shares I2C bus with 5V ADS1115
Level shifter ensures proper communication between different voltage levels
```

### ADS1115 ADC Module (4-Channel) - External I2C Bus
```
ADS1115 Pin   Heltec Pin         Notes
-----------   ----------         -----
VDD           5V                 Power (5V) - see voltage divider below
GND           GND                Ground
SCL           GPIO22             I2C Clock (via voltage divider)
SDA           GPIO21             I2C Data (via voltage divider)
A0            Current Sensor 1   Analog input from CT1
A1            Current Sensor 2   Analog input from CT2
A2            Pressure Sensor    Optional pressure transducer
A3            Spare Input        Available for additional sensor
ADDR          GND                I2C Address 0x48 (default)
ALRT          Not connected      Alert pin (optional)
```

### I2C Level Shifter (5V ↔ 3.3V)
```
Bidirectional I2C Level Shifter Module (e.g., TXS0102, PCA9306):

Low Voltage Side (3.3V):        Level Shifter        High Voltage Side (5V):
─────────────────────────        ─────────────        ──────────────────────

Heltec 3.3V     ──────────────── VCC_A               VCC_B ──────────────── 5V Rail
Heltec GND      ──────────────── GND                 GND   ──────────────── GND Rail
Heltec GPIO21   ──────────────── SDA_A               SDA_B ──────────────── ADS1115 SDA
Heltec GPIO22   ──────────────── SCL_A               SCL_B ──────────────── ADS1115 SCL
AHT10 SDA       ──────────────── SDA_A (shared)
AHT10 SCL       ──────────────── SCL_A (shared)

Features:
- Bidirectional communication (both directions)
- Automatic direction sensing
- Built-in pull-up resistors (typically 10kΩ)
- No propagation delay
- Supports I2C speeds up to 400kHz (Fast Mode)
- OE (Output Enable) pin - tie to VCC_A for always enabled

Connection Notes:
- Both AHT10 and Heltec connect to 3.3V side (A ports)
- ADS1115 connects to 5V side (B ports)
- No additional pull-up resistors needed
- Enable pin (OE) should be tied to VCC_A (3.3V)
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

## I2C Bus Configuration

### Dual I2C Bus Setup
```
I2C Bus 0 (Built-in OLED):
Device                I2C Address    Pins
------                -----------    ----
OLED SSD1306          0x3C           SDA=GPIO4, SCL=GPIO15

I2C Bus 1 (External Sensors):
Device                I2C Address    Pins
------                -----------    ----
AHT10                 0x38           SDA=GPIO21, SCL=GPIO22
ADS1115               0x48           SDA=GPIO21, SCL=GPIO22
```

### I2C Address Notes
```
OLED (Built-in):      0x3C (fixed)
AHT10:                0x38 (fixed I2C address)
ADS1115:              0x48 (ADDR pin to GND)

ADS1115 can use addresses 0x48-0x4B by configuring ADDR pin:
- ADDR to GND: 0x48
- ADDR to VDD: 0x49  
- ADDR to SDA: 0x4A
- ADDR to SCL: 0x4B
```

## Physical Connection Layout

### Heltec LoRa32 V2 Board Layout
```
                    Heltec WiFi LoRa 32 V2
                ┌─────────────────────────────┐
    USB-C ──────│                             │
                │  [OLED Display 128x64]      │
                │                             │
                │         ESP32               │
                │                             │
                │  3.3V  GND  GPIO21  GPIO22  │ ← External I2C
                │  VIN   GND  GPIO23  GPIO2   │ ← DHT11, LED
                └─────────────────────────────┘
                       │     │     │      │
                       │     │     │      └─── LED (built-in)
                       │     │     └────────── DHT11 Data
                       │     └──────────────── Ground rail
                       └────────────────────── 5V (if external power)

External I2C Bus with Level Shifter (GPIO21/22):

3.3V Side (Low Voltage):          Level Shifter IC          5V Side (High Voltage):
────────────────────────          ────────────────          ───────────────────────

Heltec 3.3V ──────────────────── VCC_A     VCC_B ──────────────────── 5V Rail
Heltec GND  ──────────────────── GND       GND   ──────────────────── GND Rail

GPIO21 ─────┬─────────────────── SDA_A     SDA_B ──────────────────── ADS1115 SDA
            │
AHT10 SDA ──┘

GPIO22 ─────┬─────────────────── SCL_A     SCL_B ──────────────────── ADS1115 SCL  
            │
AHT10 SCL ──┘

Enable Pin: ────────────────── OE ← Tie to VCC_A (3.3V)

ADS1115 Analog Inputs:
         A0 ───── Current Sensor 1
         A1 ───── Current Sensor 2  
         A2 ───── Pressure Sensor (optional)
         A3 ───── Spare (available)
```

### Connection Summary Table
```
Component         Pin         Heltec Pin   Power    Ground
---------         ---         ----------   -----    ------
USB Power         Built-in    USB-C        5V       -
AHT10 Sensor      VCC         -            3.3V     -
AHT10 Sensor      GND         -            -        GND
AHT10 Sensor      SDA         GPIO21       -        -
AHT10 Sensor      SCL         GPIO22       -        -
ADS1115           VDD         -            5V       -
ADS1115           GND         -            -        GND
ADS1115           SDA         GPIO21       -        -
ADS1115           SCL         GPIO22       -        -
ADS1115           ADDR        -            -        GND
ADS1115           A0          Current CT1  -        -
ADS1115           A1          Current CT2  -        -
ADS1115           A2          Pressure     -        -
ADS1115           A3          Spare        -        -
Built-in OLED     All         Internal     Internal Internal
Level Shift R1    1kΩ         5V→SDA       5V       -
Level Shift R2    2kΩ         SDA→GND      -        GND
Level Shift R3    1kΩ         5V→SCL       5V       -
Level Shift R4    2kΩ         SCL→GND      -        GND
```

## Advantages of Heltec LoRa32 V2

### Hardware Benefits
1. **Integrated Design**: Built-in OLED eliminates external display wiring
2. **More Flash**: 8MB vs 4MB (double the storage)
3. **USB-C**: Modern connector for programming and power
4. **LoRa Ready**: Future expansion for remote monitoring
5. **Dual I2C**: Separate buses for display and sensors

### Future LoRa Expansion Possibilities
1. **Remote Monitoring**: Send data to distant base station
2. **Mesh Network**: Multiple pump monitors communicating
3. **Low Power**: LoRa's long range with low power consumption
4. **Backup Communication**: If WiFi fails, use LoRa
5. **Field Deployment**: Monitor remote pumps without WiFi

## Installation Notes

### Power Considerations
1. **USB-C Power**: Can use built-in USB-C for development/testing
2. **External Power**: Use VIN pin for permanent 5V installation
3. **Power Consumption**: ~240mA typical (ESP32 + OLED + sensors)
4. **LoRa Power**: Additional ~120mA when transmitting
5. **ADS1115 5V Power**: Improves ADC accuracy and input range
6. **Level Shifter**: Minimal power consumption (~1-2mA)

### Level Shifter Notes
1. **Voltage Translation**: Converts between 5V (ADS1115) and 3.3V (Heltec)
2. **Bidirectional**: Works for both SDA and SCL lines
3. **Pull-up Resistors**: 1kΩ to 5V provides proper logic levels
4. **Pull-down Resistors**: 2kΩ to GND creates voltage divider
5. **AHT10 Compatibility**: 3.3V sensor works on shared bus
6. **Alternative**: Could use dedicated I2C level shifter IC (TXS0102) for cleaner design

### Enclosure Considerations
1. **OLED Visibility**: Position board so display is visible
2. **USB-C Access**: Allow access for programming/updates
3. **LoRa Antenna**: Ensure external antenna connection if using LoRa
4. **Heat Dissipation**: ESP32 can get warm under load

### Testing Checklist
- [ ] Verify USB-C power connection
- [ ] Test built-in OLED display
- [ ] Check DHT11 sensor readings  
- [ ] Test ADS1115 I2C communication (address 0x48)
- [ ] Verify current sensor installations
- [ ] Test WiFi connectivity
- [ ] Verify web interface access
- [ ] Check MongoDB logging (if configured)
- [ ] Test LoRa functionality (future)

## Troubleshooting

### Common Issues
1. **OLED not working**: Check if conflicting with external I2C on GPIO4/15
2. **I2C conflicts**: Use separate buses (built-in vs external)
3. **Power issues**: Ensure adequate current supply for all components
4. **LoRa interference**: May affect WiFi performance if both active
5. **Level shifter problems**: Check resistor values and connections
6. **ADS1115 not detected**: Verify 5V power and level shifter wiring
7. **Communication errors**: Ensure proper voltage levels on SDA/SCL

### I2C Debugging
```
Expected I2C Devices:
Bus 0 (GPIO4/15):  OLED at 0x3C
Bus 1 (GPIO21/22): AHT10 at 0x38, ADS1115 at 0x48

Use I2C scanner to verify:
- Initialize Wire on GPIO21/22 for external sensors
- Initialize separate Wire instance on GPIO4/15 for OLED

Level Shifter Voltage Verification:
- Measure voltage at ADS1115 SDA/SCL pins: Should be ~5V (idle high)
- Measure voltage at AHT10 SDA/SCL pins: Should be ~3.3V (idle high)
- Measure voltage at Heltec GPIO21/22: Should be ~3.3V (idle high)
- During communication: Voltages should swing between high/low properly

Resistor Network Verification:
- 5V → [1kΩ] → Junction → [2kΩ] → GND
- Junction voltage should be: 5V * (2kΩ/(1kΩ+2kΩ)) = 3.33V
```

### GPIO Conflicts to Avoid
```
Reserved for Built-in Components:
GPIO4  - OLED SDA (don't use for other I2C)
GPIO15 - OLED SCL (don't use for other I2C)  
GPIO16 - OLED RST (don't use)

Reserved for LoRa (if using):
GPIO5, GPIO14, GPIO18, GPIO19, GPIO26, GPIO27

Available for Sensors:
GPIO21, GPIO22 (external I2C)
GPIO23 (DHT11)
GPIO13, GPIO17, GPIO25, GPIO32, GPIO33 (additional sensors)
```