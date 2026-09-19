#pragma once

#include <stdint.h>

namespace BootIdentity {

bool begin();
bool isReady();
uint32_t bootId();
uint32_t nextSequenceNumber();

} // namespace BootIdentity
