#include "DataCollector.h"

DataCollector::DataCollector(SensorManager* sensorMgr) {
    sensorManager = sensorMgr;
    
    tempFilter = new NoiseFilter(FILTER_SIZE, 2.0, 0.1);
    humFilter = new NoiseFilter(FILTER_SIZE, 2.0, 0.1);
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
        2,
        &collectionTask
    );
    
    Serial.println("Creating aggregation task...");
    BaseType_t result2 = xTaskCreate(
        aggregationTaskWrapper,
        "DataAggregation",
        8192,
        this,
        4,  // Much higher priority than collection task
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
        xSemaphoreGive(dataMutex);
        return data.sampleCount > 0;
    }
    
    return false;
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
    const TickType_t xFrequency = pdMS_TO_TICKS(1000);
    
    while (running) {
        collectSensorData();
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}

void DataCollector::aggregationTaskFunction() {
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(100);  // Run every 100ms instead of 10s
    
    while (running) {
        unsigned long now = millis();
        
        // Process queue every 1 second to prevent overflow
        if (now - lastQueueProcessTime >= QUEUE_PROCESS_INTERVAL) {
            processQueueData();  // Drain queue and feed to filters
            lastQueueProcessTime = now;
        }
        
        // Aggregate data every 60 seconds for final statistics
        if (now - lastAggregationTime >= AGGREGATION_INTERVAL) {
            Serial.print("Creating 60-second aggregation... Queue size: ");
            Serial.println(uxQueueMessagesWaiting(dataQueue));
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
    
    data.valid = tempOk && humOk && pressOk && current1Ok && current2Ok;
    
    if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        currentData = data;
        xSemaphoreGive(dataMutex);
    }
    
    if (data.valid && dataQueue != NULL) {
        if (xQueueSend(dataQueue, &data, 0) != pdTRUE) {
            Serial.println("Data queue full, dropping sample");
        }
    }
}

void DataCollector::processQueueData() {
    SensorData data;
    int processed = 0;
    
    // Process all available samples to prevent queue overflow
    // Since we collect 1 sample/second, this should typically process 1-2 samples
    while (xQueueReceive(dataQueue, &data, 0) == pdTRUE) {
        if (data.valid) {
            // Feed data to filters for smoothing
            tempFilter->addSample(data.temperature);
            humFilter->addSample(data.humidity);  
            pressFilter->addSample(data.pressure);
            current1Filter->addSample(data.current1);
            current2Filter->addSample(data.current2);
        }
        processed++;
        
        // Safety limit to prevent infinite loop
        if (processed >= 100) {
            Serial.println("WARNING: Queue processing limit reached, queue may be overflowing");
            break;
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
    if (!tempFilter->isReady() || !current1Filter->isReady()) {
        Serial.println("Filters not ready, skipping aggregation");
        return;
    }
    
    Serial.println("Starting actual data aggregation...");
    
    AggregatedData aggregated;
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
    aggregated.sampleCount = min(tempFilter->getSampleCount(), current1Filter->getSampleCount());
    
    aggregated.tempMin = tempFilter->getMin();
    aggregated.tempMax = tempFilter->getMax();
    aggregated.tempAvg = tempFilter->getAverage();
    
    aggregated.humMin = humFilter->getMin();
    aggregated.humMax = humFilter->getMax();
    aggregated.humAvg = humFilter->getAverage();
    
    aggregated.pressMin = pressFilter->getMin();
    aggregated.pressMax = pressFilter->getMax();
    aggregated.pressAvg = pressFilter->getAverage();
    
    aggregated.current1Min = current1Filter->getMin();
    aggregated.current1Max = current1Filter->getMax();
    aggregated.current1Avg = current1Filter->getAverage();
    aggregated.current1RMS = current1Filter->getRMS();
    
    aggregated.current2Min = current2Filter->getMin();
    aggregated.current2Max = current2Filter->getMax();
    aggregated.current2Avg = current2Filter->getAverage();
    aggregated.current2RMS = current2Filter->getRMS();
    
    aggregated.dutyCycle1 = calculateDutyCycle(current1Filter, currentThreshold1);
    aggregated.dutyCycle2 = calculateDutyCycle(current2Filter, currentThreshold2);
    
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