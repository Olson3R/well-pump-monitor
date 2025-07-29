#include "APIClient.h"

WellPumpAPIClient::WellPumpAPIClient(const APIConfig& config, const String& device, const String& loc) {
    baseURL = config.baseURL;
    apiKey = config.apiKey;
    deviceName = device;
    location = loc;
    useHttps = config.useHttps;
    verifyCertificate = config.verifyCertificate;
    
    httpClient = nullptr;
    secureClient = nullptr;
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

WellPumpAPIClient::~WellPumpAPIClient() {
    disconnect();
    if (buffer) {
        delete[] buffer;
        buffer = nullptr;
    }
}

bool WellPumpAPIClient::begin() {
    if (initialized) {
        return true;
    }
    
    if (!validateConfiguration()) {
        Serial.println("API Client: Invalid configuration");
        return false;
    }
    
    if (!setupHTTPClient()) {
        Serial.println("API Client: Failed to setup HTTP client");
        return false;
    }
    
    initialized = true;
    Serial.println("API Client: Initialized successfully");
    
    return testConnection();
}

bool WellPumpAPIClient::validateConfiguration() const {
    return baseURL.length() > 0 && 
           deviceName.length() > 0 &&
           location.length() > 0;
}

void WellPumpAPIClient::setCredentials(const APIConfig& config) {
    baseURL = config.baseURL;
    apiKey = config.apiKey;
    useHttps = config.useHttps;
    verifyCertificate = config.verifyCertificate;
    
    if (initialized) {
        disconnect();
        initialized = false;
    }
}

bool WellPumpAPIClient::setupHTTPClient() {
    cleanupHTTPClient();
    
    if (useHttps) {
        secureClient = new WiFiClientSecure();
        if (!verifyCertificate) {
            secureClient->setInsecure(); // Don't verify SSL certificates
        }
        httpClient = new HTTPClient();
    } else {
        httpClient = new HTTPClient();
    }
    
    return (httpClient != nullptr);
}

void WellPumpAPIClient::cleanupHTTPClient() {
    if (httpClient) {
        httpClient->end();
        delete httpClient;
        httpClient = nullptr;
    }
    
    if (secureClient) {
        delete secureClient;
        secureClient = nullptr;
    }
}

bool WellPumpAPIClient::testConnection() {
    if (!httpClient) return false;
    
    unsigned long now = millis();
    if (now - lastConnectionTest < CONNECTION_TEST_INTERVAL) {
        return connected;
    }
    
    // Test connection with health endpoint
    String url = baseURL + "/api/health";
    
    if (useHttps && secureClient) {
        httpClient->begin(*secureClient, url);
    } else {
        httpClient->begin(url);
    }
    
    httpClient->addHeader("Content-Type", "application/json");
    if (apiKey.length() > 0) {
        httpClient->addHeader("Authorization", "Bearer " + apiKey);
    }
    
    int httpResponseCode = httpClient->GET();
    
    connected = (httpResponseCode == 200);
    lastConnectionTest = now;
    
    if (connected) {
        Serial.println("API Client: Connection test successful");
        resetRetryCount();
    } else {
        Serial.print("API Client: Connection test failed. HTTP code: ");
        Serial.println(httpResponseCode);
        if (httpResponseCode > 0) {
            String response = httpClient->getString();
            Serial.println("Response: " + response);
        }
    }
    
    httpClient->end();
    return connected;
}

bool WellPumpAPIClient::connect() {
    return testConnection();
}

void WellPumpAPIClient::disconnect() {
    cleanupHTTPClient();
    connected = false;
}

bool WellPumpAPIClient::sendSensorData(const AggregatedData& data) {
    if (!initialized || !connected) {
        Serial.println("API Client: Not connected, buffering data");
        addToBuffer(data);
        return false;
    }
    
    if (sendSensorDataToAPI(data)) {
        resetRetryCount();
        return true;
    } else {
        Serial.println("API Client: Failed to send sensor data, buffering");
        addToBuffer(data);
        return false;
    }
}

bool WellPumpAPIClient::sendEvent(const Event& event) {
    if (!initialized || !connected) {
        Serial.println("API Client: Not connected, cannot send event");
        return false;
    }
    
    return sendEventToAPI(event);
}

bool WellPumpAPIClient::sendSensorDataToAPI(const AggregatedData& data) {
    String payload = createSensorJSON(data);
    return makeRequest("/api/sensors", "POST", payload);
}

bool WellPumpAPIClient::sendEventToAPI(const Event& event) {
    String payload = createEventJSON(event);
    return makeRequest("/api/events", "POST", payload);
}

bool WellPumpAPIClient::makeRequest(const String& endpoint, const String& method, const String& payload) {
    if (!httpClient) return false;
    
    String url = baseURL + endpoint;
    
    if (useHttps && secureClient) {
        httpClient->begin(*secureClient, url);
    } else {
        httpClient->begin(url);
    }
    
    httpClient->addHeader("Content-Type", "application/json");
    if (apiKey.length() > 0) {
        httpClient->addHeader("Authorization", "Bearer " + apiKey);
    }
    
    int httpResponseCode;
    if (method == "POST") {
        httpResponseCode = httpClient->POST(payload);
    } else if (method == "GET") {
        httpResponseCode = httpClient->GET();
    } else {
        httpClient->end();
        return false;
    }
    
    bool success = (httpResponseCode == 200 || httpResponseCode == 201);
    
    if (!success) {
        Serial.print("API Request failed. HTTP code: ");
        Serial.println(httpResponseCode);
        String response = httpClient->getString();
        if (response.length() > 0) {
            Serial.println("Response: " + response);
        }
    } else {
        Serial.println("API Request successful: " + endpoint);
    }
    
    httpClient->end();
    return success;
}

String WellPumpAPIClient::createSensorJSON(const AggregatedData& data) {
    JsonDocument doc;
    
    doc["device"] = deviceName;
    doc["location"] = location;
    doc["timestamp"] = formatTimestamp(data.endTime);
    doc["startTime"] = formatTimestamp(data.startTime);
    doc["endTime"] = formatTimestamp(data.endTime);
    doc["sampleCount"] = data.sampleCount;
    
    // Temperature data (flattened to match API format)
    doc["tempMin"] = data.tempMin;
    doc["tempMax"] = data.tempMax;
    doc["tempAvg"] = data.tempAvg;
    
    // Humidity data
    doc["humMin"] = data.humMin;
    doc["humMax"] = data.humMax;
    doc["humAvg"] = data.humAvg;
    
    // Pressure data
    doc["pressMin"] = data.pressMin;
    doc["pressMax"] = data.pressMax;
    doc["pressAvg"] = data.pressAvg;
    
    // Current 1 data
    doc["current1Min"] = data.current1Min;
    doc["current1Max"] = data.current1Max;
    doc["current1Avg"] = data.current1Avg;
    doc["current1RMS"] = data.current1RMS;
    doc["dutyCycle1"] = data.dutyCycle1;
    
    // Current 2 data
    doc["current2Min"] = data.current2Min;
    doc["current2Max"] = data.current2Max;
    doc["current2Avg"] = data.current2Avg;
    doc["current2RMS"] = data.current2RMS;
    doc["dutyCycle2"] = data.dutyCycle2;
    
    String result;
    serializeJson(doc, result);
    return result;
}

String WellPumpAPIClient::createEventJSON(const Event& event) {
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

String WellPumpAPIClient::formatTimestamp(unsigned long timestamp) {
    // Check if this looks like a unix timestamp or millis()
    if (timestamp > 1600000000) {
        // Looks like a unix timestamp (seconds since epoch) - convert to milliseconds
        unsigned long long timestampMs = (unsigned long long)timestamp * 1000;
        Serial.printf("API: Using unix timestamp %lu -> %llu ms\n", timestamp, timestampMs);
        return String(timestampMs);
    } else {
        // Looks like millis() - this indicates NTP sync failed
        Serial.printf("API: Warning - received millis timestamp %lu, NTP not synced!\n", timestamp);
        return String(0); // Return 0 to indicate invalid timestamp
    }
}

void WellPumpAPIClient::addToBuffer(const AggregatedData& data) {
    buffer[bufferIndex].data = data;
    buffer[bufferIndex].timestamp = millis();
    buffer[bufferIndex].valid = true;
    
    bufferIndex = (bufferIndex + 1) % bufferSize;
    if (bufferedCount < bufferSize) {
        bufferedCount++;
    }
    
    Serial.println("API Client: Data added to buffer (" + String(bufferedCount) + "/" + String(bufferSize) + ")");
}

bool WellPumpAPIClient::processBuffer() {
    if (bufferedCount == 0 || !connected) {
        return true;
    }
    
    bool allSuccess = true;
    uint8_t processed = 0;
    
    Serial.println("API Client: Processing buffer (" + String(bufferedCount) + " items)");
    
    for (uint8_t i = 0; i < bufferSize && processed < bufferedCount; i++) {
        if (buffer[i].valid) {
            if (sendSensorDataToAPI(buffer[i].data)) {
                buffer[i].valid = false;
                processed++;
                Serial.println("API Client: Sent buffered data item " + String(processed));
            } else {
                allSuccess = false;
                Serial.println("API Client: Failed to send buffered data, stopping");
                break; // Stop on first failure to avoid overwhelming the server
            }
            delay(100); // Small delay between requests
        }
    }
    
    // Update buffered count
    bufferedCount -= processed;
    
    if (processed > 0) {
        Serial.println("API Client: Processed " + String(processed) + " buffered items, " + String(bufferedCount) + " remaining");
    }
    
    return allSuccess;
}

bool WellPumpAPIClient::flushBuffer() {
    return processBuffer();
}

void WellPumpAPIClient::update() {
    if (!initialized) {
        return;
    }
    
    unsigned long now = millis();
    
    // Handle retry logic
    if (!connected && (now - lastRetryTime > getRetryDelay())) {
        Serial.println("API Client: Attempting reconnection (attempt " + String(retryCount + 1) + ")");
        if (testConnection()) {
            Serial.println("API Client: Reconnected successfully");
            processBuffer(); // Try to flush buffer when reconnected
        } else {
            retryCount++;
            lastRetryTime = now;
            if (retryCount <= maxRetries) {
                Serial.println("API Client: Reconnection failed, will retry in " + String(getRetryDelay()/1000) + " seconds");
            }
        }
    }
    
    // Process buffer periodically even when connected
    if (connected && bufferedCount > 0) {
        processBuffer();
    }
}

unsigned long WellPumpAPIClient::getRetryDelay() const {
    unsigned long delay = RETRY_DELAY * (1UL << min(retryCount, (uint16_t)4)); // Exponential backoff
    return min(delay, MAX_RETRY_DELAY);
}

String WellPumpAPIClient::getConnectionStatus() const {
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

String WellPumpAPIClient::getLastError() const {
    if (!httpClient) return "No HTTP client";
    return "HTTP error";
}