#ifndef SD_LOGGER_H_
#define SD_LOGGER_H_

// ESP / C Libraries
#include <stdint.h>
#include <sdmmc_cmd.h>

// External components
#include <Sliceable.h>
#include <IntervalTimer.h>

#define SD_CONN_CHECK_MS (30000)
#define SD_FILE_MAX_SIZE (1024 * 1024)

class SdLogger : public Sliceable {
public:
    SdLogger();
    virtual const char* sliceName( void) { return "SdLogger"; }

    void init(uint8_t sck, uint8_t miso, uint8_t mosi, uint8_t cs, uint8_t cd);
    void stop();
    void slice();
    void logSdCardStatus();

    void log(const char *filePrefix, const char *record, bool createNew = false);

    bool getFilename(const char *filePrefix, const char *fileExt, char *filename, bool createNew = false);

private:
    uint8_t m_cs;
    uint8_t m_cd;
    IntervalTimer m_timer;
    bool m_connected;
    sdmmc_card_t *m_card;
    void mount();
    long findLargestNumberInFilenames(const char* dir, const char* prefix);
    void logSdCardStatus(sdmmc_card_t *card);
};

#endif // SD_LOGGER_H_