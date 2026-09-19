#include "Logic.h"

#include <Arduino.h>
#include <time.h>
#include <cstring>
#include "Relay/Relay.h"

namespace {

constexpr float PH_MIN = 6.0f;
constexpr float PH_UP_HOLD_MAX = 6.2f;
constexpr float PH_MAX = 7.0f;
constexpr float PH_DOWN_HOLD_MIN = 6.8f;
constexpr float TDS_HYSTERESIS_DELTA = 5.0f;

constexpr float TEMP_LOW_TRIGGER = 24.5f;
constexpr float TEMP_LOW_CLEAR = 25.5f;
constexpr float TEMP_HIGH_CLEAR = 27.5f;
constexpr float TEMP_HIGH_TRIGGER = 28.5f;

constexpr int GROW_LIGHT_START_HOUR = 7;
constexpr int GROW_LIGHT_END_HOUR = 19;
constexpr float LUX_LOW_THRESHOLD = 2152.78f;
constexpr float LUX_HIGH_THRESHOLD = 2652.78f;

constexpr float DISTANCE_FULL = 30.0f;
constexpr float DISTANCE_LOW = 80.0f;

constexpr uint32_t PERSISTENCE_MS = 3000;
constexpr uint32_t FILL_TIMEOUT_MS = 300000;
constexpr uint32_t CONDITIONING_TIMEOUT_MS = 600000;
constexpr uint32_t RELEASE_TIMEOUT_MS = 300000;
constexpr uint32_t DOSING_PULSE_MS = 2000;
constexpr uint32_t DOSING_COOLDOWN_MS = 10000;
constexpr uint32_t MIN_VALID_EPOCH = 1704067200;
constexpr uint32_t MS_PER_DAY = 86400000UL;

struct PersistenceTimer {
    bool active = false;
    uint32_t startedAt = 0;

    bool update(bool condition, uint32_t now, uint32_t duration)
    {
        if (!condition) {
            active = false;
            return false;
        }
        if (!active) {
            active = true;
            startedAt = now;
        }
        return now - startedAt >= duration;
    }

    void reset()
    {
        active = false;
        startedAt = 0;
    }
};

enum class DosingPulse : uint8_t {
    None,
    PhUp,
    PhDown,
    Nutrient
};

Logic::State currentState = Logic::State::Init;
uint32_t stateEnteredAt = 0;
char faultReason[32] = "NONE";

bool phUpDemand = false;
bool phDownDemand = false;
bool nutrientDemand = false;
bool temperatureBuzzer = false;
bool growLight = false;

PersistenceTimer fullTankTimer;
PersistenceTimer emptyTankTimer;
PersistenceTimer flowLossTimer;
PersistenceTimer ultrasonicMonitorTimer;
PersistenceTimer ultrasonicFillTimer;
PersistenceTimer phInvalidTimer;
PersistenceTimer tdsInvalidTimer;

DosingPulse dosingPulse = DosingPulse::None;
uint32_t dosingPulseUntil = 0;
uint32_t lastDosingPulseAt = 0;
bool hasDosed = false;

uint32_t logicStartedAt = 0;
bool plantingDateReady = false;
int plantingStartDay = 0;
uint8_t plantingWeek = 1;
uint16_t plantingDay = 1;
float targetTds = 500.0f;

uint8_t lastDesiredMask = 0;
bool desiredMaskKnown = false;

bool deadlineReached(uint32_t now, uint32_t deadline)
{
    return static_cast<int32_t>(now - deadline) >= 0;
}

int dateSerial(const tm &value)
{
    tm midnight = value;
    midnight.tm_hour = 12;
    midnight.tm_min = 0;
    midnight.tm_sec = 0;
    return static_cast<int>(mktime(&midnight) / 86400);
}

bool readClock(tm &localTime, time_t &epoch)
{
    epoch = time(nullptr);
    if (epoch < static_cast<time_t>(MIN_VALID_EPOCH)) return false;
    localtime_r(&epoch, &localTime);
    return true;
}

void capturePlantingDate()
{
    if (plantingDateReady) return;

    tm localTime{};
    time_t nowEpoch = 0;
    if (readClock(localTime, nowEpoch)) {
        const time_t estimatedBootEpoch =
            nowEpoch - static_cast<time_t>(millis() / 1000UL);
        tm bootTime{};
        localtime_r(&estimatedBootEpoch, &bootTime);
        plantingStartDay = dateSerial(bootTime);
        plantingDateReady = true;
    }
}

void calculatePlantAge()
{
    int age = 1;
    if (plantingDateReady) {
        tm localTime{};
        time_t epoch = 0;
        if (readClock(localTime, epoch)) {
            age = dateSerial(localTime) - plantingStartDay + 1;
        }
    } else {
        age = static_cast<int>((millis() - logicStartedAt) / MS_PER_DAY) + 1;
    }

    if (age < 1) age = 1;
    plantingDay = static_cast<uint16_t>(age > 65535 ? 65535 : age);

    if (age <= 7) {
        plantingWeek = 1;
        targetTds = 500.0f;
    } else if (age <= 14) {
        plantingWeek = 2;
        targetTds = 600.0f;
    } else if (age <= 21) {
        plantingWeek = 3;
        targetTds = 700.0f;
    } else if (age <= 35) {
        plantingWeek = 4;
        targetTds = 800.0f;
    } else {
        plantingWeek = 5;
        targetTds = 800.0f;
    }
}

void setState(Logic::State next, uint32_t now, const char *reason = nullptr)
{
    if (currentState == next) return;
    currentState = next;
    stateEnteredAt = now;

    if (next == Logic::State::Fault && reason != nullptr) {
        strlcpy(faultReason, reason, sizeof(faultReason));
    } else if (next == Logic::State::Init) {
        strlcpy(faultReason, "NONE", sizeof(faultReason));
    }

    if (next != Logic::State::Conditioning) {
        dosingPulse = DosingPulse::None;
        dosingPulseUntil = 0;
    }
}

void updateTemperatureBuzzer(float temperature, bool valid)
{
    if (!valid) {
        temperatureBuzzer = false;
        return;
    }

    if (temperature < TEMP_LOW_TRIGGER || temperature > TEMP_HIGH_TRIGGER) {
        temperatureBuzzer = true;
    } else if (TEMP_LOW_TRIGGER <= temperature &&
               temperature <= TEMP_LOW_CLEAR) {
        // Pertahankan kondisi buzzer saat mendekati batas bawah.
    } else if (TEMP_HIGH_CLEAR <= temperature &&
               temperature <= TEMP_HIGH_TRIGGER) {
        // Pertahankan kondisi buzzer saat mendekati batas atas.
    } else {
        temperatureBuzzer = false;
    }
}

void updateGrowLight(float lux, bool valid, Logic::State state, bool timeValid,
                     int hour)
{
    if (!valid || !timeValid ||
        (state != Logic::State::Monitoring &&
         state != Logic::State::Filling &&
         state != Logic::State::Conditioning &&
         state != Logic::State::Release) ||
        hour < GROW_LIGHT_START_HOUR || hour >= GROW_LIGHT_END_HOUR) {
        growLight = false;
        return;
    }

    if (lux < LUX_LOW_THRESHOLD) {
        growLight = true;
    } else if (lux > LUX_HIGH_THRESHOLD) {
        growLight = false;
    }
}

void updatePhDemand(float ph)
{
    if (ph < PH_MIN) {
        phUpDemand = true;
    } else if (ph > PH_UP_HOLD_MAX) {
        phUpDemand = false;
    }

    if (ph > PH_MAX) {
        phDownDemand = true;
    } else if (ph < PH_DOWN_HOLD_MIN) {
        phDownDemand = false;
    }

    if (phUpDemand && phDownDemand) {
        phUpDemand = false;
        phDownDemand = false;
    }
}

void updateNutrientDemand(float tds)
{
    if (tds < targetTds) {
        nutrientDemand = true;
    } else if (tds > targetTds + TDS_HYSTERESIS_DELTA) {
        nutrientDemand = false;
    }
}

void startDosingPulse(DosingPulse type, uint32_t now)
{
    if (dosingPulse != DosingPulse::None &&
        !deadlineReached(now, dosingPulseUntil)) {
        return;
    }
    if (hasDosed && now - lastDosingPulseAt < DOSING_COOLDOWN_MS) return;

    dosingPulse = type;
    dosingPulseUntil = now + DOSING_PULSE_MS;
    lastDosingPulseAt = now;
    hasDosed = true;
}

void setPulseOutput(Logic::ActuatorState &output, uint32_t now)
{
    if (dosingPulse == DosingPulse::None ||
        deadlineReached(now, dosingPulseUntil)) {
        dosingPulse = DosingPulse::None;
        return;
    }

    switch (dosingPulse) {
    case DosingPulse::PhUp:
        output.phUpPump = true;
        break;
    case DosingPulse::PhDown:
        output.phDownPump = true;
        break;
    case DosingPulse::Nutrient:
        output.nutrientPump = true;
        break;
    case DosingPulse::None:
        break;
    }
}

} // namespace

namespace Logic {

bool begin()
{
    configTime(7 * 3600, 0, "pool.ntp.org", "time.google.com");

    currentState = State::Init;
    stateEnteredAt = millis();
    logicStartedAt = stateEnteredAt;
    strlcpy(faultReason, "NONE", sizeof(faultReason));
    phUpDemand = false;
    phDownDemand = false;
    nutrientDemand = false;
    temperatureBuzzer = false;
    growLight = false;
    fullTankTimer.reset();
    emptyTankTimer.reset();
    flowLossTimer.reset();
    ultrasonicMonitorTimer.reset();
    ultrasonicFillTimer.reset();
    phInvalidTimer.reset();
    tdsInvalidTimer.reset();
    dosingPulse = DosingPulse::None;
    dosingPulseUntil = 0;
    lastDosingPulseAt = 0;
    hasDosed = false;
    plantingDateReady = false;
    plantingStartDay = 0;
    plantingWeek = 1;
    plantingDay = 1;
    targetTds = 500.0f;
    desiredMaskKnown = false;
    lastDesiredMask = 0;
    return true;
}

ActuatorState evaluate(const Inputs &inputs)
{
    const uint32_t now = millis();
    if (stateEnteredAt == 0) stateEnteredAt = now;

    capturePlantingDate();
    calculatePlantAge();

    tm localTime{};
    time_t epoch = 0;
    const bool timeValid = readClock(localTime, epoch);
    updateTemperatureBuzzer(inputs.temperatureC, inputs.temperatureValid);

    ActuatorState output{};
    output.state = currentState;
    output.plantingDay = plantingDay;
    output.plantingWeek = plantingWeek;
    output.targetTds = targetTds;
    strlcpy(output.faultReason, faultReason, sizeof(output.faultReason));

    const bool allReagentsAvailable =
        inputs.phUpNormal && inputs.nutrientANormal &&
        inputs.nutrientBNormal && inputs.phDownNormal;

    switch (currentState) {
    case State::Init:
        if (inputs.distanceValid && inputs.phValid && inputs.tdsValid) {
            setState(State::Monitoring, now);
        } else if (now - stateEnteredAt >= PERSISTENCE_MS) {
            setState(State::Fault, now, "INIT_SENSOR_FAILED");
        }
        break;

    case State::Monitoring: {
        const bool ultrasonicFault = ultrasonicMonitorTimer.update(
            !inputs.distanceValid, now, PERSISTENCE_MS);

        // Turbidity dirty/cloudy hanya dilaporkan. Tidak menjadi fault.
        if (ultrasonicFault) {
            setState(State::Fault, now, "ULTRASONIC_INVALID");
        } else if (!allReagentsAvailable) {
            setState(State::Fault, now, "REAGENT_EMPTY");
        } else if (inputs.distanceValid && inputs.distanceCm >= DISTANCE_LOW) {
            setState(State::Filling, now);
        }
        break;
    }

    case State::Filling: {
        output.waterPump = true;
        const bool ultrasonicFault = ultrasonicFillTimer.update(
            !inputs.distanceValid, now, PERSISTENCE_MS);
        const bool full = fullTankTimer.update(
            inputs.distanceValid && inputs.distanceCm <= DISTANCE_FULL,
            now, PERSISTENCE_MS);
        const bool timeout = now - stateEnteredAt > FILL_TIMEOUT_MS;

        if (ultrasonicFault) {
            setState(State::Fault, now, "ULTRASONIC_INVALID_FILLING");
        } else if (full) {
            fullTankTimer.reset();
            setState(State::Conditioning, now);
        } else if (timeout) {
            setState(State::Fault, now, "FILL_TIMEOUT");
        }
        break;
    }

    case State::Conditioning: {
        output.aerator = true;
        const bool phFault = phInvalidTimer.update(
            !inputs.phValid, now, PERSISTENCE_MS);
        const bool tdsFault = tdsInvalidTimer.update(
            !inputs.tdsValid, now, PERSISTENCE_MS);
        const bool timeout = now - stateEnteredAt > CONDITIONING_TIMEOUT_MS;

        if (phFault || tdsFault) {
            setState(State::Fault, now,
                     phFault ? "PH_SENSOR_INVALID" : "TDS_SENSOR_INVALID");
            break;
        }

        if (inputs.phValid && inputs.tdsValid) {
            updatePhDemand(inputs.ph);
            updateNutrientDemand(inputs.tdsPpm);

            if (phUpDemand && !inputs.phUpNormal) {
                setState(State::Fault, now, "REAGENT_PH_UP_EMPTY");
            } else if (phDownDemand && !inputs.phDownNormal) {
                setState(State::Fault, now, "REAGENT_PH_DOWN_EMPTY");
            } else if (nutrientDemand &&
                       (!inputs.nutrientANormal || !inputs.nutrientBNormal)) {
                setState(State::Fault, now, "REAGENT_NUTRIENT_EMPTY");
            } else if (phUpDemand) {
                startDosingPulse(DosingPulse::PhUp, now);
            } else if (phDownDemand) {
                startDosingPulse(DosingPulse::PhDown, now);
            } else if (nutrientDemand) {
                startDosingPulse(DosingPulse::Nutrient, now);
            } else if (inputs.ph >= PH_MIN && inputs.ph <= PH_MAX &&
                       inputs.tdsPpm >= targetTds &&
                       inputs.tdsPpm <= targetTds + TDS_HYSTERESIS_DELTA) {
                setState(State::Release, now);
            } else if (timeout) {
                setState(State::Fault, now, "CONDITIONING_TIMEOUT");
            }
        }
        break;
    }

    case State::Release: {
        output.solenoidValve = true;
        output.aerator = true;
        const bool empty = emptyTankTimer.update(
            inputs.distanceValid && inputs.distanceCm >= DISTANCE_LOW,
            now, PERSISTENCE_MS);
        const bool noFlow = flowLossTimer.update(
            !inputs.flowValid || inputs.flowRateLpm <= 0.0f,
            now, PERSISTENCE_MS);
        const bool timeout = now - stateEnteredAt > RELEASE_TIMEOUT_MS;

        if (empty) {
            emptyTankTimer.reset();
            flowLossTimer.reset();
            setState(State::Monitoring, now);
        } else if (noFlow) {
            setState(State::Fault, now, "FLOW_TIMEOUT");
        } else if (timeout) {
            setState(State::Fault, now, "RELEASE_TIMEOUT");
        }
        break;
    }

    case State::Fault:
        if (inputs.resetFault) setState(State::Init, now);
        break;
    }

    if (currentState == State::Conditioning) {
        setPulseOutput(output, now);
        output.aerator = true;
    } else {
        dosingPulse = DosingPulse::None;
        dosingPulseUntil = 0;
    }

    if (currentState == State::Filling) {
        output.waterPump = true;
    } else if (currentState == State::Release) {
        output.solenoidValve = true;
        output.aerator = true;
    } else if (currentState != State::Conditioning) {
        output.waterPump = false;
    }

    if (currentState == State::Init || currentState == State::Monitoring ||
        currentState == State::Fault) {
        output.phUpPump = false;
        output.phDownPump = false;
        output.nutrientPump = false;
        output.solenoidValve = false;
        output.aerator = false;
    }

    updateGrowLight(inputs.lightLux, inputs.lightValid, currentState,
                    timeValid, timeValid ? localTime.tm_hour : -1);
    output.growLight = growLight;
    output.buzzerTemperature = temperatureBuzzer;
    output.buzzerFault = currentState == State::Fault;
    output.state = currentState;
    strlcpy(output.faultReason, faultReason, sizeof(output.faultReason));
    return output;
}

uint8_t actuatorMask(const ActuatorState &state)
{
    uint8_t mask = 0;
    if (state.phUpPump) mask |= 1U << 0;
    if (state.phDownPump) mask |= 1U << 1;
    if (state.nutrientPump) mask |= 1U << 2;
    if (state.waterPump) mask |= 1U << 3;
    if (state.solenoidValve) mask |= 1U << 4;
    if (state.growLight) mask |= 1U << 5;
    if (state.aerator) mask |= 1U << 6;
    return mask;
}

bool applyActuators(const ActuatorState &state)
{
    const uint8_t desired = actuatorMask(state);
    if (!desiredMaskKnown) {
        lastDesiredMask = Relay::getState();
        desiredMaskKnown = true;
    }

    const uint8_t changed = desired ^ lastDesiredMask;
    bool accepted = true;
    for (uint8_t relay = 1; relay <= 8; ++relay) {
        const uint8_t bit = static_cast<uint8_t>(1U << (relay - 1));
        if ((changed & bit) == 0) continue;
        const bool on = (desired & bit) != 0;
        if (!Relay::setRelay(relay, on)) {
            accepted = false;
            continue;
        }
        if (on) lastDesiredMask |= bit;
        else lastDesiredMask &= static_cast<uint8_t>(~bit);
    }
    return accepted;
}

const char *stateName(State state)
{
    switch (state) {
    case State::Init: return "INIT";
    case State::Monitoring: return "MONITORING";
    case State::Filling: return "FILLING";
    case State::Conditioning: return "CONDITIONING";
    case State::Release: return "RELEASE";
    case State::Fault: return "FAULT";
    }
    return "UNKNOWN";
}

} // namespace Logic
