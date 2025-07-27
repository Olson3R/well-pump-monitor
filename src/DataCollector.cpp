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
    
    dataQueue = xQueueCreate(QUEUE_SIZE, sizeof(SensorData));
    if (dataQueue == NULL) {
        Serial.println("Failed to create data queue");
        return false;
    }
    
    dataMutex = xSemaphoreCreateMutex();
    if (dataMutex == NULL) {
        Serial.println("Failed to create data mutex");
        vQueueDelete(dataQueue);
        return false;
    }
    
    BaseType_t result1 = xTaskCreate(
        collectionTaskWrapper,
        "DataCollection",
        4096,
        this,
        2,
        &collectionTask
    );
    
    BaseType_t result2 = xTaskCreate(
        aggregationTaskWrapper,
        "DataAggregation",
        4096,
        this,
        1,
        &aggregationTask
    );
    
    if (result1 != pdPASS || result2 != pdPASS) {
        Serial.println("Failed to create tasks");
        stop();
        return false;
    }
    
    running = true;
    lastAggregationTime = millis();
    
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

void DataCollector::collectionTaskWrapper(void* parameter) {
    DataCollector* collector = static_cast<DataCollector*>(parameter);
    collector->collectionTaskFunction();
}

void DataCollector::aggregationTaskWrapper(void* parameter) {
    DataCollector* collector = static_cast<DataCollector*>(parameter);
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
    const TickType_t xFrequency = pdMS_TO_TICKS(10000);
    
    while (running) {
        unsigned long now = millis();
        if (now - lastAggregationTime >= AGGREGATION_INTERVAL) {
            aggregateData();
            lastAggregationTime = now;
        }
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}

void DataCollector::collectSensorData() {
    if (!sensorManager || !sensorManager->isHealthy()) {
        return;
    }
    
    SensorData data;
    data.timestamp = millis();
    data.valid = true;
    
    bool tempOk = sensorManager->readTemperature(data.temperature);
    bool humOk = sensorManager->readHumidity(data.humidity);
    bool pressOk = sensorManager->readPressure(data.pressure);
    bool current1Ok = sensorManager->readCurrent1(data.current1);
    bool current2Ok = sensorManager->readCurrent2(data.current2);
    
    if (tempOk) tempFilter->addSample(data.temperature);
    if (humOk) humFilter->addSample(data.humidity);
    if (pressOk) pressFilter->addSample(data.pressure);
    if (current1Ok) current1Filter->addSample(data.current1);
    if (current2Ok) current2Filter->addSample(data.current2);
    
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

void DataCollector::aggregateData() {
    if (!tempFilter->isReady() || !current1Filter->isReady()) {
        return;
    }
    
    AggregatedData aggregated;
    aggregated.startTime = lastAggregationTime;
    aggregated.endTime = millis();
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