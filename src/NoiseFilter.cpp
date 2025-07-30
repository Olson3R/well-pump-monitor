#include "NoiseFilter.h"
#include <math.h>

NoiseFilter::NoiseFilter(uint16_t size, float outlierThresh, float smoothing) {
    bufferSize = size;
    bufferIndex = 0;
    samplesCount = 0;
    outlierThreshold = outlierThresh;
    smoothingFactor = smoothing;
    
    lastFiltered = 0.0;
    lastAverage = 0.0;
    initialized = false;
    
    buffer = new float[bufferSize];
    for (uint16_t i = 0; i < bufferSize; i++) {
        buffer[i] = 0.0;
    }
}

NoiseFilter::~NoiseFilter() {
    delete[] buffer;
}

void NoiseFilter::reset() {
    bufferIndex = 0;
    samplesCount = 0;
    lastFiltered = 0.0;
    lastAverage = 0.0;
    initialized = false;
    
    for (uint16_t i = 0; i < bufferSize; i++) {
        buffer[i] = 0.0;
    }
}

void NoiseFilter::addSample(float sample) {
    if (isnan(sample) || isinf(sample)) {
        return;
    }
    
    // Skip outlier detection for first 3 samples to avoid issues with initialization
    if (samplesCount > 3 && isOutlier(sample)) {
        return;
    }
    
    buffer[bufferIndex] = sample;
    bufferIndex = (bufferIndex + 1) % bufferSize;
    
    if (samplesCount < bufferSize) {
        samplesCount++;
    }
    
    updateStatistics();
}

float NoiseFilter::getAverage() const {
    if (samplesCount == 0) return 0.0;
    
    float sum = 0.0;
    for (uint16_t i = 0; i < samplesCount; i++) {
        sum += buffer[i];
    }
    
    return sum / samplesCount;
}

float NoiseFilter::getFiltered() const {
    return lastFiltered;
}

float NoiseFilter::getMin() const {
    if (samplesCount == 0) return 0.0;
    
    float minVal = buffer[0];
    for (uint16_t i = 1; i < samplesCount; i++) {
        if (buffer[i] < minVal) {
            minVal = buffer[i];
        }
    }
    
    return minVal;
}

float NoiseFilter::getMax() const {
    if (samplesCount == 0) return 0.0;
    
    float maxVal = buffer[0];
    for (uint16_t i = 1; i < samplesCount; i++) {
        if (buffer[i] > maxVal) {
            maxVal = buffer[i];
        }
    }
    
    return maxVal;
}

float NoiseFilter::getRMS() const {
    if (samplesCount == 0) return 0.0;
    
    float sumSquares = 0.0;
    for (uint16_t i = 0; i < samplesCount; i++) {
        sumSquares += buffer[i] * buffer[i];
    }
    
    return sqrt(sumSquares / samplesCount);
}

bool NoiseFilter::isReady() const {
    return samplesCount >= (bufferSize / 2);
}

uint16_t NoiseFilter::getSampleCount() const {
    return samplesCount;
}

void NoiseFilter::setOutlierThreshold(float threshold) {
    outlierThreshold = threshold;
}

void NoiseFilter::setSmoothingFactor(float factor) {
    smoothingFactor = constrain(factor, 0.01, 1.0);
}

bool NoiseFilter::isOutlier(float sample) const {
    if (samplesCount == 0) return false;
    
    float currentAvg = getAverage();
    float deviation = abs(sample - currentAvg);
    
    float threshold = outlierThreshold * currentAvg;
    if (threshold < 0.1) threshold = 0.1;
    
    return deviation > threshold;
}

void NoiseFilter::updateStatistics() {
    if (samplesCount == 0) return;
    
    float currentAvg = getAverage();
    
    if (!initialized) {
        lastFiltered = currentAvg;
        lastAverage = currentAvg;
        initialized = true;
    } else {
        lastFiltered = (smoothingFactor * currentAvg) + ((1.0 - smoothingFactor) * lastFiltered);
        lastAverage = currentAvg;
    }
}