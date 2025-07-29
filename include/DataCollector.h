#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include "SensorManager.h"
#include "NoiseFilter.h"

struct SensorData {
    float temperature;
    float humidity;
    float pressure;
    float current1;
    float current2;
    unsigned long timestamp;
    bool valid;
};

struct AggregatedData {
    float tempMin, tempMax, tempAvg;
    float humMin, humMax, humAvg;
    float pressMin, pressMax, pressAvg;
    float current1Min, current1Max, current1Avg, current1RMS;
    float current2Min, current2Max, current2Avg, current2RMS;
    float dutyCycle1, dutyCycle2;
    unsigned long startTime;
    unsigned long endTime;
    uint16_t sampleCount;
};

class DataCollector {
private:
    SensorManager* sensorManager;
    
    NoiseFilter* tempFilter;
    NoiseFilter* humFilter;
    NoiseFilter* pressFilter;
    NoiseFilter* current1Filter;
    NoiseFilter* current2Filter;
    
    QueueHandle_t dataQueue;
    SemaphoreHandle_t dataMutex;
    
    TaskHandle_t collectionTask;
    TaskHandle_t aggregationTask;
    
    SensorData currentData;
    AggregatedData lastAggregated;
    
    bool running;
    unsigned long lastAggregationTime;
    
    static const uint16_t QUEUE_SIZE = 100;
    static const uint16_t FILTER_SIZE = 20;
    static const unsigned long AGGREGATION_INTERVAL = 60000;
    
    float currentThreshold1;
    float currentThreshold2;
    
public:
    DataCollector(SensorManager* sensorMgr);
    ~DataCollector();
    
    bool begin();
    void stop();
    
    bool getCurrentData(SensorData& data);
    bool getAggregatedData(AggregatedData& data);
    
    void setCurrentThresholds(float threshold1, float threshold2);
    
    bool isRunning() const { return running; }
    uint16_t getQueueSize() const;
    
private:
    static void IRAM_ATTR collectionTaskWrapper(void* parameter);
    static void IRAM_ATTR aggregationTaskWrapper(void* parameter);
    
    void collectionTaskFunction();
    void aggregationTaskFunction();
    
    void collectSensorData();
    void aggregateData();
    
    float calculateDutyCycle(NoiseFilter* filter, float threshold);
};