#include "DataCollector.h"

DataCollector::DataCollector(SensorManager* sensorMgr) {
    sensorManager = sensorMgr;
    
    tempFilter = new NoiseFilter(FILTER_SIZE, 20.0, 0.1);  // Much more lenient for temperature
    humFilter = new NoiseFilter(FILTER_SIZE, 20.0, 0.1);  // Much more lenient for humidity
    pressFilter = new NoiseFilter(FILTER_SIZE, 2.0, 0.1);
    current1Filter = new NoiseFilter(FILTER_SIZE, 1.5, 0.2);
    current2Filter = new NoiseFilter(FILTER_SIZE, 1.5, 0.2);
    
    dataQueue = NULL;
    dataMutex = NULL;
    collectionTask = NULL;
    aggregationTask = NULL;
    
    running = false;
    lastAggregationTime = 0;
    lastQueueProcessTime = 0;
    
    currentThreshold1 = 0.5;
    currentThreshold2 = 0.5;
    
    memset(&currentData, 0, sizeof(currentData));
    memset(&lastAggregated, 0, sizeof(lastAggregated));
}

DataCollector::~DataCollector() {
    stop();
    
    delete tempFilter;
    delete humFilter;
    delete pressFilter;
    delete current1Filter;
    delete current2Filter;
}

bool DataCollector::begin() {
    if (running) return true;
    
    // Validate that object is properly initialized
    if (!sensorManager) {
        Serial.println("ERROR: SensorManager is null in DataCollector::begin()");
        return false;
    }
    
    if (!tempFilter || !humFilter || !pressFilter || !current1Filter || !current2Filter) {
        Serial.println("ERROR: One or more filters is null in DataCollector::begin()");
        return false;
    }
    
    Serial.println("Creating data queue...");
    dataQueue = xQueueCreate(QUEUE_SIZE, sizeof(SensorData));
    if (dataQueue == NULL) {
        Serial.println("Failed to create data queue");
        return false;
    }
    
    Serial.println("Creating data mutex...");
    dataMutex = xSemaphoreCreateMutex();
    if (dataMutex == NULL) {
        Serial.println("Failed to create data mutex");
        vQueueDelete(dataQueue);
        return false;
    }
    
    Serial.println("Creating collection task...");
    BaseType_t result1 = xTaskCreate(
        collectionTaskWrapper,
        "DataCollection",
        8192,
        this,
        1,  // Lower priority to give more CPU to aggregation and main loop
        &collectionTask
    );
    
    Serial.println("Creating aggregation task...");
    BaseType_t result2 = xTaskCreate(
        aggregationTaskWrapper,
        "DataAggregation",
        8192,
        this,
        3,  // High priority to ensure data aggregation happens
        &aggregationTask
    );
    
    if (result1 != pdPASS || result2 != pdPASS) {
        Serial.printf("Failed to create tasks: result1=%d, result2=%d\n", result1, result2);
        stop();
        return false;
    }
    
    running = true;
    lastAggregationTime = millis();
    lastQueueProcessTime = millis();
    
    Serial.println("DataCollector started successfully");
    return true;
}

void DataCollector::stop() {
    if (!running) return;
    
    running = false;
    
    if (collectionTask != NULL) {
        vTaskDelete(collectionTask);
        collectionTask = NULL;
    }
    
    if (aggregationTask != NULL) {
        vTaskDelete(aggregationTask);
        aggregationTask = NULL;
    }
    
    if (dataQueue != NULL) {
        vQueueDelete(dataQueue);
        dataQueue = NULL;
    }
    
    if (dataMutex != NULL) {
        vSemaphoreDelete(dataMutex);
        dataMutex = NULL;
    }
    
    Serial.println("DataCollector stopped");
}

bool DataCollector::getCurrentData(SensorData& data) {
    if (!running) return false;
    
    if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        data = currentData;
        xSemaphoreGive(dataMutex);
        return data.valid;
    }
    
    return false;
}

bool DataCollector::getAggregatedData(AggregatedData& data) {
    if (!running) return false;
    
    if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        data = lastAggregated;
        bool hasValidData = (data.sampleCount > 0);
        xSemaphoreGive(dataMutex);
        return hasValidData;
    }
    
    return false;
}

void DataCollector::clearAggregatedData() {
    if (!running) return;
    
    if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        memset(&lastAggregated, 0, sizeof(lastAggregated));
        xSemaphoreGive(dataMutex);
        Serial.println("DataCollector: Aggregated data cleared after successful send");
    }
}

void DataCollector::setCurrentThresholds(float threshold1, float threshold2) {
    currentThreshold1 = threshold1;
    currentThreshold2 = threshold2;
}

uint16_t DataCollector::getQueueSize() const {
    if (dataQueue == NULL) return 0;
    return uxQueueMessagesWaiting(dataQueue);
}

void IRAM_ATTR DataCollector::collectionTaskWrapper(void* parameter) {
    if (parameter == nullptr) {
        Serial.println("ERROR: Null parameter in collectionTaskWrapper!");
        vTaskDelete(NULL);
        return;
    }
    
    DataCollector* collector = static_cast<DataCollector*>(parameter);
    if (collector == nullptr) {
        Serial.println("ERROR: Failed to cast parameter!");
        vTaskDelete(NULL);
        return;
    }
    
    // Small delay to ensure everything is initialized
    vTaskDelay(pdMS_TO_TICKS(100));
    
    collector->collectionTaskFunction();
}

void IRAM_ATTR DataCollector::aggregationTaskWrapper(void* parameter) {
    if (parameter == nullptr) {
        Serial.println("ERROR: Null parameter in aggregationTaskWrapper!");
        vTaskDelete(NULL);
        return;
    }
    
    DataCollector* collector = static_cast<DataCollector*>(parameter);
    if (collector == nullptr) {
        Serial.println("ERROR: Failed to cast parameter!");
        vTaskDelete(NULL);
        return;
    }
    
    // Small delay to ensure everything is initialized
    vTaskDelay(pdMS_TO_TICKS(100));
    
    collector->aggregationTaskFunction();
}

void DataCollector::collectionTaskFunction() {
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(2000);  // Collect every 2 seconds to get 30 samples per minute
    
    while (running) {
        collectSensorData();
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}

void DataCollector::aggregationTaskFunction() {
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(2000);  // Run every 2 seconds to process queue regularly
    
    while (running) {
        unsigned long now = millis();
        
        // Process queue every 2 seconds to ensure filters get fed regularly
        processQueueData();  // Always drain queue to feed filters
        
        // Aggregate data every 60 seconds for final statistics
        if (now - lastAggregationTime >= AGGREGATION_INTERVAL) {
            Serial.print("Creating 60-second aggregation... Queue size: ");
            Serial.println(uxQueueMessagesWaiting(dataQueue));
            Serial.print("Filter sample counts - Temp: ");
            Serial.print(tempFilter->getSampleCount());
            Serial.print(", Current1: ");
            Serial.println(current1Filter->getSampleCount());
            
            aggregateData();
            lastAggregationTime = now;  // Use millis() consistently for timing
            Serial.println("60-second aggregation complete");
        }
        
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}

void DataCollector::collectSensorData() {
    if (!sensorManager || !sensorManager->isHealthy()) {
        return;
    }
    
    SensorData data;
    extern unsigned long getCurrentTimestamp();
    unsigned long timestamp = getCurrentTimestamp();
    data.timestamp = (timestamp > 0) ? timestamp : millis();
    data.valid = true;
    
    bool tempOk = sensorManager->readTemperature(data.temperature);
    bool humOk = sensorManager->readHumidity(data.humidity);
    bool pressOk = sensorManager->readPressure(data.pressure);
    bool current1Ok = sensorManager->readCurrent1(data.current1);
    bool current2Ok = sensorManager->readCurrent2(data.current2);
    
    // Don't add to filters here - let processQueueData() handle it
    // This prevents double-adding samples to filters
    
    // Debug temperature sensor issues with values
    if (!tempOk || !humOk) {
        Serial.printf("Temp/Hum read failed - Temp: %s (%.1f), Hum: %s (%.1f)\n", 
                      tempOk ? "OK" : "FAIL", data.temperature,
                      humOk ? "OK" : "FAIL", data.humidity);
    } else {
        Serial.printf("Temp/Hum read OK - Temp: %.1f°F, Hum: %.1f%%\n", 
                      data.temperature, data.humidity);
    }
    
    // Debug validation failures
    if (!tempOk) Serial.println("Temperature validation failed");
    if (!humOk) Serial.println("Humidity validation failed"); 
    if (!pressOk) Serial.println("Pressure validation failed");
    if (!current1Ok) Serial.println("Current1 validation failed");
    if (!current2Ok) Serial.println("Current2 validation failed");
    
    // Allow sample to be valid even if temp/humidity fail occasionally
    // The main issue is that we need current sensors working for aggregation
    data.valid = pressOk && current1Ok && current2Ok;
    
    if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        currentData = data;
        xSemaphoreGive(dataMutex);
    }
    
    // Always queue samples, even if not fully valid - we'll handle validation in processQueueData
    if (dataQueue != NULL) {
        if (xQueueSend(dataQueue, &data, 0) != pdTRUE) {
            Serial.println("Data queue full, dropping sample");
        }
    }
}

void DataCollector::processQueueData() {
    SensorData data;
    int processed = 0;
    
    // Process available samples in batches to reduce CPU overhead
    // With 3-second collection interval, expect 1-2 samples max
    while (xQueueReceive(dataQueue, &data, 0) == pdTRUE && processed < 10) {
        // Always add samples to filters even if data.valid is false
        // This ensures all filters get consistent sample counts
        
        // Add temperature - use reasonable range check
        if (data.temperature >= -40.0 && data.temperature <= 150.0) {
            uint16_t countBefore = tempFilter->getSampleCount();
            tempFilter->addSample(data.temperature);
            uint16_t countAfter = tempFilter->getSampleCount();
            if (countAfter > countBefore) {
                Serial.printf("Added temp sample %.1f°F to filter (count: %d->%d)\n", 
                             data.temperature, countBefore, countAfter);
            } else {
                Serial.printf("Temp sample %.1f°F REJECTED as outlier (count stays: %d)\n", 
                             data.temperature, countAfter);
            }
        } else {
            // Use a reasonable default if temperature is invalid (70°F)
            tempFilter->addSample(70.0);
            Serial.printf("Invalid temp %.1f, using 70°F default (count now: %d)\n", 
                         data.temperature, tempFilter->getSampleCount());
        }
        
        // Add humidity - use reasonable range check  
        if (data.humidity >= 0.0 && data.humidity <= 100.0) {
            humFilter->addSample(data.humidity);
        } else {
            // Use a reasonable default if humidity is invalid (50%)
            humFilter->addSample(50.0);
            Serial.printf("Invalid humidity %.1f, using 50%% default\n", data.humidity);
        }
        
        // Add pressure and current only if data is marked valid (these are critical)
        if (data.valid) {
            pressFilter->addSample(data.pressure);
            current1Filter->addSample(data.current1);
            current2Filter->addSample(data.current2);
        } else {
            Serial.println("Skipping pressure/current samples - data invalid");
        }
        processed++;
        
        // Yield CPU after each sample to allow other tasks to run
        if (processed % 5 == 0) {
            vTaskDelay(pdMS_TO_TICKS(1));  // Brief yield
        }
    }
    
    if (processed > 0) {
        Serial.print("Processed ");
        Serial.print(processed);
        Serial.print(" samples. Queue remaining: ");
        Serial.println(uxQueueMessagesWaiting(dataQueue));
    }
}

void DataCollector::aggregateData() {
    Serial.println("Starting data aggregation with varied sample counts...");
    
    AggregatedData aggregated = {0}; // Initialize all fields to zero
    extern unsigned long getCurrentTimestamp();
    unsigned long currentTime = getCurrentTimestamp();
    
    // Use unix timestamps if available, otherwise use 0 to indicate invalid
    if (currentTime > 1600000000) {
        // We have valid unix timestamps
        unsigned long startUnixTime = currentTime - (AGGREGATION_INTERVAL / 1000);  // 60 seconds ago
        aggregated.startTime = startUnixTime;
        aggregated.endTime = currentTime;
    } else {
        // NTP not synced - use 0 to indicate invalid timestamps
        aggregated.startTime = 0;
        aggregated.endTime = 0;
    }
    
    // Capture individual sample counts for each metric
    aggregated.tempSampleCount = tempFilter->getSampleCount();
    aggregated.humSampleCount = humFilter->getSampleCount();
    aggregated.pressSampleCount = pressFilter->getSampleCount();
    aggregated.current1SampleCount = current1Filter->getSampleCount();
    aggregated.current2SampleCount = current2Filter->getSampleCount();
    
    // Keep backward compatibility - use minimum count
    aggregated.sampleCount = min({aggregated.tempSampleCount, aggregated.humSampleCount, 
                                 aggregated.pressSampleCount, aggregated.current1SampleCount, 
                                 aggregated.current2SampleCount});
    
    Serial.printf("Sample counts - Temp: %d, Hum: %d, Press: %d, I1: %d, I2: %d\n",
                  aggregated.tempSampleCount, aggregated.humSampleCount, aggregated.pressSampleCount,
                  aggregated.current1SampleCount, aggregated.current2SampleCount);
    
    // Temperature data - populate if we have samples
    if (aggregated.tempSampleCount > 0) {
        aggregated.tempMin = tempFilter->getMin();
        aggregated.tempMax = tempFilter->getMax();
        aggregated.tempAvg = tempFilter->getAverage();
    } else {
        aggregated.tempMin = aggregated.tempMax = aggregated.tempAvg = 0.0;
        Serial.println("No temperature samples available");
    }
    
    // Humidity data - populate if we have samples
    if (aggregated.humSampleCount > 0) {
        aggregated.humMin = humFilter->getMin();
        aggregated.humMax = humFilter->getMax();
        aggregated.humAvg = humFilter->getAverage();
    } else {
        aggregated.humMin = aggregated.humMax = aggregated.humAvg = 0.0;
        Serial.println("No humidity samples available");
    }
    
    // Pressure data - populate if we have samples
    if (aggregated.pressSampleCount > 0) {
        aggregated.pressMin = pressFilter->getMin();
        aggregated.pressMax = pressFilter->getMax();
        aggregated.pressAvg = pressFilter->getAverage();
    } else {
        aggregated.pressMin = aggregated.pressMax = aggregated.pressAvg = 0.0;
        Serial.println("No pressure samples available");
    }
    
    // Current 1 data - populate if we have samples
    if (aggregated.current1SampleCount > 0) {
        aggregated.current1Min = current1Filter->getMin();
        aggregated.current1Max = current1Filter->getMax();
        aggregated.current1Avg = current1Filter->getAverage();
        aggregated.current1RMS = current1Filter->getRMS();
        aggregated.dutyCycle1 = calculateDutyCycle(current1Filter, currentThreshold1);
    } else {
        aggregated.current1Min = aggregated.current1Max = aggregated.current1Avg = aggregated.current1RMS = 0.0;
        aggregated.dutyCycle1 = 0.0;
        Serial.println("No current1 samples available");
    }
    
    // Current 2 data - populate if we have samples
    if (aggregated.current2SampleCount > 0) {
        aggregated.current2Min = current2Filter->getMin();
        aggregated.current2Max = current2Filter->getMax();
        aggregated.current2Avg = current2Filter->getAverage();
        aggregated.current2RMS = current2Filter->getRMS();
        aggregated.dutyCycle2 = calculateDutyCycle(current2Filter, currentThreshold2);
    } else {
        aggregated.current2Min = aggregated.current2Max = aggregated.current2Avg = aggregated.current2RMS = 0.0;
        aggregated.dutyCycle2 = 0.0;
        Serial.println("No current2 samples available");
    }
    
    if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        lastAggregated = aggregated;
        xSemaphoreGive(dataMutex);
    }
    
    tempFilter->reset();
    humFilter->reset();
    pressFilter->reset();
    current1Filter->reset();
    current2Filter->reset();
    
    Serial.printf("Aggregated: T=%.1f, P=%.1f, I1=%.2f, I2=%.2f, DC1=%.1f%%, DC2=%.1f%%\n",
                  aggregated.tempAvg, aggregated.pressAvg, aggregated.current1Avg, 
                  aggregated.current2Avg, aggregated.dutyCycle1, aggregated.dutyCycle2);
}

float DataCollector::calculateDutyCycle(NoiseFilter* filter, float threshold) {
    if (filter->getSampleCount() == 0) return 0.0;
    
    uint16_t aboveThreshold = 0;
    uint16_t totalSamples = filter->getSampleCount();
    
    for (uint16_t i = 0; i < totalSamples; i++) {
        if (filter->getAverage() > threshold) {
            aboveThreshold++;
        }
    }
    
    return (float)aboveThreshold / totalSamples * 100.0;
}