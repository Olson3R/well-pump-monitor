#pragma once

#include <Arduino.h>
#include "DataCollector.h"

enum EventType {
    EVENT_NONE = 0,
    EVENT_HIGH_CURRENT = 1,
    EVENT_LOW_PRESSURE = 2,
    EVENT_LOW_TEMPERATURE = 3,
    EVENT_SENSOR_ERROR = 4,
    EVENT_SYSTEM_ERROR = 5
};

struct Event {
    EventType type;
    float value;
    float threshold;
    unsigned long startTime;
    unsigned long duration;
    bool active;
    String description;
};

class EventDetector {
private:
    DataCollector* dataCollector;
    
    float highCurrentThreshold;
    float lowPressureThreshold;
    float lowTemperatureThreshold;
    
    float pressureHysteresis;
    float currentHysteresis;
    float temperatureHysteresis;
    
    unsigned long currentEventTime;
    unsigned long pressureEventTime;
    unsigned long temperatureEventTime;
    
    unsigned long currentEventDuration;
    unsigned long pressureEventDuration;
    unsigned long temperatureEventDuration;
    
    Event currentEvents[10];
    uint8_t eventCount;
    
    bool highCurrentActive;
    bool lowPressureActive;
    bool lowTemperatureActive;
    bool sensorErrorActive;
    
    static const unsigned long CURRENT_EVENT_DELAY = 3000;
    static const unsigned long PRESSURE_EVENT_DELAY = 10000;
    static const unsigned long TEMPERATURE_EVENT_DELAY = 10000;
    
public:
    EventDetector(DataCollector* collector);
    
    void begin();
    void update();
    
    void setThresholds(float highCurrent, float lowPressure, float lowTemp);
    void setHysteresis(float pressureHyst, float currentHyst, float tempHyst);
    
    bool hasActiveEvents() const;
    uint8_t getEventCount() const { return eventCount; }
    Event getEvent(uint8_t index) const;
    
    bool isHighCurrentActive() const { return highCurrentActive; }
    bool isLowPressureActive() const { return lowPressureActive; }
    bool isLowTemperatureActive() const { return lowTemperatureActive; }
    bool isSensorErrorActive() const { return sensorErrorActive; }
    
    String getStatusString() const;
    String getEventSummary() const;
    
private:
    void checkHighCurrent(const SensorData& data);
    void checkLowPressure(const SensorData& data);
    void checkLowTemperature(const SensorData& data);
    void checkSensorHealth(const SensorData& data);
    
    void addEvent(EventType type, float value, float threshold, const String& description);
    void clearEvent(EventType type);
    void updateEvent(EventType type, float value, unsigned long duration);
    
    int findEventIndex(EventType type) const;
    void removeEvent(uint8_t index);
    
    String eventTypeToString(EventType type) const;
};