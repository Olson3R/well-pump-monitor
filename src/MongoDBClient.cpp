#include "MongoDBClient.h"

WellPumpMongoClient::WellPumpMongoClient(const String& url, const String& key, 
                                        const String& dataSource, const String& db,
                                        const String& device, const String& loc)
{
    mongoURL = url;
    apiKey = key;
    this->dataSource = dataSource;
    database = db;
    deviceName = device;
    location = loc;
    
    sensorCollection = "sensor_data";
    eventCollection = "events";
    
    httpClient = nullptr;
    connected = false;
    initialized = false;
    lastConnectionTest = 0;
    lastRetryTime = 0;
    retryCount = 0;
    maxRetries = 3;
    
    bufferSize = BUFFER_SIZE;
    buffer = new DataBuffer[bufferSize];
    bufferIndex = 0;
    bufferedCount = 0;
    
    for (uint8_t i = 0; i < bufferSize; i++) {
        buffer[i].valid = false;
    }
}

WellPumpMongoClient::~WellPumpMongoClient() {
    disconnect();
    if (buffer) {
        delete[] buffer;
        buffer = nullptr;
    }
}

bool WellPumpMongoClient::begin() {
    if (initialized) {
        return true;
    }
    
    if (!validateConfiguration()) {
        return false;
    }
    
    if (httpClient) {
        delete httpClient;
    }
    httpClient = new HTTPClient();
    
    initialized = true;
    return testConnection();
}

bool WellPumpMongoClient::validateConfiguration() const {
    return mongoURL.length() > 0 && 
           apiKey.length() > 0 && 
           dataSource.length() > 0 && 
           database.length() > 0 &&
           deviceName.length() > 0;
}

void WellPumpMongoClient::setCredentials(const String& url, const String& key, 
                                        const String& dataSource, const String& db) {
    mongoURL = url;
    apiKey = key;
    this->dataSource = dataSource;
    database = db;
    
    if (initialized) {
        disconnect();
        initialized = false;
    }
}

bool WellPumpMongoClient::testConnection() {
    if (!httpClient) return false;
    
    unsigned long now = millis();
    if (now - lastConnectionTest < CONNECTION_TEST_INTERVAL) {
        return connected;
    }
    
    // Test connection by trying to find one document (should return 200 even if empty)
    JsonDocument testDoc;
    testDoc["dataSource"] = dataSource;
    testDoc["database"] = database;
    testDoc["collection"] = sensorCollection;
    testDoc["filter"] = JsonObject();
    testDoc["limit"] = 1;
    
    String testPayload;
    serializeJson(testDoc, testPayload);
    
    httpClient->begin(mongoURL + "/action/findOne");
    httpClient->addHeader("Content-Type", "application/json");
    httpClient->addHeader("Authorization", "Bearer " + apiKey);
    
    int httpResponseCode = httpClient->POST(testPayload);
    
    connected = (httpResponseCode == 200);
    lastConnectionTest = now;
    
    httpClient->end();
    
    if (connected) {
        resetRetryCount();
    }
    
    return connected;
}

bool WellPumpMongoClient::connect() {
    if (!httpClient) return false;
    return testConnection();
}

void WellPumpMongoClient::disconnect() {
    if (httpClient) {
        httpClient->end();
        delete httpClient;
        httpClient = nullptr;
    }
    connected = false;
}

bool WellPumpMongoClient::writeAggregatedData(const AggregatedData& data) {
    if (!initialized || !connected) {
        addToBuffer(data);
        return false;
    }
    
    if (writeDataDocument(data)) {
        resetRetryCount();
        return true;
    } else {
        addToBuffer(data);
        return false;
    }
}

bool WellPumpMongoClient::writeEvent(const Event& event) {
    if (!initialized || !connected) {
        return false;
    }
    
    return writeEventDocument(event);
}

bool WellPumpMongoClient::writeDataDocument(const AggregatedData& data) {
    String document = createSensorDocument(data);
    return insertDocument(sensorCollection, document);
}

bool WellPumpMongoClient::writeEventDocument(const Event& event) {
    String document = createEventDocument(event);
    return insertDocument(eventCollection, document);
}

bool WellPumpMongoClient::insertDocument(const String& collection, const String& document) {
    if (!httpClient) return false;
    
    JsonDocument insertDoc;
    insertDoc["dataSource"] = dataSource;
    insertDoc["database"] = database;
    insertDoc["collection"] = collection;
    
    JsonDocument docObj;
    deserializeJson(docObj, document);
    insertDoc["document"] = docObj;
    
    String payload;
    serializeJson(insertDoc, payload);
    
    httpClient->begin(mongoURL + "/action/insertOne");
    httpClient->addHeader("Content-Type", "application/json");
    httpClient->addHeader("Authorization", "Bearer " + apiKey);
    
    int httpResponseCode = httpClient->POST(payload);
    
    bool success = (httpResponseCode == 201 || httpResponseCode == 200);
    
    if (!success) {
        Serial.print("MongoDB insert failed. HTTP code: ");
        Serial.println(httpResponseCode);
        String response = httpClient->getString();
        if (response.length() > 0) {
            Serial.println("Response: " + response);
        }
    }
    
    httpClient->end();
    return success;
}

String WellPumpMongoClient::createSensorDocument(const AggregatedData& data) {
    JsonDocument doc;
    
    doc["device"] = deviceName;
    doc["location"] = location;
    doc["timestamp"] = formatTimestamp(data.endTime);
    doc["startTime"] = formatTimestamp(data.startTime);
    doc["endTime"] = formatTimestamp(data.endTime);
    doc["sampleCount"] = data.sampleCount;
    
    // Temperature data
    JsonObject temp = doc["temperature"].to<JsonObject>();
    temp["min"] = data.tempMin;
    temp["max"] = data.tempMax;
    temp["avg"] = data.tempAvg;
    
    // Humidity data
    JsonObject hum = doc["humidity"].to<JsonObject>();
    hum["min"] = data.humMin;
    hum["max"] = data.humMax;
    hum["avg"] = data.humAvg;
    
    // Pressure data
    JsonObject press = doc["pressure"].to<JsonObject>();
    press["min"] = data.pressMin;
    press["max"] = data.pressMax;
    press["avg"] = data.pressAvg;
    
    // Current 1 data
    JsonObject curr1 = doc["current1"].to<JsonObject>();
    curr1["min"] = data.current1Min;
    curr1["max"] = data.current1Max;
    curr1["avg"] = data.current1Avg;
    curr1["rms"] = data.current1RMS;
    curr1["dutyCycle"] = data.dutyCycle1;
    
    // Current 2 data
    JsonObject curr2 = doc["current2"].to<JsonObject>();
    curr2["min"] = data.current2Min;
    curr2["max"] = data.current2Max;
    curr2["avg"] = data.current2Avg;
    curr2["rms"] = data.current2RMS;
    curr2["dutyCycle"] = data.dutyCycle2;
    
    String result;
    serializeJson(doc, result);
    return result;
}

String WellPumpMongoClient::createEventDocument(const Event& event) {
    JsonDocument doc;
    
    doc["device"] = deviceName;
    doc["location"] = location;
    doc["timestamp"] = formatTimestamp(event.startTime);
    doc["type"] = (int)event.type;
    doc["value"] = event.value;
    doc["threshold"] = event.threshold;
    doc["startTime"] = formatTimestamp(event.startTime);
    doc["duration"] = event.duration;
    doc["active"] = event.active;
    doc["description"] = event.description;
    
    String result;
    serializeJson(doc, result);
    return result;
}

String WellPumpMongoClient::formatTimestamp(unsigned long timestamp) {
    // Convert milliseconds to ISO 8601 format
    // For simplicity, we'll use the timestamp as-is, but in production
    // you might want to convert to proper ISO format
    return String(timestamp);
}

void WellPumpMongoClient::addToBuffer(const AggregatedData& data) {
    buffer[bufferIndex].data = data;
    buffer[bufferIndex].timestamp = millis();
    buffer[bufferIndex].valid = true;
    
    bufferIndex = (bufferIndex + 1) % bufferSize;
    if (bufferedCount < bufferSize) {
        bufferedCount++;
    }
}

bool WellPumpMongoClient::processBuffer() {
    if (bufferedCount == 0 || !connected) {
        return true;
    }
    
    bool allSuccess = true;
    uint8_t processed = 0;
    
    for (uint8_t i = 0; i < bufferSize && processed < bufferedCount; i++) {
        if (buffer[i].valid) {
            if (writeDataDocument(buffer[i].data)) {
                buffer[i].valid = false;
                processed++;
            } else {
                allSuccess = false;
                break; // Stop on first failure to avoid overwhelming the server
            }
        }
    }
    
    // Update buffered count
    bufferedCount -= processed;
    
    return allSuccess;
}

bool WellPumpMongoClient::flushBuffer() {
    return processBuffer();
}

void WellPumpMongoClient::update() {
    if (!initialized) {
        return;
    }
    
    unsigned long now = millis();
    
    // Handle retry logic
    if (!connected && (now - lastRetryTime > getRetryDelay())) {
        if (testConnection()) {
            processBuffer(); // Try to flush buffer when reconnected
        } else {
            retryCount++;
            lastRetryTime = now;
        }
    }
    
    // Process buffer periodically even when connected
    if (connected && bufferedCount > 0) {
        processBuffer();
    }
}

unsigned long WellPumpMongoClient::getRetryDelay() const {
    unsigned long delay = RETRY_DELAY * (1UL << min(retryCount, (uint16_t)4)); // Exponential backoff
    return min(delay, MAX_RETRY_DELAY);
}

String WellPumpMongoClient::getConnectionStatus() const {
    if (!initialized) {
        return "Not initialized";
    }
    if (connected) {
        if (bufferedCount > 0) {
            return "Connected (buffer: " + String(bufferedCount) + ")";
        }
        return "Connected";
    }
    if (retryCount > maxRetries) {
        return "Failed (max retries exceeded)";
    }
    return "Disconnected (retry " + String(retryCount) + "/" + String(maxRetries) + ")";
}

String WellPumpMongoClient::getLastError() const {
    if (!httpClient) return "No HTTP client";
    return "HTTP error";
}