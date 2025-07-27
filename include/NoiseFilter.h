#pragma once

#include <Arduino.h>

class NoiseFilter {
private:
    float* buffer;
    uint16_t bufferSize;
    uint16_t bufferIndex;
    uint16_t samplesCount;
    
    float outlierThreshold;
    float smoothingFactor;
    
    float lastFiltered;
    float lastAverage;
    
    bool initialized;
    
public:
    NoiseFilter(uint16_t size, float outlierThresh = 2.0, float smoothing = 0.1);
    ~NoiseFilter();
    
    void reset();
    void addSample(float sample);
    
    float getAverage() const;
    float getFiltered() const;
    float getMin() const;
    float getMax() const;
    float getRMS() const;
    
    bool isReady() const;
    uint16_t getSampleCount() const;
    
    void setOutlierThreshold(float threshold);
    void setSmoothingFactor(float factor);
    
private:
    bool isOutlier(float sample) const;
    void updateStatistics();
};