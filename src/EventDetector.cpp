#include "EventDetector.h"

EventDetector::EventDetector(DataCollector* collector) {
    dataCollector = collector;
    
    highCurrentThreshold = 7.2;
    lowPressureThreshold = 5.0;
    lowTemperatureThreshold = 38.0;
    
    pressureHysteresis = 2.0;
    currentHysteresis = 1.0;
    temperatureHysteresis = 2.0;
    
    currentEventTime = 0;
    pressureEventTime = 0;
    temperatureEventTime = 0;
    
    currentEventDuration = 0;
    pressureEventDuration = 0;
    temperatureEventDuration = 0;
    
    eventCount = 0;
    
    highCurrentActive = false;
    lowPressureActive = false;
    lowTemperatureActive = false;
    sensorErrorActive = false;
    
    memset(currentEvents, 0, sizeof(currentEvents));
}

void EventDetector::begin() {
    Serial.println("EventDetector initialized");
}

void EventDetector::update() {
    if (!dataCollector || !dataCollector->isRunning()) {
        return;
    }
    
    SensorData data;
    if (!dataCollector->getCurrentData(data)) {
        return;
    }
    
    checkHighCurrent(data);
    checkLowPressure(data);
    checkLowTemperature(data);
    checkSensorHealth(data);
}

void EventDetector::setThresholds(float highCurrent, float lowPressure, float lowTemp) {
    highCurrentThreshold = highCurrent;
    lowPressureThreshold = lowPressure;
    lowTemperatureThreshold = lowTemp;
}

void EventDetector::setHysteresis(float pressureHyst, float currentHyst, float tempHyst) {
    pressureHysteresis = pressureHyst;
    currentHysteresis = currentHyst;
    temperatureHysteresis = tempHyst;
}

bool EventDetector::hasActiveEvents() const {
    for (uint8_t i = 0; i < eventCount; i++) {
        if (currentEvents[i].active) {
            return true;
        }
    }
    return false;
}

Event EventDetector::getEvent(uint8_t index) const {
    if (index < eventCount) {
        return currentEvents[index];
    }
    Event empty;
    memset(&empty, 0, sizeof(empty));
    return empty;
}

String EventDetector::getStatusString() const {
    if (!hasActiveEvents()) {
        return "Normal";
    }
    
    String status = "ALERT: ";
    bool first = true;
    
    if (highCurrentActive) {
        if (!first) status += ", ";
        status += "High Current";
        first = false;
    }
    
    if (lowPressureActive) {
        if (!first) status += ", ";
        status += "Low Pressure";
        first = false;
    }
    
    if (lowTemperatureActive) {
        if (!first) status += ", ";
        status += "Low Temperature";
        first = false;
    }
    
    if (sensorErrorActive) {
        if (!first) status += ", ";
        status += "Sensor Error";
        first = false;
    }
    
    return status;
}

String EventDetector::getEventSummary() const {
    String summary = "Events: ";
    summary += eventCount;
    summary += " active";
    
    if (eventCount > 0) {
        summary += " (";
        for (uint8_t i = 0; i < eventCount; i++) {
            if (i > 0) summary += ", ";
            summary += eventTypeToString(currentEvents[i].type);
        }
        summary += ")";
    }
    
    return summary;
}

void EventDetector::checkHighCurrent(const SensorData& data) {
    unsigned long now = millis();
    
    float maxCurrent = max(data.current1, data.current2);
    bool currentHigh = maxCurrent > highCurrentThreshold;
    
    if (currentHigh && !highCurrentActive) {
        if (currentEventTime == 0) {
            currentEventTime = now;
        } else if (now - currentEventTime >= CURRENT_EVENT_DELAY) {
            highCurrentActive = true;
            currentEventDuration = now - currentEventTime;
            addEvent(EVENT_HIGH_CURRENT, maxCurrent, highCurrentThreshold, 
                    "High current detected on pump motor");
            Serial.printf("HIGH CURRENT EVENT: %.2fA (threshold: %.2fA)\n", 
                         maxCurrent, highCurrentThreshold);
        }
    } else if (!currentHigh && highCurrentActive) {
        if (maxCurrent < (highCurrentThreshold - currentHysteresis)) {
            highCurrentActive = false;
            currentEventTime = 0;
            clearEvent(EVENT_HIGH_CURRENT);
            Serial.println("High current event cleared");
        }
    } else if (!currentHigh) {
        currentEventTime = 0;
    }
    
    if (highCurrentActive) {
        updateEvent(EVENT_HIGH_CURRENT, maxCurrent, now - currentEventTime);
    }
}

void EventDetector::checkLowPressure(const SensorData& data) {
    unsigned long now = millis();
    
    bool pressureLow = data.pressure < lowPressureThreshold;
    
    if (pressureLow && !lowPressureActive) {
        if (pressureEventTime == 0) {
            pressureEventTime = now;
        } else if (now - pressureEventTime >= PRESSURE_EVENT_DELAY) {
            lowPressureActive = true;
            pressureEventDuration = now - pressureEventTime;
            addEvent(EVENT_LOW_PRESSURE, data.pressure, lowPressureThreshold, 
                    "Low pressure detected in system");
            Serial.printf("LOW PRESSURE EVENT: %.1f PSI (threshold: %.1f PSI)\n", 
                         data.pressure, lowPressureThreshold);
        }
    } else if (!pressureLow && lowPressureActive) {
        if (data.pressure > (lowPressureThreshold + pressureHysteresis)) {
            lowPressureActive = false;
            pressureEventTime = 0;
            clearEvent(EVENT_LOW_PRESSURE);
            Serial.println("Low pressure event cleared");
        }
    } else if (!pressureLow) {
        pressureEventTime = 0;
    }
    
    if (lowPressureActive) {
        updateEvent(EVENT_LOW_PRESSURE, data.pressure, now - pressureEventTime);
    }
}

void EventDetector::checkLowTemperature(const SensorData& data) {
    unsigned long now = millis();
    
    bool temperatureLow = data.temperature < lowTemperatureThreshold;
    
    if (temperatureLow && !lowTemperatureActive) {
        if (temperatureEventTime == 0) {
            temperatureEventTime = now;
        } else if (now - temperatureEventTime >= TEMPERATURE_EVENT_DELAY) {
            lowTemperatureActive = true;
            temperatureEventDuration = now - temperatureEventTime;
            addEvent(EVENT_LOW_TEMPERATURE, data.temperature, lowTemperatureThreshold, 
                    "Low temperature detected in pump house");
            Serial.printf("LOW TEMPERATURE EVENT: %.1f°F (threshold: %.1f°F)\n", 
                         data.temperature, lowTemperatureThreshold);
        }
    } else if (!temperatureLow && lowTemperatureActive) {
        if (data.temperature > (lowTemperatureThreshold + temperatureHysteresis)) {
            lowTemperatureActive = false;
            temperatureEventTime = 0;
            clearEvent(EVENT_LOW_TEMPERATURE);
            Serial.println("Low temperature event cleared");
        }
    } else if (!temperatureLow) {
        temperatureEventTime = 0;
    }
    
    if (lowTemperatureActive) {
        updateEvent(EVENT_LOW_TEMPERATURE, data.temperature, now - temperatureEventTime);
    }
}

void EventDetector::checkSensorHealth(const SensorData& data) {
    bool sensorError = !data.valid;
    
    if (sensorError && !sensorErrorActive) {
        sensorErrorActive = true;
        addEvent(EVENT_SENSOR_ERROR, 0, 0, "Sensor communication error detected");
        Serial.println("SENSOR ERROR EVENT");
    } else if (!sensorError && sensorErrorActive) {
        sensorErrorActive = false;
        clearEvent(EVENT_SENSOR_ERROR);
        Serial.println("Sensor error cleared");
    }
}

void EventDetector::addEvent(EventType type, float value, float threshold, const String& description) {
    if (eventCount >= 10) {
        removeEvent(0);
    }
    
    Event& event = currentEvents[eventCount];
    event.type = type;
    event.value = value;
    event.threshold = threshold;
    event.startTime = millis();
    event.duration = 0;
    event.active = true;
    event.description = description;
    
    eventCount++;
}

void EventDetector::clearEvent(EventType type) {
    int index = findEventIndex(type);
    if (index >= 0) {
        removeEvent(index);
    }
}

void EventDetector::updateEvent(EventType type, float value, unsigned long duration) {
    int index = findEventIndex(type);
    if (index >= 0) {
        currentEvents[index].value = value;
        currentEvents[index].duration = duration;
    }
}

int EventDetector::findEventIndex(EventType type) const {
    for (uint8_t i = 0; i < eventCount; i++) {
        if (currentEvents[i].type == type) {
            return i;
        }
    }
    return -1;
}

void EventDetector::removeEvent(uint8_t index) {
    if (index >= eventCount) return;
    
    for (uint8_t i = index; i < eventCount - 1; i++) {
        currentEvents[i] = currentEvents[i + 1];
    }
    
    eventCount--;
    memset(&currentEvents[eventCount], 0, sizeof(Event));
}

String EventDetector::eventTypeToString(EventType type) const {
    switch (type) {
        case EVENT_HIGH_CURRENT: return "High Current";
        case EVENT_LOW_PRESSURE: return "Low Pressure";
        case EVENT_LOW_TEMPERATURE: return "Low Temperature";
        case EVENT_SENSOR_ERROR: return "Sensor Error";
        case EVENT_SYSTEM_ERROR: return "System Error";
        default: return "Unknown";
    }
}