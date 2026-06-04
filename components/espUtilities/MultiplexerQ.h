#ifndef MULTIPLEXER_Q_H_
#define MULTIPLEXER_Q_H_

#include "CircularQ.h"
#include "Sliceable.h"

#include <stdint.h>

// Provides a simple method for multiplexing data onto a single channel (Typically uart)
// Data is prepended with a 1 byte header, with upper 4 bits indicating channel, and lower 4 bits indicating length
// No framing, so primary channel must have an idle time for synchronization

#define MAX_NUM_MUX_CHANNELS (4)
#define MAX_LEN_MUX_DATA     (15)
#define MUX_BUF_SIZE         (512)


typedef CircularQBase<char> CircularByteQ;

class MultiplexerQ : public Sliceable {
private:
    // Incoming data to be routed to appropriate internal queues
    CircularQ<char, MUX_BUF_SIZE> m_inQ;
    // Outgoing data to be sent over primary channel
    CircularQ<char, MUX_BUF_SIZE> m_outQ;

    CircularByteQ *m_inQueues[MAX_NUM_MUX_CHANNELS];
    CircularByteQ *m_outQueues[MAX_NUM_MUX_CHANNELS];

    uint8_t m_curInQueue;
    uint8_t m_curOutQueue;

    uint16_t m_inLength;
    uint16_t m_outLength;

    void processIncoming();
    void processOutgoing();

public:
    MultiplexerQ();
    virtual ~MultiplexerQ();
    virtual const char* sliceName( void) { return "MultiplexerQ"; }
    void init();
    virtual void slice( void);

    CircularByteQ *getInQ() { return &m_inQ; }
    CircularByteQ *getOutQ() { return &m_outQ; }

    void set(uint8_t channel, CircularByteQ *inQ, CircularByteQ *outQ);

};

#endif // MULTIPLEXER_Q_H_