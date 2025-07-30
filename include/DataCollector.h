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
    uint16_t tempSampleCount;
    uint16_t humSampleCount;
    uint16_t pressSampleCount;
    uint16_t current1SampleCount;
    uint16_t current2SampleCount;
    uint16_t sampleCount; // Keep for backward compatibility - will be the minimum count
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
    unsigned long lastQueueProcessTime;
    
    static const uint16_t QUEUE_SIZE = 100;
    static const uint16_t FILTER_SIZE = 30;
    static const unsigned long AGGREGATION_INTERVAL = 60000;  // Aggregate over 60-second windows
    static const unsigned long QUEUE_PROCESS_INTERVAL = 1000;  // Process queue every 1 second
    
    float currentThreshold1;
    float currentThreshold2;
    
public:
    DataCollector(SensorManager* sensorMgr);
    ~DataCollector();
    
    bool begin();
    void stop();
    
    bool getCurrentData(SensorData& data);
    bool getAggregatedData(AggregatedData& data);
    void clearAggregatedData();
    
    void setCurrentThresholds(float threshold1, float threshold2);
    
    bool isRunning() const { return running; }
    uint16_t getQueueSize() const;
    
private:
    static void IRAM_ATTR collectionTaskWrapper(void* parameter);
    static void IRAM_ATTR aggregationTaskWrapper(void* parameter);
    
    void collectionTaskFunction();
    void aggregationTaskFunction();
    
    void collectSensorData();
    void processQueueData();
    void aggregateData();
    
    float calculateDutyCycle(NoiseFilter* filter, float threshold);
};