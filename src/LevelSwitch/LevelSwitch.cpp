#include "LevelSwitch.h"
#include <Arduino.h>
#include "Config/config.h"

namespace {

constexpr uint8_t SWITCH_COUNT =
    static_cast<uint8_t>(LevelSwitch::Id::Count);
bool debouncedState[SWITCH_COUNT] = {true, true, true, true};
bool lastRawState[SWITCH_COUNT] = {true, true, true, true};
uint32_t lastChangeTime[SWITCH_COUNT] = {0, 0, 0, 0};
bool initialized = false;

uint8_t pinFor(LevelSwitch::Id id)
{
    switch (id) {
    case LevelSwitch::Id::PhUp:
        return Config::Pins::LEVEL_SWITCH;
    case LevelSwitch::Id::NutrientA:
        return Config::Pins::FLOAT_NUTRIENT_A;
    case LevelSwitch::Id::NutrientB:
        return Config::Pins::FLOAT_NUTRIENT_B;
    case LevelSwitch::Id::PhDown:
        return Config::Pins::FLOAT_PH_DOWN;
    case LevelSwitch::Id::Count:
        break;
    }
    return Config::Pins::LEVEL_SWITCH;
}

bool readNormal(LevelSwitch::Id id)
{
    const uint8_t index = static_cast<uint8_t>(id);
    const int pinState = digitalRead(pinFor(id));
    const bool rawState = Config::LevelSwitch::ACTIVE_LOW
        ? (pinState == LOW)
        : (pinState == HIGH);

    if (rawState != lastRawState[index]) {
        lastChangeTime[index] = millis();
        lastRawState[index] = rawState;
    }

    if ((millis() - lastChangeTime[index]) >=
        Config::LevelSwitch::DEBOUNCE_DELAY_MS) {
        debouncedState[index] = rawState;
    }

    return debouncedState[index];
}

} // namespace

namespace LevelSwitch {

bool begin()
{
    for (uint8_t index = 0; index < SWITCH_COUNT; ++index) {
        const Id id = static_cast<Id>(index);
        pinMode(pinFor(id), INPUT_PULLUP);
        const int pinState = digitalRead(pinFor(id));
        const bool state = Config::LevelSwitch::ACTIVE_LOW
            ? (pinState == LOW)
            : (pinState == HIGH);
        debouncedState[index] = state;
        lastRawState[index] = state;
        lastChangeTime[index] = millis();
    }
    initialized = true;
    return true;
}

bool isNormal()
{
    return isNormal(Id::PhUp);
}

bool isNormal(Id id)
{
    if (!initialized || id == Id::Count) return true;
    return readNormal(id);
}

} // namespace LevelSwitch
