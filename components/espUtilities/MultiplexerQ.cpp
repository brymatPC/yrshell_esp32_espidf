#include "MultiplexerQ.h"

#include <esp_log.h>

static const char* TAG = "MuxQ   ";

MultiplexerQ::MultiplexerQ() :
    m_curInQueue(0),
    m_curOutQueue(0),
    m_inLength(0),
    m_outLength(0)
{
    for(uint8_t i=0; i < MAX_NUM_MUX_CHANNELS; i++) {
        m_inQueues[i] = nullptr;
        m_outQueues[i] = nullptr;
    }
}
MultiplexerQ::~MultiplexerQ() {

}
void MultiplexerQ::init() {

}
void MultiplexerQ::set(uint8_t channel, CircularByteQ *inQ, CircularByteQ *outQ) {
    if(channel < MAX_NUM_MUX_CHANNELS) {
        m_inQueues[channel] = inQ;
        m_outQueues[channel] = outQ;
    }
}
void MultiplexerQ::slice( void) {
    processIncoming();
    processOutgoing();
}

void MultiplexerQ::processIncoming() {
    if(m_inLength > 0) {
        if(m_inQ.used() > 0) {
            if(m_inQueues[m_curInQueue] != nullptr && m_inQueues[m_curInQueue]->spaceAvailable(1)) {
                uint8_t val = m_inQ.get();
                m_inQueues[m_curInQueue]->put(val);
                m_inLength--;
            } else if(m_inQueues[m_curInQueue] == nullptr) {
                // Discard data for invalid channel
                m_inQ.get();
                m_inLength--;
            }
        }
    } else {
        // Look for header
        if(m_inQ.used() > 0) {
            uint8_t val = m_inQ.get();
            uint8_t channel = (val & 0xF0) >> 4;
            uint16_t len = (val & 0x0F);
            if(channel < MAX_NUM_MUX_CHANNELS && len > 0) {
                m_curInQueue = channel;
                m_inLength = len;
                ESP_LOGD(TAG, "New header: ch %u len %u", channel, len);
            } else {
                m_curInQueue = 0;
                m_inLength = 0;
                ESP_LOGD(TAG, "Invalid header: ch %u len %u, ignoring", channel, len);
            }
        }
    }
}
void MultiplexerQ::processOutgoing() {
    if(m_outLength == 0) {
        // Switch to next valid outQueue
        m_curOutQueue++;
        if(m_curOutQueue >= MAX_NUM_MUX_CHANNELS) {
            m_curOutQueue = 0;
        }
        if(m_outQueues[m_curOutQueue] != nullptr) {
            uint16_t len = m_outQueues[m_curOutQueue]->used();
            if(len > MAX_LEN_MUX_DATA) {
                len = MAX_LEN_MUX_DATA;
            }
            m_outLength = len;
        }
    } else {
        // + 1 for header byte
        if(m_outQ.spaceAvailable(m_outLength+1)) {
            uint8_t header = m_curOutQueue << 4;
            header |= (m_outLength & 0x0F);
            m_outQ.put(header);
            while(m_outLength > 0) {
                m_outQ.put(m_outQueues[m_curOutQueue]->get());
                m_outLength--;
            }
        }
    }
}