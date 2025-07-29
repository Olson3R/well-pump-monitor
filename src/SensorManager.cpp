#include "SensorManager.h"

SensorManager::SensorManager(TwoWire* wire) 
    : aht(), ads(), wireInstance(wire)
{
    // Pressure sensor calibration: 0.5V-4.5V = 0-100 PSI (typical 4-20mA pressure transducer)
    // Connected to ADC channel A2
    pressureOffset = 0.5;  // 0.5V = 0 PSI
    pressureScale = 25.0;  // (100 PSI / 4.0V) = 25 PSI/V
    
    // Current sensor calibration: Assuming 2.5V = 0A, 30A/V sensitivity (typical Hall effect sensor)
    // Current sensor 1 connected to ADC channel A0
    // Current sensor 2 connected to ADC channel A1
    current1Offset = 2.5;  // 2.5V = 0A (center bias)
    current1Scale = 30.0;  // 30A per volt
    current2Offset = 2.5;  // 2.5V = 0A (center bias)
    current2Scale = 30.0;  // 30A per volt
    
    ahtInitialized = false;
    adsInitialized = false;
    
    lastTempRead = 0;
    lastPressureRead = 0;
    lastCurrentRead = 0;
    
    lastTemperature = 0.0;
    lastHumidity = 0.0;
    lastPressure = 0.0;
    lastCurrent1 = 0.0;
    lastCurrent2 = 0.0;
}

bool SensorManager::begin() {
    // Initialize AHT10 on the external I2C bus
    if (!aht.begin(wireInstance)) {
        Serial.println("Failed to initialize AHT10");
        ahtInitialized = false;
    } else {
        ahtInitialized = true;
        Serial.println("AHT10 initialized successfully");
    }
    
    if (!ads.begin(0x48, wireInstance)) {
        Serial.println("Failed to initialize ADS1115");
        adsInitialized = false;
        return false;
    }
    
    ads.setGain(GAIN_TWOTHIRDS);
    adsInitialized = true;
    
    Serial.print("AHT initialized: ");
    Serial.println(ahtInitialized ? "YES" : "NO");
    Serial.print("ADS initialized: ");
    Serial.println(adsInitialized ? "YES" : "NO");
    
    return ahtInitialized && adsInitialized;
}

bool SensorManager::isHealthy() {
    return ahtInitialized && adsInitialized;
}

bool SensorManager::readTemperature(float& temperature) {
    unsigned long now = millis();
    if (now - lastTempRead < TEMP_READ_INTERVAL) {
        temperature = lastTemperature;
        return true;
    }
    
    if (!ahtInitialized) return false;
    
    sensors_event_t humidity_event, temp_event;
    if (!aht.getEvent(&humidity_event, &temp_event)) {
        return false;
    }
    
    float temp = temp_event.temperature * 9.0 / 5.0 + 32.0; // Convert to Fahrenheit
    if (!validateTemperature(temp)) {
        return false;
    }
    
    lastTemperature = temp;
    lastHumidity = humidity_event.relative_humidity; // Update humidity at same time
    lastTempRead = now;
    temperature = temp;
    return true;
}

bool SensorManager::readHumidity(float& humidity) {
    unsigned long now = millis();
    if (now - lastTempRead < TEMP_READ_INTERVAL) {
        humidity = lastHumidity;
        return true;
    }
    
    if (!ahtInitialized) return false;
    
    // Read both temperature and humidity together for efficiency
    sensors_event_t humidity_event, temp_event;
    if (!aht.getEvent(&humidity_event, &temp_event)) {
        return false;
    }
    
    float hum = humidity_event.relative_humidity;
    if (!validateHumidity(hum)) {
        return false;
    }
    
    lastHumidity = hum;
    lastTemperature = temp_event.temperature * 9.0 / 5.0 + 32.0; // Update temp at same time
    lastTempRead = now;
    humidity = hum;
    return true;
}

bool SensorManager::readPressure(float& pressure) {
    unsigned long now = millis();
    if (now - lastPressureRead < PRESSURE_READ_INTERVAL) {
        pressure = lastPressure;
        Serial.printf("Pressure: Using cached value %.1f PSI\n", pressure);
        return true;
    }
    
    if (!adsInitialized) return false;
    
    float rawValue = readADSChannel(2);  // Pressure sensor connected to A2
    if (rawValue < 0) return false;
    
    float pressureValue = (rawValue - pressureOffset) * pressureScale;
    
    // Debug output for pressure calibration
    Serial.printf("Pressure: Raw=%.3fV, Calculated=%.1fPSI (offset=%.1f, scale=%.1f)\n", 
                  rawValue, pressureValue, pressureOffset, pressureScale);
    
    if (!validatePressure(pressureValue)) {
        Serial.printf("Pressure validation failed: %.1f PSI (range: 0-150)\n", pressureValue);
        return false;
    }
    
    lastPressure = pressureValue;
    lastPressureRead = now;
    pressure = pressureValue;
    return true;
}

bool SensorManager::readCurrent1(float& current) {
    unsigned long now = millis();
    if (now - lastCurrentRead < CURRENT_READ_INTERVAL) {
        current = lastCurrent1;
        Serial.printf("Current1: Using cached value %.2f A\n", current);
        return true;
    }
    
    if (!adsInitialized) return false;
    
    float rawValue = readADSChannel(0);  // Current sensor 1 connected to A0
    if (rawValue < 0) return false;
    
    float currentValue = (rawValue - current1Offset) * current1Scale;
    
    // Debug output for current1 calibration
    Serial.printf("Current1: Raw=%.3fV, Calculated=%.2fA (offset=%.1f, scale=%.1f)\n", 
                  rawValue, currentValue, current1Offset, current1Scale);
    
    if (!validateCurrent(currentValue)) {
        Serial.printf("Current1 validation failed: %.2f A (range: 0-50)\n", currentValue);
        return false;
    }
    
    lastCurrent1 = currentValue;
    lastCurrentRead = now;
    current = currentValue;
    return true;
}

bool SensorManager::readCurrent2(float& current) {
    if (!adsInitialized) return false;
    
    float rawValue = readADSChannel(1);  // Current sensor 2 connected to A1
    if (rawValue < 0) return false;
    
    float currentValue = (rawValue - current2Offset) * current2Scale;
    
    // Debug output for current2 calibration
    Serial.printf("Current2: Raw=%.3fV, Calculated=%.2fA (offset=%.1f, scale=%.1f)\n", 
                  rawValue, currentValue, current2Offset, current2Scale);
    
    if (!validateCurrent(currentValue)) {
        Serial.printf("Current2 validation failed: %.2f A (range: 0-50)\n", currentValue);
        return false;
    }
    
    lastCurrent2 = currentValue;
    current = currentValue;
    return true;
}

void SensorManager::calibratePressure(float zeroPoint, float fullScale) {
    pressureOffset = zeroPoint;
    pressureScale = fullScale / (6.144 - zeroPoint);
}

void SensorManager::calibrateCurrent1(float zeroPoint, float fullScale) {
    current1Offset = zeroPoint;
    current1Scale = fullScale / (6.144 - zeroPoint);
}

void SensorManager::calibrateCurrent2(float zeroPoint, float fullScale) {
    current2Offset = zeroPoint;
    current2Scale = fullScale / (6.144 - zeroPoint);
}

void SensorManager::setCalibration(float pressOffset, float pressScale, 
                                  float curr1Offset, float curr1Scale,
                                  float curr2Offset, float curr2Scale) {
    pressureOffset = pressOffset;
    pressureScale = pressScale;
    current1Offset = curr1Offset;
    current1Scale = curr1Scale;
    current2Offset = curr2Offset;
    current2Scale = curr2Scale;
}

float SensorManager::readADSChannel(uint8_t channel) {
    if (!adsInitialized) return -1.0;
    
    int16_t rawValue = 0;
    switch(channel) {
        case 0: rawValue = ads.readADC_SingleEnded(0); break;
        case 1: rawValue = ads.readADC_SingleEnded(1); break;
        case 2: rawValue = ads.readADC_SingleEnded(2); break;
        case 3: rawValue = ads.readADC_SingleEnded(3); break;
        default: return -1.0;
    }
    
    float voltage = ads.computeVolts(rawValue);
    
    // Debug: Show raw ADC values and computed voltage
    Serial.printf("ADC Ch%d: Raw=%d, Voltage=%.3fV\n", channel, rawValue, voltage);
    
    return voltage;
}

bool SensorManager::validateTemperature(float temp) {
    return (temp >= -40.0 && temp <= 150.0);
}

bool SensorManager::validateHumidity(float hum) {
    return (hum >= 0.0 && hum <= 100.0);
}

bool SensorManager::validatePressure(float pressure) {
    return (pressure >= 0.0 && pressure <= 150.0);
}

bool SensorManager::validateCurrent(float current) {
    // Allow negative values for bidirectional current sensors (Hall effect)
    return (current >= -50.0 && current <= 50.0);
}

// Single-point calibration functions
void SensorManager::calibratePressureAtValue(float knownPressure) {
    float rawVoltage = getRawPressureVoltage();
    if (rawVoltage > 0) {
        if (knownPressure == 0.0) {
            // Zero calibration - set offset
            pressureOffset = rawVoltage;
            Serial.printf("Pressure zero calibrated: offset=%.3fV\n", pressureOffset);
        } else {
            // Single-point calibration - calculate scale factor
            pressureScale = knownPressure / (rawVoltage - pressureOffset);
            Serial.printf("Pressure calibrated: %.1f PSI at %.3fV, scale=%.1f\n", 
                         knownPressure, rawVoltage, pressureScale);
        }
    }
}

void SensorManager::calibrateCurrent1AtValue(float knownCurrent) {
    float rawVoltage = getRawCurrent1Voltage();
    if (rawVoltage > 0) {
        if (knownCurrent == 0.0) {
            // Zero calibration - set offset
            current1Offset = rawVoltage;
            Serial.printf("Current1 zero calibrated: offset=%.3fV\n", current1Offset);
        } else {
            // Single-point calibration - calculate scale factor
            current1Scale = knownCurrent / (rawVoltage - current1Offset);
            Serial.printf("Current1 calibrated: %.2f A at %.3fV, scale=%.1f\n", 
                         knownCurrent, rawVoltage, current1Scale);
        }
    }
}

void SensorManager::calibrateCurrent2AtValue(float knownCurrent) {
    float rawVoltage = getRawCurrent2Voltage();
    if (rawVoltage > 0) {
        if (knownCurrent == 0.0) {
            // Zero calibration - set offset
            current2Offset = rawVoltage;
            Serial.printf("Current2 zero calibrated: offset=%.3fV\n", current2Offset);
        } else {
            // Single-point calibration - calculate scale factor
            current2Scale = knownCurrent / (rawVoltage - current2Offset);
            Serial.printf("Current2 calibrated: %.2f A at %.3fV, scale=%.2f\n", 
                         knownCurrent, rawVoltage, current2Scale);
        }
    }
}

// Get raw voltage readings for calibration
float SensorManager::getRawPressureVoltage() {
    return readADSChannel(2);
}

float SensorManager::getRawCurrent1Voltage() {
    return readADSChannel(0);
}

float SensorManager::getRawCurrent2Voltage() {
    return readADSChannel(1);
}