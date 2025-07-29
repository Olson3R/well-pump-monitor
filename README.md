# Well Pump Monitor

A comprehensive IoT monitoring system for well pumps using ESP32 microcontroller. Monitors temperature, humidity, water pressure, and pump current consumption with web interface, event detection, and API integration.

## Features

- **Real-time Monitoring**: Temperature, humidity, water pressure, and dual current sensors
- **Web Interface**: Built-in web server with real-time data display
- **Event Detection**: Configurable alerts for high current, low pressure, and low temperature
- **Data Logging**: Automatic data aggregation and API integration
- **Sensor Calibration**: Easy web-based calibration interface
- **WiFi Connectivity**: Station mode with AP fallback for configuration
- **OTA Updates**: Over-the-air firmware updates
- **OLED Display**: Local status display on built-in screen

## Hardware Requirements

### Main Board
- **Heltec WiFi LoRa 32 V2** - ESP32-based board with built-in OLED display

### Sensors
- **AHT10** - Temperature and humidity sensor (I2C)
- **ADS1115** - 16-bit ADC for analog sensors (I2C)
- **Pressure Sensor** - 4-20mA pressure transducer (connected to ADS1115 A2)
- **Current Sensors** - Hall effect current sensors (connected to ADS1115 A0, A1)

### Pin Connections
```
I2C Sensors Bus (External):
- SDA: GPIO 21
- SCL: GPIO 22
- AHT10: Address 0x38
- ADS1115: Address 0x48

OLED Display (Built-in):
- SDA: GPIO 4
- SCL: GPIO 15
- Address: 0x3C

ADC Channel Assignment:
- A0: Current Sensor 1
- A1: Current Sensor 2  
- A2: Pressure Sensor
```

## Software Architecture

### Core Components
- **SensorManager**: Handles all sensor readings and calibration
- **DataCollector**: Collects and filters sensor data using FreeRTOS tasks
- **EventDetector**: Monitors thresholds and generates alerts
- **APIClient**: Sends data to external APIs
- **NoiseFilter**: Digital filtering for stable sensor readings

### Data Flow
1. **Collection Task** (1Hz): Reads sensors, applies filtering
2. **Aggregation Task** (1/60Hz): Calculates statistics over 1-minute intervals
3. **Event Detection**: Monitors for threshold violations with hysteresis
4. **API Logging**: Sends aggregated data and events to configured endpoint

## Installation

### Prerequisites
- [PlatformIO](https://platformio.org/) installed
- ESP32 development environment set up

### Build and Upload
```bash
# Clone the repository
git clone <repository-url>
cd well-pump-monitor

# Build the project
pio run

# Upload firmware
pio run -t upload

# Upload web files to SPIFFS
pio run -t uploadfs

# Monitor serial output
pio device monitor
```

## Configuration

### Initial Setup
1. Power on the device
2. Connect to WiFi network "WellPump-Config" (password: "pumphouse")
3. Navigate to http://192.168.4.1
4. Configure WiFi credentials and API settings

### Sensor Calibration
1. Access the web interface at `http://device-ip/calibrate.html`
2. For each sensor:
   - **Zero Calibration**: With no load/pressure applied, click "Set Zero"
   - **Single-Point Calibration**: Apply known value, enter it, click "Calibrate"

#### Calibration Examples
```
Pressure Sensor:
1. With no pressure: Click "Set Zero"
2. Apply 50 PSI: Enter "50", click "Calibrate"

Current Sensors:
1. With pump off: Click "Set Zero" 
2. With known 15A load: Enter "15", click "Calibrate"
```

## Web Interface

### Main Dashboard
- Real-time sensor readings
- System status indicators
- WiFi connection status
- Active alerts display

### API Endpoints
- `GET /api/sensors` - Current sensor readings
- `GET /api/aggregated` - Aggregated data over time
- `GET /api/events` - Active events and alerts
- `GET /api/status` - System health status
- `GET /api/calibrate` - Raw sensor voltages
- `POST /api/calibrate` - Calibrate sensors

### Configuration Pages
- `/config/wifi` - WiFi settings
- `/config/api` - API integration settings
- `/calibrate.html` - Sensor calibration interface

## Event Detection

### Configurable Thresholds
- **High Current**: Pump overcurrent detection
- **Low Pressure**: Water pressure drop alerts  
- **Low Temperature**: Freeze protection
- **Sensor Errors**: Communication failures

### Hysteresis
Prevents false alarms with configurable hysteresis values for each threshold type.

## API Integration

Supports sending data to external APIs (REST endpoints) with configurable:
- Base URL and API key
- HTTPS with certificate verification options
- Automatic retry on failures
- JSON payload format

## Troubleshooting

### Common Issues

**Sensors showing static readings:**
- Check ADC channel connections (A0, A1, A2)
- Verify I2C wiring and addresses
- Use calibration interface to check raw voltages

**WiFi connection issues:**
- Device creates AP "WellPump-Config" on connection failure
- Check credentials in configuration interface
- Monitor serial output for connection status

**Guru Meditation Errors:**
- Usually indicates stack overflow or memory corruption
- Check sensor initialization and FreeRTOS task stack sizes

### Serial Debug Output
Connect at 115200 baud to see:
- Sensor initialization status
- Raw ADC readings and calibrated values
- WiFi connection progress
- Event detection triggers
- API communication status

## File Structure
```
well-pump-monitor/
├── src/
│   ├── main.cpp              # Main application
│   ├── SensorManager.cpp     # Sensor interface
│   ├── DataCollector.cpp     # Data collection tasks
│   ├── EventDetector.cpp     # Alert system
│   ├── APIClient.cpp         # External API integration
│   └── NoiseFilter.cpp       # Digital filtering
├── include/                  # Header files
├── data/                    # Web interface files
│   ├── index.html           # Main dashboard
│   ├── calibrate.html       # Calibration interface
│   └── *.css, *.js          # Styling and scripts
├── docs/                    # Documentation
└── platformio.ini          # Build configuration
```

## Technical Specifications

### Performance
- **Sampling Rate**: 1Hz sensor readings
- **Data Aggregation**: 1-minute intervals
- **Memory Usage**: ~16% RAM, ~36% Flash
- **Power Consumption**: ~200mA @ 3.3V (WiFi active)

### Sensor Specifications
- **Temperature**: ±0.3°C accuracy, -40°C to +85°C range
- **Humidity**: ±2% RH accuracy, 0-100% RH range  
- **Pressure**: 0-150 PSI range (configurable)
- **Current**: ±50A range (Hall effect sensors)

### Communication
- **WiFi**: 802.11 b/g/n, 2.4GHz
- **Web Server**: Async HTTP server on port 80
- **API**: RESTful JSON endpoints
- **OTA**: HTTP-based firmware updates

## License

This project is licensed under the MIT License - see the LICENSE file for details.

## Contributing

1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Add tests if applicable
5. Submit a pull request

## Support

For issues and questions:
- Check the troubleshooting section
- Review serial debug output
- Create an issue with detailed information including:
  - Hardware configuration
  - Serial output logs
  - Steps to reproduce