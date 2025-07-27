#pragma once

#include <Arduino.h>
#include <Adafruit_AHTX0.h>
#include <Adafruit_ADS1X15.h>
#include <Wire.h>

class SensorManager {
private:
    Adafruit_AHTX0 aht;
    Adafruit_ADS1115 ads;
    TwoWire* wireInstance;
    
    float pressureOffset;
    float pressureScale;
    float current1Offset;
    float current1Scale;
    float current2Offset;
    float current2Scale;
    
    bool ahtInitialized;
    bool adsInitialized;
    
    unsigned long lastTempRead;
    unsigned long lastPressureRead;
    unsigned long lastCurrentRead;
    
    float lastTemperature;
    float lastHumidity;
    float lastPressure;
    float lastCurrent1;
    float lastCurrent2;
    
    static const unsigned long TEMP_READ_INTERVAL = 5000;
    static const unsigned long PRESSURE_READ_INTERVAL = 3000;
    static const unsigned long CURRENT_READ_INTERVAL = 1000;
    
public:
    SensorManager(TwoWire* wire = &Wire);
    
    bool begin();
    bool isHealthy();
    
    bool readTemperature(float& temperature);
    bool readHumidity(float& humidity);
    bool readPressure(float& pressure);
    bool readCurrent1(float& current);
    bool readCurrent2(float& current);
    
    void calibratePressure(float zeroPoint, float fullScale);
    void calibrateCurrent1(float zeroPoint, float fullScale);
    void calibrateCurrent2(float zeroPoint, float fullScale);
    
    void setCalibration(float pressOffset, float pressScale, 
                       float curr1Offset, float curr1Scale,
                       float curr2Offset, float curr2Scale);
    
    float getTemperature() const { return lastTemperature; }
    float getHumidity() const { return lastHumidity; }
    float getPressure() const { return lastPressure; }
    float getCurrent1() const { return lastCurrent1; }
    float getCurrent2() const { return lastCurrent2; }
    
    bool isAHTHealthy() const { return ahtInitialized; }
    bool isADSHealthy() const { return adsInitialized; }
    
private:
    float readADSChannel(uint8_t channel);
    bool validateTemperature(float temp);
    bool validateHumidity(float hum);
    bool validatePressure(float pressure);
    bool validateCurrent(float current);
};