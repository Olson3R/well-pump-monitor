#include "SensorManager.h"

SensorManager::SensorManager(TwoWire* wire) 
    : aht(), ads(), wireInstance(wire)
{
    pressureOffset = 0.0;
    pressureScale = 1.0;
    current1Offset = 0.0;
    current1Scale = 1.0;
    current2Offset = 0.0;
    current2Scale = 1.0;
    
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
        return true;
    }
    
    if (!adsInitialized) return false;
    
    float rawValue = readADSChannel(0);
    if (rawValue < 0) return false;
    
    float pressureValue = (rawValue - pressureOffset) * pressureScale;
    if (!validatePressure(pressureValue)) {
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
        return true;
    }
    
    if (!adsInitialized) return false;
    
    float rawValue = readADSChannel(1);
    if (rawValue < 0) return false;
    
    float currentValue = (rawValue - current1Offset) * current1Scale;
    if (!validateCurrent(currentValue)) {
        return false;
    }
    
    lastCurrent1 = currentValue;
    lastCurrentRead = now;
    current = currentValue;
    return true;
}

bool SensorManager::readCurrent2(float& current) {
    if (!adsInitialized) return false;
    
    float rawValue = readADSChannel(2);
    if (rawValue < 0) return false;
    
    float currentValue = (rawValue - current2Offset) * current2Scale;
    if (!validateCurrent(currentValue)) {
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
    
    return ads.computeVolts(rawValue);
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
    return (current >= 0.0 && current <= 50.0);
}