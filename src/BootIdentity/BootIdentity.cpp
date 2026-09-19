#include "BootIdentity.h"

#include <Arduino.h>
#include <Preferences.h>

namespace {

constexpr char NVS_NAMESPACE[] = "hydro_identity";
constexpr char BOOT_ID_KEY[] = "boot_id";

bool initialized = false;
uint32_t currentBootId = 0;
uint32_t currentSequenceNumber = 0;

} // namespace

namespace BootIdentity {

bool begin()
{
    if (initialized) return true;

    Preferences preferences;
    if (!preferences.begin(NVS_NAMESPACE, false)) return false;

    const uint32_t previousBootId = preferences.getULong(BOOT_ID_KEY, 0);
    currentBootId = previousBootId == UINT32_MAX ? 1 : previousBootId + 1;
    const size_t written = preferences.putULong(BOOT_ID_KEY, currentBootId);
    preferences.end();

    if (written != sizeof(currentBootId)) {
        currentBootId = 0;
        return false;
    }

    currentSequenceNumber = 0;
    initialized = true;
    return true;
}

bool isReady()
{
    return initialized;
}

uint32_t bootId()
{
    return currentBootId;
}

uint32_t nextSequenceNumber()
{
    if (!initialized) return 0;
    if (currentSequenceNumber == UINT32_MAX) {
        currentSequenceNumber = 1;
    } else {
        ++currentSequenceNumber;
    }
    return currentSequenceNumber;
}

} // namespace BootIdentity
