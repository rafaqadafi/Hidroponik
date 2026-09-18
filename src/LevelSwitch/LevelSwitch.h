#pragma once

#include <stdint.h>

namespace LevelSwitch {

enum class Id : uint8_t {
    PhUp = 0,
    NutrientA,
    NutrientB,
    PhDown,
    Count
};

bool begin();
bool isNormal();
bool isNormal(Id id);

} // namespace LevelSwitch
