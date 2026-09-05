#include "NetworkGate.h"

#include <freertos/event_groups.h>

namespace {

EventGroupHandle_t eventGroup = nullptr;
constexpr EventBits_t NETWORK_CONNECTED_BIT = (1U << 0);

}

namespace NetworkGate {

bool begin()
{
    if (eventGroup != nullptr) return true;
    eventGroup = xEventGroupCreate();
    return eventGroup != nullptr;
}

void setConnected(bool connected)
{
    if (eventGroup == nullptr) return;
    if (connected) xEventGroupSetBits(eventGroup, NETWORK_CONNECTED_BIT);
    else xEventGroupClearBits(eventGroup, NETWORK_CONNECTED_BIT);
}

bool waitUntilConnected(TickType_t timeout)
{
    if (eventGroup == nullptr) return false;
    const EventBits_t bits = xEventGroupWaitBits(
        eventGroup,
        NETWORK_CONNECTED_BIT,
        pdFALSE,
        pdTRUE,
        timeout);
    return (bits & NETWORK_CONNECTED_BIT) != 0;
}

bool isConnected()
{
    return eventGroup != nullptr &&
           (xEventGroupGetBits(eventGroup) & NETWORK_CONNECTED_BIT) != 0;
}

}
