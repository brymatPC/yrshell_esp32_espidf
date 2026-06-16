#include "FrequencyEstimate.h"
#include "Utilities.h"
#include "esp_log_custom.h"

static const char* TAG = "FreqEst";

FrequencyEstimate::FrequencyEstimate() :
    m_runningAverage(0)
{

}
FrequencyEstimate::~FrequencyEstimate() {

}

void FrequencyEstimate::setInQ(CircularQBase<uint16_t> *prevQ) {
    m_inQ.setPreviousQ(prevQ);
}

void FrequencyEstimate::slice() {
    if(m_inQ.used() > 512) {
        uint16_t max = 0;
        uint16_t min = UINT16_MAX;
        uint16_t prevValue = 0;
        uint16_t numZeroCrossings = 0;
        uint16_t lastZeroCrossing = 0;
        uint16_t aveDist = 0;
        for(uint16_t i=0; i < 512; i++) {
            if(!m_inQ.valueAvailable()) break;
            uint16_t val = m_inQ.get();
            if(!m_aveQ.spaceAvailable()) {
                uint16_t old = m_aveQ.get();
                m_runningAverage -= old;
            }
            m_runningAverage += val;
            m_aveQ.put(val);

            if(val > max) {
                max = val;
            }
            if(val < min) {
                min = val;
            }
            if(m_aveQ.used() > 256) {
                uint32_t runAverage = m_runningAverage / m_aveQ.used();
                if(prevValue != 0) {
                    if(prevValue <= runAverage && val > runAverage) {
                        if(lastZeroCrossing != 0) {
                            aveDist += (i - lastZeroCrossing);
                            numZeroCrossings++;
                        }
                        lastZeroCrossing = i;
                    }
                }
            }
            prevValue = val;
        }

        float curAveF = ((float) m_runningAverage) / (((float) m_aveQ.used()));
        float aveDistF = ((float) aveDist) / ((float) numZeroCrossings);

        ESP_LOGI(TAG, "Freq: min %u, max %u, ave %.2f", min, max, curAveF);
        ESP_LOGI(TAG, "Freq: numZeroCrossings %u, aveDist %.2f", numZeroCrossings, aveDistF);
    }
}
