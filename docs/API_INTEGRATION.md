# API Integration Guide

The ESP32 well-pump-monitor has been updated to send data to a Next.js API instead of MongoDB.

## Changes Made

### 1. New API Client
- Replaced `MongoDBClient` with `WellPumpAPIClient`
- Added support for HTTP and HTTPS requests
- Includes automatic buffering and retry logic
- SSL certificate verification can be disabled for self-signed certificates

### 2. Data Format
The ESP32 now sends data in the exact format expected by your Next.js API:

**Sensor Data (POST /api/sensors):**
```json
{
  "device": "well-pump-monitor",
  "location": "Pump House", 
  "timestamp": "1640995200000",
  "startTime": "1640995140000",
  "endTime": "1640995200000",
  "sampleCount": 60,
  "tempMin": 18.5, "tempMax": 19.2, "tempAvg": 18.8,
  "humMin": 65.0, "humMax": 68.5, "humAvg": 66.7,
  "pressMin": 38.2, "pressMax": 42.1, "pressAvg": 40.3,
  "current1Min": 0.1, "current1Max": 7.8, "current1Avg": 2.3, "current1RMS": 2.8, "dutyCycle1": 0.35,
  "current2Min": 0.0, "current2Max": 0.2, "current2Avg": 0.1, "current2RMS": 0.1, "dutyCycle2": 0.0
}
```

**Events (POST /api/events):**
```json
{
  "device": "well-pump-monitor",
  "location": "Pump House",
  "timestamp": "1640995200000",
  "type": 1,
  "value": 8.5,
  "threshold": 7.2, 
  "startTime": "1640995180000",
  "duration": 20000,
  "active": true,
  "description": "High current detected on pump 1"
}
```

### 3. Configuration
The ESP32 web interface now includes API configuration instead of MongoDB:

- **API Base URL**: Your server URL (e.g., `https://yourdomain.com`)
- **API Key**: Optional authentication key
- **Use HTTPS**: Enable/disable HTTPS
- **Verify SSL Certificate**: Enable/disable certificate verification

## Setup Instructions

### 1. Build and Upload Firmware
```bash
# Navigate to the project directory
cd /Users/scotto/Documents/PlatformIO/Projects/well-pump-monitor

# Build the project
pio run

# Upload to ESP32
pio run --target upload

# Upload filesystem (web interface)
pio run --target uploadfs
```

### 2. Configure WiFi and API
1. Connect to the ESP32's WiFi hotspot: `WellPump-Config` (password: `pumphouse`)
2. Open `http://192.168.4.1` in your browser
3. Go to Settings
4. Configure WiFi with your network credentials
5. Configure API with your server URL

### 3. API Server Configuration
Make sure your Next.js server is running and accessible:

1. **For local testing**: Use `http://your-local-ip:3000`
2. **For production**: Use your domain with SSL: `https://yourdomain.com`
3. **For self-signed certificates**: Disable "Verify SSL Certificate"

## Event Types
The ESP32 sends these event types:
- `1`: High Current (EVENT_HIGH_CURRENT)
- `2`: Low Pressure (EVENT_LOW_PRESSURE) 
- `3`: Low Temperature (EVENT_LOW_TEMPERATURE)
- `4`: Sensor Error (EVENT_SENSOR_ERROR)
- `5`: System Error (EVENT_SYSTEM_ERROR)

## Data Flow
1. ESP32 collects sensor data every second
2. Data is aggregated every 60 seconds
3. Aggregated data is sent to `/api/sensors`
4. Events are sent immediately to `/api/events` when detected
5. Failed requests are buffered and retried automatically

## Troubleshooting

### Connection Issues
- Check that WiFi credentials are correct
- Verify API URL is accessible from ESP32's network
- For HTTPS: Try disabling certificate verification first

### Data Not Appearing
- Check ESP32 serial output for error messages
- Verify API endpoints are working with curl/Postman
- Check ESP32's system status via web interface

### SSL Certificate Issues
- For self-signed certificates, disable "Verify SSL Certificate"
- For Let's Encrypt certificates, enable verification
- Check that certificates are valid and not expired

## Monitoring
- Web interface shows connection status and buffered data count
- Serial output provides detailed logging
- System status API shows current state

The ESP32 will automatically retry failed connections and buffer data during outages.