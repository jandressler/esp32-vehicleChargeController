/*
    Name: DanfossTLX-REST        
    
    Used Libraries - output from verbose compile
    Multiple libraries were found for "WiFi.h" Used:       ...\packages\esp32\hardware\esp32\2.0.3\libraries\WiFi
    Using library WiFi at version 2.0.0 in folder:         ...\packages\esp32\hardware\esp32\2.0.3\libraries\WiFi 
    Using library WebServer at version 2.0.0 in folder:    ...\packages\esp32\hardware\esp32\2.0.3\libraries\WebServer 
    Using library ArduinoJson at version 6.19.4 in folder: ...\libraries\ArduinoJson 
    Using library FS at version 2.0.0 in folder:           ...\packages\esp32\hardware\esp32\2.0.3\libraries\FS 
    
*/ 
#include <Arduino.h>
#include <HardwareSerial.h>
#include "DanfossTLX-RS485.h"
#include "ModbusClientRTU.h"
#include "Secrets.h" // #defines of SSID & WIFI_PSWD
#include <WiFi.h>         
#include <WiFiClient.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <math.h>
#include <string.h>

enum class ChargingMode : uint8_t;

#define BLYNK_PRINT Serial
#define RXD2 16
#define TXD2 17
#define GOE_STATUS_URL "http://192.168.2.190/api/status?filter=fwv,typ,alw,alwt,amp,acu,frc,car,err,nrg,lccfc,lcctc,lbp,psm,fsp,lfspt,rbt,mptwt,mpwst,pnp,pwm"
#define GOE_SET_URL "http://192.168.2.190/api/set?"

String Program="DanfossTLX-REST";
String Arduino="1.18.19";
String CodeDate = "2023-02-07";
String Board  = "LOLIN D32 - ESP32 2.0.3";

DanfossTLX MyTLX(RXD2, TXD2);
WebServer server(80);
ModbusClientRTU modbusClient(GPIO_NUM_4);

struct MeterReading {
  float value = 0.0f;
  bool valid = false;
};

const uint16_t meterRegisters[] = {
  0x0400, 0x0402, 0x0404,
  0x0410, 0x0412, 0x0414,
  0x041A, 0x041C, 0x041E,
  0x0420
};
const char *meterNames[] = {
  "voltage_l1", "voltage_l2", "voltage_l3",
  "current_l1", "current_l2", "current_l3",
  "power_l1", "power_l2", "power_l3",
  "power_total"
};
const char *meterUnits[] = {
  "V", "V", "V", "A", "A", "A", "W", "W", "W", "W"
};
constexpr size_t METER_READING_COUNT = sizeof(meterRegisters) / sizeof(meterRegisters[0]);
MeterReading meterReadings[METER_READING_COUNT];
uint32_t meterUpdatedAt = 0;
uint32_t meterPowerLastValidAt = 0;
uint32_t meterPowerErrorCount = 0;
uint32_t meterPowerConsecutiveErrors = 0;
uint8_t tlxReadsSinceMeter = 0;
constexpr uint8_t METER_POLL_EVERY_TLX_READS = 6;
constexpr bool GOE_PV_CONTROL_ENABLED = true;
constexpr uint32_t GOE_PV_MEAN_WINDOW_MS = 60000;
constexpr uint32_t GOE_PV_START_HOLD_MS = 300000;
constexpr uint32_t GOE_PV_CURRENT_COMMAND_INTERVAL_MS = 60000;
constexpr uint32_t GOE_PV_START_STOP_INTERVAL_MS = 300000;
constexpr uint32_t GOE_PV_STOP_HOLD_MS = 300000;
constexpr uint32_t GOE_PV_METER_STALE_MS = 30000;
constexpr uint32_t GOE_PV_SAFETY_STOP_RETRY_MS = 60000;
constexpr uint32_t GOE_MANUAL_TLX_STATUS_MAX_AGE_MS = 90000;
constexpr uint32_t GOE_MANUAL_UNPLUG_CONFIRM_SAMPLES = 2;
constexpr uint32_t GOE_PV_LIMIT_CURRENT_ERROR_COUNT = 3;
constexpr uint32_t GOE_PV_STOP_ERROR_COUNT = 6;
constexpr int GOE_PV_MIN_CURRENT_A = 6;
constexpr int GOE_PV_MAX_CURRENT_A = 16;
constexpr float GOE_PV_PHASE_VOLTAGE_V = 230.0f;
constexpr float GOE_PV_SAFETY_RESERVE_W = 400.0f;
constexpr uint8_t GOE_GRID_POWER_SAMPLE_CAPACITY = 16;

struct GridPowerSample {
  float watts = 0.0f;
  uint32_t sampledAt = 0;
};
GridPowerSample gridPowerSamples[GOE_GRID_POWER_SAMPLE_CAPACITY];
uint8_t gridPowerSampleNext = 0;
uint8_t gridPowerSampleCount = 0;
bool gridPowerMeanValid = false;
float gridPowerMeanW = 0.0f;
bool pvPolicyGridPowerMeanValid = false;
float pvPolicyGridPowerMeanW = 0.0f;
bool pvAvailablePowerMeanValid = false;
float pvAvailablePowerMeanW = 0.0f;
bool pvStartThresholdValid = false;
float pvStartThresholdW = 0.0f;
uint32_t pvStartConditionSince = 0;
uint32_t pvStopConditionSince = 0;
int pvTargetCurrentA = GOE_PV_MIN_CURRENT_A;
const char *pvControlState = "WAITING_FOR_MEASUREMENTS";
const char *pvControlAction = "NONE";
bool pvControllerStateInitialized = false;
bool pvControllerCharging = false;
bool pvStartStopClockInitialized = false;
uint32_t pvLastStartStopCommandAt = 0;
bool pvCurrentCommandClockInitialized = false;
uint32_t pvLastCurrentCommandAt = 0;
bool pvSafetyStopAttemptClockInitialized = false;
uint32_t pvLastSafetyStopAttemptAt = 0;
bool startupSafeStopPending = false;
uint32_t startupSafeStopLastAttemptAt = 0;
bool manualPvReturnArmed = false;
bool manualCarConnectedSeen = false;
uint8_t manualUnplugSampleCount = 0;
uint32_t manualLastCarStateSampleAt = 0;
bool manualPvNightObserved = false;
bool carWasConnected = false;
uint32_t lastCarStateSafetySampleAt = 0;
uint8_t unplugSafetySampleCount = 0;
bool carUnplugStopPending = false;
uint32_t carUnplugStopLastAttemptAt = 0;
const char *pvControlStateTimed = nullptr;
uint32_t pvControlStateSince = 0;

struct GoEStatus {
  bool valid = false;
  bool chargeAllowed = false;
  bool chargeAllowedChangedAtValid = false;
  int64_t chargeAllowedChangedAt = 0;
  int requestedCurrent = 0;
  bool allowedCurrentValid = false;
  int allowedCurrent = 0;
  int forceMode = 0;
  bool phasePreferenceValid = false;
  uint8_t phasePreference = 0;
  bool forceSinglePhaseValid = false;
  bool forceSinglePhase = false;
  bool lastPhaseToggleValid = false;
  int64_t lastPhaseToggleMs = 0;
  bool chargerUptimeValid = false;
  int64_t chargerUptimeMs = 0;
  bool minPhaseToggleWaitValid = false;
  int64_t minPhaseToggleWaitMs = 0;
  bool minPhaseWishTimeValid = false;
  int64_t minPhaseWishTimeMs = 0;
  bool phaseCountValid = false;
  int phaseCount = 0;
  bool phaseWishValid = false;
  int phaseWish = 0;
  bool carStateValid = false;
  int carState = 0;
  int errorCode = 0;
  bool lastChangedFromChargingValid = false;
  int64_t lastChangedFromChargingMs = 0;
  bool lastChangedToChargingValid = false;
  int64_t lastChangedToChargingMs = 0;
  bool lastButtonPressValid = false;
  int64_t lastButtonPressMs = 0;
  int energy[16] = {};
  uint8_t energyCount = 0;
  String chargerType;
  String firmware;
  String error;
  uint32_t updatedAt = 0;
};
GoEStatus goEStatus;
uint32_t lastGoEPoll = 0;
constexpr uint32_t GOE_POLL_INTERVAL_MS = 15000;
constexpr uint32_t GOE_PHASE_REQUEST_STABILITY_MS = 600000;
constexpr uint32_t GOE_PHASE_MIN_COMMAND_INTERVAL_MS = 600000;
constexpr uint32_t GOE_PHASE_STATUS_MAX_AGE_MS = 45000;
constexpr uint32_t METER_STATUS_MAX_AGE_MS = 15000;
constexpr float GOE_THREE_PHASE_SWITCH_ON_EXPORT_W = 5200.0f;
constexpr float GOE_THREE_PHASE_SWITCH_OFF_EXPORT_W = 5000.0f;
bool phaseCommandClockInitialized = false;
uint32_t lastPhaseCommandAt = 0;
bool phaseTargetInitialized = false;
uint8_t phaseTarget = 1;
uint32_t phaseTargetSince = 0;
bool phaseAvailableSurplusValid = false;
float phaseAvailableSurplusW = 0.0f;

enum class ChargingMode : uint8_t {
  Off,
  PvAdaptive,
  ManualPower
};

enum class GoEPhasePreference : uint8_t {
  Auto = 0,
  ForceOnePhase = 1,
  ForceThreePhases = 2
};

enum class GoEPhaseRequestResult : uint8_t {
  CommandSent,
  AlreadySet,
  StatusUnavailable,
  WaitingForChargePermission,
  WaitingForStability,
  WaitingForControllerInterval,
  WaitingForChargerInterval,
  WriteFailed
};

GoEPhaseRequestResult requestGoEPhasePreference(
    uint8_t preference, uint32_t desiredSinceMs, bool allowWhenStopped);

ChargingMode chargingMode = ChargingMode::PvAdaptive;
int manualCurrentA = GOE_PV_MAX_CURRENT_A;
uint8_t manualPhasePreference =
    static_cast<uint8_t>(GoEPhasePreference::Auto);
const char *chargingModeTransitionState = "STARTUP_RECOVERY";
bool chargingModeTransitionPending = false;
uint32_t modeLastCommandAt = 0;
uint32_t modeCommandSentAt = 0;
bool modeCommandClockInitialized = false;
bool modeCommandAwaitingStatus = false;
uint32_t modePhaseRequestedAt = 0;
bool manualStartStopIntervalInitialized = false;
uint32_t manualLastStartStopCommandAt = 0;
bool manualOffConfirmationPending = false;

JsonDocument jsonDocument;
char jsonbuffer[3072];

const char *carStateText(int state) {
  switch (state) {
    case 0: return "Unknown/Error";
    case 1: return "Idle";
    case 2: return "Charging";
    case 3: return "WaitCar";
    case 4: return "Complete";
    case 5: return "Error";
    case 6: return "Initializing";
    default: return "Unrecognized";
  }
}

const char *phasePreferenceText(uint8_t preference) {
  switch (static_cast<GoEPhasePreference>(preference)) {
    case GoEPhasePreference::Auto: return "Auto";
    case GoEPhasePreference::ForceOnePhase: return "Force_1";
    case GoEPhasePreference::ForceThreePhases: return "Force_3";
    default: return "Unrecognized";
  }
}

const char *chargingModeText(ChargingMode mode) {
  switch (mode) {
    case ChargingMode::Off: return "OFF";
    case ChargingMode::PvAdaptive: return "PV_ADAPTIVE";
    case ChargingMode::ManualPower: return "START";
    default: return "UNRECOGNIZED";
  }
}

bool requestChargingMode(
    ChargingMode mode, int requestedCurrentA = GOE_PV_MIN_CURRENT_A,
    uint8_t requestedPhasePreference =
        static_cast<uint8_t>(GoEPhasePreference::Auto)) {
  switch (mode) {
    case ChargingMode::Off:
    case ChargingMode::PvAdaptive:
      break;
    case ChargingMode::ManualPower:
      if (requestedCurrentA < GOE_PV_MIN_CURRENT_A ||
          requestedCurrentA > GOE_PV_MAX_CURRENT_A ||
          requestedPhasePreference >
              static_cast<uint8_t>(GoEPhasePreference::ForceThreePhases)) {
        Serial.printf(
            "charging mode request rejected: invalid manual settings %dA, psm=%u\n",
            requestedCurrentA, requestedPhasePreference);
        chargingModeTransitionState = "INVALID_MANUAL_SETTINGS";
        chargingModeTransitionPending = false;
        return false;
      }
      manualCurrentA = requestedCurrentA;
      manualPhasePreference = requestedPhasePreference;
      break;
    default:
      Serial.println("charging mode request rejected: unknown mode");
      chargingModeTransitionState = "INVALID_MODE";
      chargingModeTransitionPending = false;
      return false;
  }

  if (chargingMode != mode) {
    Serial.printf("charging mode requested: %s\n", chargingModeText(mode));
    if (mode == ChargingMode::ManualPower && !manualPvReturnArmed) {
      manualPvReturnArmed = true;
      manualCarConnectedSeen =
          goEStatus.valid && goEStatus.carStateValid &&
          millis() - goEStatus.updatedAt <= GOE_PHASE_STATUS_MAX_AGE_MS &&
          goEStatus.carState >= 2 && goEStatus.carState <= 4;
      manualUnplugSampleCount = 0;
      manualLastCarStateSampleAt = 0;
      manualPvNightObserved = false;
    }
    if (mode == ChargingMode::Off &&
        chargingMode == ChargingMode::ManualPower &&
        manualStartStopIntervalInitialized) {
      manualOffConfirmationPending = true;
    }
    chargingMode = mode;
    if (mode == ChargingMode::PvAdaptive) {
      pvControllerStateInitialized = false;
      pvStartConditionSince = 0;
      pvStopConditionSince = 0;
      manualPvReturnArmed = false;
      manualCarConnectedSeen = false;
      manualUnplugSampleCount = 0;
      manualLastCarStateSampleAt = 0;
      manualPvNightObserved = false;
    }
  }
  modeCommandAwaitingStatus = false;
  modeCommandClockInitialized = false;
  modePhaseRequestedAt = millis();
  chargingModeTransitionState = "PENDING";
  chargingModeTransitionPending = true;
  return true;
}

uint32_t manualStartStopWaitRemainingMs() {
  if (chargingMode != ChargingMode::ManualPower ||
      !manualStartStopIntervalInitialized || !goEStatus.valid ||
      goEStatus.forceMode == 0) {
    return 0;
  }
  const uint32_t elapsed = millis() - manualLastStartStopCommandAt;
  if (elapsed >= GOE_PV_START_STOP_INTERVAL_MS) {
    return 0;
  }
  return GOE_PV_START_STOP_INTERVAL_MS - elapsed;
}

void updateNonAdaptiveChargingMode() {
  const uint32_t now = millis();
  const bool goEStatusFresh =
      goEStatus.valid && now - goEStatus.updatedAt <= GOE_PHASE_STATUS_MAX_AGE_MS;
  if (chargingMode == ChargingMode::Off) {
    pvControllerCharging = false;
    if (goEStatusFresh && goEStatus.forceMode == 1 &&
        !goEStatus.chargeAllowed) {
      if (manualOffConfirmationPending) {
        manualLastStartStopCommandAt = now;
        manualOffConfirmationPending = false;
      }
      chargingModeTransitionState = "ACTIVE";
      chargingModeTransitionPending = false;
      modeCommandAwaitingStatus = false;
      return;
    }
    if (!modeCommandClockInitialized ||
        now - modeLastCommandAt >= GOE_PV_SAFETY_STOP_RETRY_MS) {
      modeLastCommandAt = now;
      modeCommandClockInitialized = true;
      if (writeGoEForceMode(1)) {
        if (manualOffConfirmationPending) {
          manualLastStartStopCommandAt = now;
          manualOffConfirmationPending = false;
        }
        pvLastStartStopCommandAt = now;
        pvStartStopClockInitialized = true;
        chargingModeTransitionState = "WAITING_FOR_OFF_CONFIRMATION";
      } else {
        chargingModeTransitionState = "OFF_COMMAND_FAILED";
      }
    }
    return;
  }

  if (!goEStatusFresh) {
    chargingModeTransitionState = "WAITING_FOR_FRESH_GO_E_STATUS";
    modeCommandAwaitingStatus = false;
    if (!modeCommandClockInitialized ||
        now - modeLastCommandAt >= GOE_PV_SAFETY_STOP_RETRY_MS) {
      modeLastCommandAt = now;
      modeCommandClockInitialized = true;
      if (writeGoEForceMode(1)) {
        pvLastStartStopCommandAt = now;
        pvStartStopClockInitialized = true;
        chargingModeTransitionState = "STOPPING_FOR_STALE_GO_E_STATUS";
      }
    }
    return;
  }
  if (goEStatus.forceMode == 2) {
    chargingModeTransitionState = "BLOCKED_BY_EXTERNAL_FORCE_ON";
    modeCommandAwaitingStatus = false;
    return;
  }
  if (modeCommandAwaitingStatus) {
    if (static_cast<int32_t>(goEStatus.updatedAt - modeCommandSentAt) <= 0) {
      chargingModeTransitionState = "WAITING_FOR_GO_E_CONFIRMATION";
      return;
    }
    modeCommandAwaitingStatus = false;
  }

  const int requestedCurrent = manualCurrentA;

  if (goEStatus.requestedCurrent != requestedCurrent) {
    if (modeCommandClockInitialized &&
        now - modeLastCommandAt < GOE_POLL_INTERVAL_MS) {
      return;
    }
    modeLastCommandAt = now;
    modeCommandClockInitialized = true;
    if (writeGoERequestedCurrent(requestedCurrent)) {
      modeCommandAwaitingStatus = true;
      modeCommandSentAt = now;
      chargingModeTransitionState = "WAITING_FOR_CURRENT_CONFIRMATION";
    } else {
      chargingModeTransitionState = "CURRENT_COMMAND_FAILED";
    }
    return;
  }

  if (goEStatus.forceMode != 0) {
    if (manualStartStopIntervalInitialized &&
        now - manualLastStartStopCommandAt <
            GOE_PV_START_STOP_INTERVAL_MS) {
      chargingModeTransitionState = "WAITING_FOR_PREVIOUS_MANUAL_STOP_INTERVAL";
      return;
    }
    if (modeCommandClockInitialized &&
        now - modeLastCommandAt < GOE_POLL_INTERVAL_MS) {
      return;
    }
    modeLastCommandAt = now;
    modeCommandClockInitialized = true;
    if (writeGoEForceMode(0)) {
      pvLastStartStopCommandAt = now;
      pvStartStopClockInitialized = true;
      manualLastStartStopCommandAt = now;
      manualStartStopIntervalInitialized = true;
      modeCommandAwaitingStatus = true;
      modeCommandSentAt = now;
      chargingModeTransitionState = "WAITING_FOR_CHARGE_PERMISSION";
    } else {
      chargingModeTransitionState = "CHARGE_ENABLE_FAILED";
    }
    return;
  }
  if (!goEStatus.chargeAllowed) {
    chargingModeTransitionState = "WAITING_FOR_CHARGER_PERMISSION";
    return;
  }

  if (!goEStatus.phasePreferenceValid) {
    chargingModeTransitionState = "WAITING_FOR_PHASE_STATUS";
    return;
  }
  if (goEStatus.phasePreference != manualPhasePreference) {
    const GoEPhaseRequestResult phaseRequest = requestGoEPhasePreference(
        manualPhasePreference, modePhaseRequestedAt, false);
    switch (phaseRequest) {
      case GoEPhaseRequestResult::CommandSent:
        modeLastCommandAt = now;
        modeCommandClockInitialized = true;
        modeCommandAwaitingStatus = true;
        modeCommandSentAt = now;
        chargingModeTransitionState = "WAITING_FOR_PHASE_CONFIRMATION";
        break;
      case GoEPhaseRequestResult::AlreadySet:
        chargingModeTransitionState = "ACTIVE";
        chargingModeTransitionPending = false;
        break;
      case GoEPhaseRequestResult::StatusUnavailable:
        chargingModeTransitionState = "WAITING_FOR_PHASE_STATUS";
        break;
      case GoEPhaseRequestResult::WaitingForChargePermission:
        chargingModeTransitionState =
            "WAITING_FOR_CHARGE_PERMISSION_BEFORE_PHASE_CHANGE";
        break;
      case GoEPhaseRequestResult::WaitingForStability:
        chargingModeTransitionState = "WAITING_FOR_PHASE_STABILITY";
        break;
      case GoEPhaseRequestResult::WaitingForControllerInterval:
        chargingModeTransitionState = "WAITING_FOR_PHASE_COMMAND_INTERVAL";
        break;
      case GoEPhaseRequestResult::WaitingForChargerInterval:
        chargingModeTransitionState = "WAITING_FOR_CHARGER_PHASE_INTERVAL";
        break;
      case GoEPhaseRequestResult::WriteFailed:
        chargingModeTransitionState = "PHASE_COMMAND_FAILED";
        break;
    }
    return;
  }

  pvControllerCharging = goEStatus.forceMode == 0 && goEStatus.chargeAllowed;
  chargingModeTransitionState = "ACTIVE";
  chargingModeTransitionPending = false;
}

void pollGoEStatus() {
  const uint32_t pollStartedAt = millis();
  HTTPClient http;
  http.setConnectTimeout(1000);
  http.setTimeout(800);
  if (!http.begin(GOE_STATUS_URL)) {
    goEStatus.valid = false;
    goEStatus.error = "Unable to initialize HTTP request";
    Serial.printf("go-e status failed after %lu ms: unable to initialize HTTP request\n",
                  static_cast<unsigned long>(millis() - pollStartedAt));
    return;
  }

  const int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    goEStatus.valid = false;
    goEStatus.error = httpCode > 0
        ? "HTTP status " + String(httpCode)
        : http.errorToString(httpCode);
    Serial.printf("go-e status failed after %lu ms: %s\n",
                  static_cast<unsigned long>(millis() - pollStartedAt),
                  goEStatus.error.c_str());
    http.end();
    return;
  }

  const String payload = http.getString();
  http.end();
  JsonDocument response;
  const DeserializationError parseError = deserializeJson(response, payload);
  if (parseError) {
    goEStatus.valid = false;
    goEStatus.error = String("Invalid JSON: ") + parseError.c_str();
    Serial.printf("go-e status failed after %lu ms: %s\n",
                  static_cast<unsigned long>(millis() - pollStartedAt),
                  goEStatus.error.c_str());
    return;
  }

  const JsonVariantConst allowedCurrent = response["acu"];
  const JsonVariantConst carState = response["car"];
  const JsonVariantConst chargeAllowedChangedAt = response["alwt"];
  const JsonVariantConst lastChangedFromCharging = response["lccfc"];
  const JsonVariantConst lastChangedToCharging = response["lcctc"];
  const JsonVariantConst lastButtonPress = response["lbp"];
  const JsonVariantConst phasePreference = response["psm"];
  const JsonVariantConst forceSinglePhase = response["fsp"];
  const JsonVariantConst lastPhaseToggle = response["lfspt"];
  const JsonVariantConst chargerUptime = response["rbt"];
  const JsonVariantConst minPhaseToggleWait = response["mptwt"];
  const JsonVariantConst minPhaseWishTime = response["mpwst"];
  const JsonVariantConst phaseCount = response["pnp"];
  const JsonVariantConst phaseWish = response["pwm"];
  if (!response["fwv"].is<const char *>() ||
      !response["typ"].is<const char *>() ||
      !response["alw"].is<bool>() ||
      !(chargeAllowedChangedAt.isNull() || chargeAllowedChangedAt.is<int64_t>()) ||
      !response["amp"].is<int>() ||
      !(allowedCurrent.isNull() || allowedCurrent.is<int>()) ||
      !response["frc"].is<int>() ||
      !(carState.isNull() || carState.is<int>()) ||
      !response["err"].is<int>() ||
      !response["nrg"].is<JsonArray>() ||
      response["nrg"].as<JsonArray>().size() > 16 ||
      !(lastChangedFromCharging.isNull() || lastChangedFromCharging.is<int64_t>()) ||
      !(lastChangedToCharging.isNull() || lastChangedToCharging.is<int64_t>()) ||
      !(lastButtonPress.isNull() || lastButtonPress.is<int64_t>())) {
    goEStatus.valid = false;
    goEStatus.error = "Required status field missing or has wrong type";
    Serial.printf("go-e status failed after %lu ms: %s\n",
                  static_cast<unsigned long>(millis() - pollStartedAt),
                  goEStatus.error.c_str());
    return;
  }

  goEStatus.firmware = response["fwv"].as<String>();
  goEStatus.chargerType = response["typ"].as<String>();
  goEStatus.chargeAllowed = response["alw"].as<bool>();
  goEStatus.chargeAllowedChangedAtValid = !chargeAllowedChangedAt.isNull();
  goEStatus.chargeAllowedChangedAt = goEStatus.chargeAllowedChangedAtValid
      ? chargeAllowedChangedAt.as<int64_t>()
      : 0;
  goEStatus.requestedCurrent = response["amp"].as<int>();
  goEStatus.allowedCurrentValid = !allowedCurrent.isNull();
  goEStatus.allowedCurrent = goEStatus.allowedCurrentValid
      ? allowedCurrent.as<int>()
      : 0;
  goEStatus.forceMode = response["frc"].as<int>();
  goEStatus.phasePreferenceValid = phasePreference.is<uint8_t>();
  goEStatus.phasePreference = goEStatus.phasePreferenceValid
      ? phasePreference.as<uint8_t>()
      : 0;
  goEStatus.forceSinglePhaseValid = forceSinglePhase.is<bool>();
  goEStatus.forceSinglePhase = goEStatus.forceSinglePhaseValid
      ? forceSinglePhase.as<bool>()
      : false;
  goEStatus.lastPhaseToggleValid = lastPhaseToggle.is<int64_t>();
  goEStatus.lastPhaseToggleMs = goEStatus.lastPhaseToggleValid
      ? lastPhaseToggle.as<int64_t>()
      : 0;
  goEStatus.chargerUptimeValid = chargerUptime.is<int64_t>();
  goEStatus.chargerUptimeMs = goEStatus.chargerUptimeValid
      ? chargerUptime.as<int64_t>()
      : 0;
  goEStatus.minPhaseToggleWaitValid = minPhaseToggleWait.is<int64_t>();
  goEStatus.minPhaseToggleWaitMs = goEStatus.minPhaseToggleWaitValid
      ? minPhaseToggleWait.as<int64_t>()
      : 0;
  goEStatus.minPhaseWishTimeValid = minPhaseWishTime.is<int64_t>();
  goEStatus.minPhaseWishTimeMs = goEStatus.minPhaseWishTimeValid
      ? minPhaseWishTime.as<int64_t>()
      : 0;
  goEStatus.phaseCountValid = phaseCount.is<int>();
  goEStatus.phaseCount = goEStatus.phaseCountValid ? phaseCount.as<int>() : 0;
  goEStatus.phaseWishValid = phaseWish.is<int>();
  goEStatus.phaseWish = goEStatus.phaseWishValid ? phaseWish.as<int>() : 0;
  goEStatus.carStateValid = !carState.isNull();
  goEStatus.carState = goEStatus.carStateValid ? carState.as<int>() : 0;
  goEStatus.errorCode = response["err"].as<int>();
  goEStatus.lastChangedFromChargingValid = !lastChangedFromCharging.isNull();
  goEStatus.lastChangedFromChargingMs = goEStatus.lastChangedFromChargingValid
      ? lastChangedFromCharging.as<int64_t>()
      : 0;
  goEStatus.lastChangedToChargingValid = !lastChangedToCharging.isNull();
  goEStatus.lastChangedToChargingMs = goEStatus.lastChangedToChargingValid
      ? lastChangedToCharging.as<int64_t>()
      : 0;
  goEStatus.lastButtonPressValid = !lastButtonPress.isNull();
  goEStatus.lastButtonPressMs = goEStatus.lastButtonPressValid
      ? lastButtonPress.as<int64_t>()
      : 0;
  const JsonArray energy = response["nrg"].as<JsonArray>();
  goEStatus.energyCount = energy.size();
  for (uint8_t i = 0; i < goEStatus.energyCount; ++i) {
    goEStatus.energy[i] = energy[i].as<int>();
  }
  goEStatus.valid = true;
  goEStatus.error = "";
  goEStatus.updatedAt = millis();

  static bool buttonPressBaselineValid = false;
  static int64_t previousButtonPressMs = 0;
  static bool previousChargerUptimeValid = false;
  static int64_t previousChargerUptimeMs = 0;
  bool buttonPressDetected = false;
  if (goEStatus.lastButtonPressValid) {
    const bool chargerRestarted =
        previousChargerUptimeValid && goEStatus.chargerUptimeValid &&
        goEStatus.chargerUptimeMs < previousChargerUptimeMs;
    if (buttonPressBaselineValid &&
        !chargerRestarted &&
        goEStatus.lastButtonPressMs > previousButtonPressMs) {
      buttonPressDetected = true;
    }
    previousButtonPressMs = goEStatus.lastButtonPressMs;
    buttonPressBaselineValid = true;
  } else {
    buttonPressBaselineValid = false;
  }
  previousChargerUptimeValid = goEStatus.chargerUptimeValid;
  previousChargerUptimeMs = goEStatus.chargerUptimeMs;
  if (buttonPressDetected) {
    Serial.printf(
        "go-e button press detected: lbp=%lld; requesting 16A %s start\n",
        static_cast<long long>(goEStatus.lastButtonPressMs),
        phasePreferenceText(manualPhasePreference));
    requestChargingMode(
        ChargingMode::ManualPower, GOE_PV_MAX_CURRENT_A,
        manualPhasePreference);
  }

  static bool previousSampleValid = false;
  static bool previousChargeAllowed = false;
  static int previousRequestedCurrent = 0;
  static int64_t previousChargeAllowedChangedAt = 0;
  if (previousSampleValid) {
    if (goEStatus.chargeAllowed != previousChargeAllowed ||
        (goEStatus.chargeAllowedChangedAtValid &&
         goEStatus.chargeAllowedChangedAt != previousChargeAllowedChangedAt)) {
      Serial.printf("go-e charge permission changed: allowed=%s alwt=",
                    goEStatus.chargeAllowed ? "yes" : "no");
      if (goEStatus.chargeAllowedChangedAtValid) {
        Serial.printf("%lld\n", static_cast<long long>(goEStatus.chargeAllowedChangedAt));
      } else {
        Serial.println("n/a");
      }
    }
    if (goEStatus.requestedCurrent != previousRequestedCurrent) {
      Serial.printf("go-e requested current changed: %dA -> %dA\n",
                    previousRequestedCurrent, goEStatus.requestedCurrent);
    }
  }
  previousSampleValid = true;
  previousChargeAllowed = goEStatus.chargeAllowed;
  previousRequestedCurrent = goEStatus.requestedCurrent;
  previousChargeAllowedChangedAt = goEStatus.chargeAllowedChangedAt;

  Serial.printf("go-e t=%lu state=", static_cast<unsigned long>(goEStatus.updatedAt));
  if (goEStatus.carStateValid) {
    Serial.printf("%s(%d)", carStateText(goEStatus.carState), goEStatus.carState);
  } else {
    Serial.print("Unavailable");
  }
  Serial.printf(" allowed=%s requested=%dA actual=",
                goEStatus.chargeAllowed ? "yes" : "no",
                goEStatus.requestedCurrent);
  if (goEStatus.allowedCurrentValid) {
    Serial.printf("%dA", goEStatus.allowedCurrent);
  } else {
    Serial.print("n/a");
  }
  Serial.printf(" force=%d error=%d http_ms=%lu ",
                goEStatus.forceMode, goEStatus.errorCode,
                static_cast<unsigned long>(millis() - pollStartedAt));
  Serial.print("alwt=");
  if (goEStatus.chargeAllowedChangedAtValid) {
    Serial.printf("%lld", static_cast<long long>(goEStatus.chargeAllowedChangedAt));
  } else {
    Serial.print("n/a");
  }
  Serial.print(" last_from_charge_ms=");
  if (goEStatus.lastChangedFromChargingValid) {
    Serial.printf("%lld", static_cast<long long>(goEStatus.lastChangedFromChargingMs));
  } else {
    Serial.print("n/a");
  }
  Serial.print(" last_to_charge_ms=");
  if (goEStatus.lastChangedToChargingValid) {
    Serial.printf("%lld", static_cast<long long>(goEStatus.lastChangedToChargingMs));
  } else {
    Serial.print("n/a");
  }
  Serial.print(" nrg=[");
  for (uint8_t i = 0; i < goEStatus.energyCount; ++i) {
    Serial.printf("%s%d", i ? "," : "", goEStatus.energy[i]);
  }
  Serial.println("]");
}

void returnToPvAdaptiveMode(const char *reason) {
  if (chargingMode == ChargingMode::PvAdaptive || !manualPvReturnArmed) {
    return;
  }
  Serial.printf("automatic return to PV-Adaptiv: %s\n", reason);
  requestChargingMode(ChargingMode::PvAdaptive);
}

void updateManualModeAutomaticReturn() {
  if (!manualPvReturnArmed ||
      (chargingMode != ChargingMode::ManualPower &&
       chargingMode != ChargingMode::Off)) {
    return;
  }

  const uint32_t now = millis();
  const bool goEStatusFresh =
      goEStatus.valid &&
      now - goEStatus.updatedAt <= GOE_PHASE_STATUS_MAX_AGE_MS;
  if (goEStatusFresh &&
      goEStatus.updatedAt != manualLastCarStateSampleAt) {
    manualLastCarStateSampleAt = goEStatus.updatedAt;
    if (goEStatus.carStateValid) {
      if (goEStatus.carState >= 2 && goEStatus.carState <= 4) {
        manualCarConnectedSeen = true;
        manualUnplugSampleCount = 0;
      } else if (goEStatus.carState == 1 && manualCarConnectedSeen) {
        if (manualUnplugSampleCount < GOE_MANUAL_UNPLUG_CONFIRM_SAMPLES) {
          ++manualUnplugSampleCount;
        }
      } else {
        manualUnplugSampleCount = 0;
      }
    }
  }
  if (manualUnplugSampleCount >= GOE_MANUAL_UNPLUG_CONFIRM_SAMPLES) {
    returnToPvAdaptiveMode("vehicle disconnected");
    return;
  }

  if (!MyTLX.TLX.Valid[DanfossTLX::OpMode] ||
      now - MyTLX.TLX.UpdatedAt[DanfossTLX::OpMode] >
          GOE_MANUAL_TLX_STATUS_MAX_AGE_MS) {
    return;
  }

  if (MyTLX.TLXMode == DanfossTLX::OFFGRIDOFF) {
    manualPvNightObserved = true;
    return;
  }

  if (manualPvNightObserved &&
      MyTLX.TLXMode == DanfossTLX::ONGRID) {
    returnToPvAdaptiveMode("inverter returned on grid after night");
  }
}

bool writeGoEPhasePreference(uint8_t preference) {
  if (preference > static_cast<uint8_t>(GoEPhasePreference::ForceThreePhases)) {
    Serial.printf("go-e phase preference rejected: invalid value %u\n", preference);
    return false;
  }

  HTTPClient http;
  http.setConnectTimeout(1000);
  http.setTimeout(800);
  const String url = String(GOE_SET_URL) + "psm=" + String(preference);
  if (!http.begin(url)) {
    Serial.println("go-e phase preference write failed: unable to initialize HTTP request");
    return false;
  }

  const int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    Serial.printf("go-e phase preference write failed: HTTP %d\n", httpCode);
    http.end();
    return false;
  }

  const String payload = http.getString();
  http.end();
  JsonDocument response;
  const DeserializationError parseError = deserializeJson(response, payload);
  if (parseError || !response["psm"].is<bool>() || !response["psm"].as<bool>()) {
    Serial.printf("go-e phase preference write rejected: %s\n", payload.c_str());
    return false;
  }

  Serial.printf("go-e phase preference write accepted: psm=%u\n", preference);
  return true;
}

bool writeGoERequestedCurrent(int currentA) {
  if (currentA < GOE_PV_MIN_CURRENT_A || currentA > GOE_PV_MAX_CURRENT_A) {
    Serial.printf("go-e current write rejected: invalid value %dA\n", currentA);
    return false;
  }

  HTTPClient http;
  http.setConnectTimeout(1000);
  http.setTimeout(800);
  const String url = String(GOE_SET_URL) + "amp=" + String(currentA);
  if (!http.begin(url)) {
    Serial.println("go-e current write failed: unable to initialize HTTP request");
    return false;
  }

  const int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    Serial.printf("go-e current write failed: HTTP %d\n", httpCode);
    http.end();
    return false;
  }

  const String payload = http.getString();
  http.end();
  JsonDocument response;
  const DeserializationError parseError = deserializeJson(response, payload);
  if (parseError || !response["amp"].is<bool>() || !response["amp"].as<bool>()) {
    Serial.printf("go-e current write rejected: %s\n", payload.c_str());
    return false;
  }

  Serial.printf("go-e requested current write accepted: amp=%dA\n", currentA);
  return true;
}

bool writeGoEForceMode(uint8_t forceMode) {
  if (forceMode > 1) {
    Serial.printf("go-e force-mode write rejected: invalid value %u\n", forceMode);
    return false;
  }

  HTTPClient http;
  http.setConnectTimeout(1000);
  http.setTimeout(800);
  const String url = String(GOE_SET_URL) + "frc=" + String(forceMode);
  if (!http.begin(url)) {
    Serial.println("go-e force-mode write failed: unable to initialize HTTP request");
    return false;
  }

  const int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    Serial.printf("go-e force-mode write failed: HTTP %d\n", httpCode);
    http.end();
    return false;
  }

  const String payload = http.getString();
  http.end();
  JsonDocument response;
  const DeserializationError parseError = deserializeJson(response, payload);
  if (parseError || !response["frc"].is<bool>() || !response["frc"].as<bool>()) {
    Serial.printf("go-e force-mode write rejected: %s\n", payload.c_str());
    return false;
  }

  Serial.printf("go-e force-mode write accepted: frc=%u\n", forceMode);
  return true;
}

GoEPhaseRequestResult requestGoEPhasePreference(
    uint8_t preference, uint32_t desiredSinceMs, bool allowWhenStopped) {
  if (preference > static_cast<uint8_t>(GoEPhasePreference::ForceThreePhases) ||
      !goEStatus.valid || !goEStatus.phasePreferenceValid ||
      millis() - goEStatus.updatedAt > GOE_PHASE_STATUS_MAX_AGE_MS) {
    return GoEPhaseRequestResult::StatusUnavailable;
  }
  if (goEStatus.phasePreference == preference) {
    return GoEPhaseRequestResult::AlreadySet;
  }
  const bool chargerStopped = goEStatus.forceMode == 1 &&
      !goEStatus.chargeAllowed;
  if ((!goEStatus.chargeAllowed || goEStatus.forceMode != 0) &&
      !(allowWhenStopped && chargerStopped)) {
    return GoEPhaseRequestResult::WaitingForChargePermission;
  }

  const uint32_t now = millis();
  const int64_t phaseWishStableMs = goEStatus.minPhaseWishTimeValid
      ? goEStatus.minPhaseWishTimeMs
      : 0;
  const uint32_t reportedWishStableMs = phaseWishStableMs > 0
      ? static_cast<uint32_t>(phaseWishStableMs)
      : 0;
  const uint32_t stableTimeMs = reportedWishStableMs > GOE_PHASE_REQUEST_STABILITY_MS
      ? reportedWishStableMs
      : GOE_PHASE_REQUEST_STABILITY_MS;
  if (now - desiredSinceMs < stableTimeMs ||
      (phaseCommandClockInitialized &&
       now - lastPhaseCommandAt < GOE_PHASE_MIN_COMMAND_INTERVAL_MS)) {
    return now - desiredSinceMs < stableTimeMs
        ? GoEPhaseRequestResult::WaitingForStability
        : GoEPhaseRequestResult::WaitingForControllerInterval;
  }

  if (goEStatus.lastPhaseToggleValid && goEStatus.chargerUptimeValid &&
      goEStatus.chargerUptimeMs >= goEStatus.lastPhaseToggleMs) {
    const int64_t elapsedSinceToggleMs =
        goEStatus.chargerUptimeMs - goEStatus.lastPhaseToggleMs;
    const int64_t configuredWaitMs = goEStatus.minPhaseToggleWaitValid
        ? goEStatus.minPhaseToggleWaitMs
        : 0;
    const uint32_t reportedMinimumWaitMs = configuredWaitMs > 0
        ? static_cast<uint32_t>(configuredWaitMs)
        : 0;
    const uint32_t minimumWaitMs = reportedMinimumWaitMs > GOE_PHASE_MIN_COMMAND_INTERVAL_MS
        ? reportedMinimumWaitMs
        : GOE_PHASE_MIN_COMMAND_INTERVAL_MS;
    if (elapsedSinceToggleMs < minimumWaitMs) {
      return GoEPhaseRequestResult::WaitingForChargerInterval;
    }
  }

  if (!writeGoEPhasePreference(preference)) {
    return GoEPhaseRequestResult::WriteFailed;
  }
  lastPhaseCommandAt = now;
  phaseCommandClockInitialized = true;
  return GoEPhaseRequestResult::CommandSent;
}

void updateGoEPhasePolicy(ChargingMode mode) {
  phaseAvailableSurplusValid = false;
  if (startupSafeStopPending) {
    phaseTargetInitialized = false;
    return;
  }
  if (mode == ChargingMode::ManualPower) {
    return;
  }
  if (!goEStatus.valid || !goEStatus.phasePreferenceValid ||
      millis() - goEStatus.updatedAt > GOE_PHASE_STATUS_MAX_AGE_MS) {
    phaseTargetInitialized = false;
    return;
  }

  uint8_t requestedPreference = manualPhasePreference;
  if (mode == ChargingMode::Off &&
      manualPhasePreference == static_cast<uint8_t>(GoEPhasePreference::Auto)) {
    requestedPreference = static_cast<uint8_t>(GoEPhasePreference::Auto);
  } else if (mode == ChargingMode::PvAdaptive &&
             manualPhasePreference ==
                 static_cast<uint8_t>(GoEPhasePreference::Auto)) {
    const MeterReading &gridPower = meterReadings[METER_READING_COUNT - 1];
    if (!gridPower.valid || millis() - meterUpdatedAt > METER_STATUS_MAX_AGE_MS) {
      phaseTargetInitialized = false;
      return;
    }

    if (goEStatus.energyCount < 10) {
      phaseTargetInitialized = false;
      return;
    }
    float availableSurplusW = -gridPower.value +
                              abs(goEStatus.energy[11]);
    availableSurplusW = max(0.0f, availableSurplusW);
    phaseAvailableSurplusW = availableSurplusW;
    phaseAvailableSurplusValid = true;

    if (goEStatus.phasePreference ==
        static_cast<uint8_t>(GoEPhasePreference::ForceThreePhases)) {
      requestedPreference = availableSurplusW < GOE_THREE_PHASE_SWITCH_OFF_EXPORT_W
          ? static_cast<uint8_t>(GoEPhasePreference::ForceOnePhase)
          : static_cast<uint8_t>(GoEPhasePreference::ForceThreePhases);
    } else {
      requestedPreference = availableSurplusW >= GOE_THREE_PHASE_SWITCH_ON_EXPORT_W
          ? static_cast<uint8_t>(GoEPhasePreference::ForceThreePhases)
          : static_cast<uint8_t>(GoEPhasePreference::ForceOnePhase);
    }
  }

  const uint32_t now = millis();
  if (!phaseTargetInitialized || phaseTarget != requestedPreference) {
    phaseTarget = requestedPreference;
    phaseTargetSince = now;
    phaseTargetInitialized = true;
    Serial.printf("go-e phase target changed: %s\n",
                  phasePreferenceText(phaseTarget));
  }

  if (phaseTarget != goEStatus.phasePreference) {
    requestGoEPhasePreference(
        phaseTarget, phaseTargetSince, goEStatus.forceMode == 1);
  }
}

void updateGridPowerMean() {
  const uint32_t now = millis();
  float sum = 0.0f;
  uint8_t count = 0;
  for (uint8_t i = 0; i < gridPowerSampleCount; ++i) {
    const GridPowerSample &sample = gridPowerSamples[i];
    if (now - sample.sampledAt <= GOE_PV_MEAN_WINDOW_MS) {
      sum += sample.watts;
      ++count;
    }
  }
  gridPowerMeanValid = count > 0;
  if (gridPowerMeanValid) {
    gridPowerMeanW = sum / count;
  }

}

void updatePvControlStateTimer() {
  if (pvControlStateTimed != pvControlState) {
    pvControlStateTimed = pvControlState;
    pvControlStateSince = millis();
  }
}

uint32_t pvControlStateDurationMs() {
  return pvControlStateTimed == pvControlState
      ? millis() - pvControlStateSince
      : 0;
}

bool pvStartWaitRemainingMs(uint32_t &remainingMs) {
  if (pvStartConditionSince == 0 ||
      (strcmp(pvControlState, "START_RESERVE_HOLD") != 0 &&
       strcmp(pvControlState, "START_WAITING_FOR_MIN_INTERVAL") != 0)) {
    return false;
  }

  const uint32_t now = millis();
  const uint32_t holdElapsedMs = now - pvStartConditionSince;
  const uint32_t holdRemainingMs = holdElapsedMs < GOE_PV_START_HOLD_MS
      ? GOE_PV_START_HOLD_MS - holdElapsedMs
      : 0;
  const uint32_t startStopElapsedMs = now - pvLastStartStopCommandAt;
  const uint32_t startStopRemainingMs =
      pvStartStopClockInitialized &&
              startStopElapsedMs < GOE_PV_START_STOP_INTERVAL_MS
          ? GOE_PV_START_STOP_INTERVAL_MS - startStopElapsedMs
          : 0;
  remainingMs = max(holdRemainingMs, startStopRemainingMs);
  return true;
}

uint8_t getGoEActivePhaseCount() {
  if (goEStatus.forceSinglePhaseValid && goEStatus.forceSinglePhase) {
    return 1;
  }
  if (goEStatus.phasePreferenceValid &&
      goEStatus.phasePreference ==
          static_cast<uint8_t>(GoEPhasePreference::ForceThreePhases)) {
    return 3;
  }
  if (goEStatus.phasePreferenceValid &&
      goEStatus.phasePreference ==
          static_cast<uint8_t>(GoEPhasePreference::ForceOnePhase)) {
    return 1;
  }
  return goEStatus.phaseCountValid && goEStatus.phaseCount == 3 ? 3 : 1;
}

void updateCarUnplugSafety() {
  const uint32_t now = millis();
  if (!goEStatus.valid ||
      now - goEStatus.updatedAt > GOE_PHASE_STATUS_MAX_AGE_MS) {
    return;
  }

  if (goEStatus.updatedAt != lastCarStateSafetySampleAt) {
    lastCarStateSafetySampleAt = goEStatus.updatedAt;
    if (goEStatus.carStateValid &&
        goEStatus.carState >= 2 && goEStatus.carState <= 4) {
      carWasConnected = true;
      unplugSafetySampleCount = 0;
    } else if (goEStatus.carStateValid && goEStatus.carState == 1 &&
               carWasConnected) {
      if (unplugSafetySampleCount < GOE_MANUAL_UNPLUG_CONFIRM_SAMPLES) {
        ++unplugSafetySampleCount;
      }
      if (unplugSafetySampleCount >= GOE_MANUAL_UNPLUG_CONFIRM_SAMPLES) {
        carWasConnected = false;
        carUnplugStopPending = true;
        carUnplugStopLastAttemptAt = 0;
        pvControllerCharging = false;
        pvStartConditionSince = 0;
        pvStopConditionSince = 0;
        Serial.println("vehicle unplugged; forcing go-e force mode to stopped");
      }
    } else {
      unplugSafetySampleCount = 0;
    }
  }

  if (carUnplugStopPending && goEStatus.forceMode == 1 &&
      !goEStatus.chargeAllowed) {
    carUnplugStopPending = false;
    pvLastStartStopCommandAt = now;
    pvStartStopClockInitialized = true;
    return;
  }
  if (carUnplugStopPending &&
      (carUnplugStopLastAttemptAt == 0 ||
       now - carUnplugStopLastAttemptAt >= GOE_PV_SAFETY_STOP_RETRY_MS)) {
    carUnplugStopLastAttemptAt = now;
    if (writeGoEForceMode(1)) {
      pvLastStartStopCommandAt = now;
      pvStartStopClockInitialized = true;
    }
  }
}

void updateGoEPvChargingPolicy() {
  const uint32_t now = millis();
  pvControlAction = "NONE";
  pvStartThresholdValid = false;
  updateGridPowerMean();

  if (startupSafeStopPending) {
    pvControlState = "STARTUP_SAFE_STOP_PENDING";
    pvControlAction = "STOP";
    if (goEStatus.valid &&
        now - goEStatus.updatedAt <= GOE_PHASE_STATUS_MAX_AGE_MS &&
        goEStatus.forceMode == 1 && !goEStatus.chargeAllowed) {
      startupSafeStopPending = false;
      pvControllerCharging = false;
      pvControllerStateInitialized = true;
      pvControlState = "STARTUP_SAFE_STOP_CONFIRMED";
      pvControlAction = "NONE";
      chargingModeTransitionState = "ACTIVE";
      chargingModeTransitionPending = false;
      startupSafeStopLastAttemptAt = 0;
      pvLastStartStopCommandAt = now;
      pvStartStopClockInitialized = true;
      return;
    }
    if (startupSafeStopLastAttemptAt == 0 ||
        now - startupSafeStopLastAttemptAt >= GOE_PV_SAFETY_STOP_RETRY_MS) {
      startupSafeStopLastAttemptAt = now;
      if (writeGoEForceMode(1)) {
        pvControlState = "STARTUP_STOP_SENT_WAITING_CONFIRMATION";
      } else {
        pvControlState = "STARTUP_STOP_FAILED_RETRYING";
      }
    }
    return;
  }
  if (carUnplugStopPending) {
    pvControlState = "CAR_UNPLUG_STOP_PENDING";
    pvControlAction = "STOP";
    return;
  }
  if (chargingMode != ChargingMode::PvAdaptive) {
    pvControlState = chargingMode == ChargingMode::Off
        ? "MODE_OFF"
        : (goEStatus.valid && goEStatus.forceMode == 0 &&
                   goEStatus.chargeAllowed
               ? "MANUAL_MODE_ACTIVE"
               : "MANUAL_MODE_STARTING");
    pvControlAction = "NONE";
    updateNonAdaptiveChargingMode();
    return;
  }

  if (!goEStatus.valid ||
      now - goEStatus.updatedAt > GOE_PHASE_STATUS_MAX_AGE_MS ||
      goEStatus.energyCount <= 11) {
    pvAvailablePowerMeanValid = false;
    pvStartConditionSince = 0;
    pvControlState = "WAITING_FOR_GO_E_STATUS";
    if (pvControllerCharging && GOE_PV_CONTROL_ENABLED &&
        (!pvSafetyStopAttemptClockInitialized ||
         now - pvLastSafetyStopAttemptAt >= GOE_PV_SAFETY_STOP_RETRY_MS)) {
      pvControlAction = "STOP";
      pvLastSafetyStopAttemptAt = now;
      pvSafetyStopAttemptClockInitialized = true;
      if (writeGoEForceMode(1)) {
        pvControllerCharging = false;
        pvLastStartStopCommandAt = now;
        pvStartStopClockInitialized = true;
      }
    }
    return;
  }
  if (goEStatus.forceMode == 2) {
    pvControllerCharging = false;
    pvControllerStateInitialized = true;
    pvControlState = "EXTERNAL_FORCE_ON";
    return;
  }
  if (chargingModeTransitionPending) {
    chargingModeTransitionState = "ACTIVE";
    chargingModeTransitionPending = false;
  }
  if (!pvControllerStateInitialized) {
    pvControllerCharging =
        goEStatus.forceMode == 0 && goEStatus.chargeAllowed;
    pvControllerStateInitialized = true;
  }
  if (!pvStartStopClockInitialized) {
    pvLastStartStopCommandAt = now;
    pvStartStopClockInitialized = true;
  }
  if (!pvCurrentCommandClockInitialized) {
    pvLastCurrentCommandAt = now;
    pvCurrentCommandClockInitialized = true;
  }
  if (!gridPowerMeanValid ||
      meterPowerLastValidAt == 0 ||
      now - meterPowerLastValidAt > GOE_PV_METER_STALE_MS) {
    pvAvailablePowerMeanValid = false;
    pvStartConditionSince = 0;
    pvControlState = "WAITING_FOR_FRESH_DS100";
    if (pvControllerCharging) {
      pvControlAction = "STOP";
      if (GOE_PV_CONTROL_ENABLED &&
          (!pvSafetyStopAttemptClockInitialized ||
           now - pvLastSafetyStopAttemptAt >= GOE_PV_SAFETY_STOP_RETRY_MS)) {
        pvLastSafetyStopAttemptAt = now;
        pvSafetyStopAttemptClockInitialized = true;
        if (writeGoEForceMode(1)) {
          pvControllerCharging = false;
          pvLastStartStopCommandAt = now;
          pvStartStopClockInitialized = true;
        }
      }
    }
    return;
  }

  const uint8_t phaseCount = getGoEActivePhaseCount();
  pvPolicyGridPowerMeanValid = gridPowerMeanValid;
  pvPolicyGridPowerMeanW = gridPowerMeanW;
  const float chargerPowerW = abs(goEStatus.energy[11]);
  pvAvailablePowerMeanW = -pvPolicyGridPowerMeanW + chargerPowerW;
  pvAvailablePowerMeanValid = true;
  if (now - meterPowerLastValidAt > METER_STATUS_MAX_AGE_MS) {
    pvControlState = "HOLDING_FOR_FRESH_DS100";
    pvStartConditionSince = 0;
    return;
  }
  const float minimumChargingPowerW =
      GOE_PV_MIN_CURRENT_A * GOE_PV_PHASE_VOLTAGE_V * phaseCount;
  const float startThresholdW =
      minimumChargingPowerW + GOE_PV_SAFETY_RESERVE_W;
  pvStartThresholdW = startThresholdW;
  pvStartThresholdValid = true;
  int calculatedCurrentA = static_cast<int>(
      (pvAvailablePowerMeanW - GOE_PV_SAFETY_RESERVE_W) /
      (GOE_PV_PHASE_VOLTAGE_V * phaseCount));
  calculatedCurrentA = constrain(
      calculatedCurrentA, GOE_PV_MIN_CURRENT_A, GOE_PV_MAX_CURRENT_A);

  if (meterPowerConsecutiveErrors >= GOE_PV_STOP_ERROR_COUNT) {
    pvControlState = "STOP_AFTER_DS100_ERRORS";
    pvControlAction = pvControllerCharging ? "STOP" : "NONE";
    pvStartConditionSince = 0;
    if (pvControllerCharging && GOE_PV_CONTROL_ENABLED &&
        (!pvSafetyStopAttemptClockInitialized ||
         now - pvLastSafetyStopAttemptAt >= GOE_PV_SAFETY_STOP_RETRY_MS)) {
      pvLastSafetyStopAttemptAt = now;
      pvSafetyStopAttemptClockInitialized = true;
      if (writeGoEForceMode(1)) {
        pvControllerCharging = false;
        pvLastStartStopCommandAt = now;
      }
    }
    return;
  }

  if (!pvControllerCharging &&
      (!goEStatus.carStateValid || goEStatus.carState == 1)) {
    pvStartConditionSince = 0;
    pvTargetCurrentA = GOE_PV_MIN_CURRENT_A;
    pvControlState = "WAITING_FOR_VEHICLE";
    return;
  }

  if (!pvControllerCharging) {
    if (meterPowerConsecutiveErrors >= GOE_PV_LIMIT_CURRENT_ERROR_COUNT) {
      pvStartConditionSince = 0;
      pvTargetCurrentA = GOE_PV_MIN_CURRENT_A;
      pvControlState = "START_BLOCKED_BY_DS100_ERRORS";
      return;
    }
    if (meterPowerConsecutiveErrors > 0) {
      pvControlState = "WAITING_FOR_DS100_SAMPLE";
      pvTargetCurrentA = GOE_PV_MIN_CURRENT_A;
      return;
    }
    if (pvAvailablePowerMeanW < startThresholdW) {
      pvStartConditionSince = 0;
      pvControlState = "WAITING_FOR_START_RESERVE";
      pvTargetCurrentA = GOE_PV_MIN_CURRENT_A;
      return;
    }
    if (pvStartConditionSince == 0) {
      pvStartConditionSince = now;
    }
    pvTargetCurrentA = GOE_PV_MIN_CURRENT_A;
    if (now - pvStartConditionSince < GOE_PV_START_HOLD_MS) {
      pvControlState = "START_RESERVE_HOLD";
      return;
    }
    if (now - pvLastStartStopCommandAt < GOE_PV_START_STOP_INTERVAL_MS) {
      pvControlState = "START_WAITING_FOR_MIN_INTERVAL";
      return;
    }

    pvControlState = "START_READY";
    pvControlAction = "START";
    if (GOE_PV_CONTROL_ENABLED) {
      pvLastStartStopCommandAt = now;
      pvLastCurrentCommandAt = now;
      pvStartStopClockInitialized = true;
      pvCurrentCommandClockInitialized = true;
      if (writeGoERequestedCurrent(GOE_PV_MIN_CURRENT_A) &&
          writeGoEForceMode(0)) {
        pvControllerCharging = true;
        pvStartConditionSince = 0;
      } else {
        pvControlState = "START_COMMAND_FAILED";
        pvControlAction = "START_FAILED";
      }
    }
    return;
  }

  pvStartConditionSince = 0;
  if (meterPowerConsecutiveErrors == 0) {
    if (pvAvailablePowerMeanW < minimumChargingPowerW) {
      if (pvStopConditionSince == 0) {
        pvStopConditionSince = now;
      }
    } else {
      pvStopConditionSince = 0;
    }
  }

  const uint32_t errorLimitedCurrent =
      meterPowerConsecutiveErrors >= GOE_PV_LIMIT_CURRENT_ERROR_COUNT
          ? GOE_PV_MIN_CURRENT_A
          : static_cast<uint32_t>(calculatedCurrentA);
  pvTargetCurrentA = static_cast<int>(errorLimitedCurrent);

  if (pvStopConditionSince != 0 &&
      now - pvStopConditionSince >= GOE_PV_STOP_HOLD_MS &&
      meterPowerConsecutiveErrors == 0 &&
      now - pvLastStartStopCommandAt >= GOE_PV_START_STOP_INTERVAL_MS) {
    pvControlState = "STOP_READY_LOW_SURPLUS";
    pvControlAction = "STOP";
    if (GOE_PV_CONTROL_ENABLED) {
      pvLastStartStopCommandAt = now;
      if (writeGoEForceMode(1)) {
        pvControllerCharging = false;
        pvStopConditionSince = 0;
      } else {
        pvControlState = "STOP_COMMAND_FAILED";
        pvControlAction = "STOP_FAILED";
      }
    }
    return;
  }

  if (meterPowerConsecutiveErrors > 0 &&
      meterPowerConsecutiveErrors < GOE_PV_LIMIT_CURRENT_ERROR_COUNT) {
    pvControlState = "HOLDING_AFTER_DS100_ERROR";
    return;
  }

  if (!goEStatus.chargeAllowed || goEStatus.forceMode != 0) {
    pvControlState = "CHARGER_NOT_ALLOWING_CHARGE";
    return;
  }

  const int currentDifference = pvTargetCurrentA - goEStatus.requestedCurrent;
  if (currentDifference != 0 &&
      now - pvLastCurrentCommandAt >= GOE_PV_CURRENT_COMMAND_INTERVAL_MS) {
    const int nextCurrentA = goEStatus.requestedCurrent +
        (currentDifference > 0 ? 1 : -1);
    pvControlState = meterPowerConsecutiveErrors >= GOE_PV_LIMIT_CURRENT_ERROR_COUNT
        ? "LIMITING_CURRENT_AFTER_DS100_ERRORS"
        : "ADJUSTING_CURRENT";
    pvControlAction = nextCurrentA > goEStatus.requestedCurrent
        ? "INCREASE_CURRENT_1A"
        : "DECREASE_CURRENT_1A";
    if (GOE_PV_CONTROL_ENABLED) {
      pvLastCurrentCommandAt = now;
      if (!writeGoERequestedCurrent(nextCurrentA)) {
        pvControlState = "CURRENT_COMMAND_FAILED";
        pvControlAction = "CURRENT_CHANGE_FAILED";
      }
    }
  } else if (meterPowerConsecutiveErrors >= GOE_PV_LIMIT_CURRENT_ERROR_COUNT) {
    pvControlState = "LIMIT_CURRENT_TO_6A_AFTER_DS100_ERRORS";
  } else {
    pvControlState = "CHARGING_WITHIN_SURPLUS";
  }
}

void readMeterSnapshot() {
  for (MeterReading &reading : meterReadings) {
    reading.valid = false;
  }

  modbusClient.end();
  Serial2.flush();
  Serial2.end();
  delay(100);
  Serial2.begin(9600, SERIAL_8N1, RXD2, TXD2);

  modbusClient.setTimeout(500);
  modbusClient.begin(Serial2);

  for (size_t i = 0; i < METER_READING_COUNT; ++i) {
    const uint16_t address = meterRegisters[i];
    ModbusMessage response;
    for (uint8_t attempt = 0; attempt < 2; ++attempt) {
      response = modbusClient.syncRequest(
          address, 1, READ_INPUT_REGISTER, address, 2);
      if (response.size() >= 7 &&
          response[1] == READ_INPUT_REGISTER &&
          response[2] == 4) {
        break;
      }
      const uint8_t byteCount = response.size() > 2 ? response[2] : 0;
      Serial.printf("DS100 register 0x%04X read failed: attempt=%u error=%d size=%u bytes=%u\n",
                    address, attempt + 1,
                    static_cast<int>(response.getError()), response.size(), byteCount);
      delay(100);
    }

    if (response.size() < 7 || response[1] != READ_INPUT_REGISTER ||
        response[2] != 4) continue;

    uint32_t rawValue = 0;
    response.get(3, rawValue);
    if (i < 3) {
      meterReadings[i].value = rawValue / 1000.0f;
    } else if (i < 6) {
      meterReadings[i].value = static_cast<int32_t>(rawValue) / 1000.0f;
    } else {
      meterReadings[i].value = static_cast<int32_t>(rawValue);
    }
    meterReadings[i].valid = true;
    delay(100);
  }

  meterUpdatedAt = millis();
  const MeterReading &gridPower = meterReadings[METER_READING_COUNT - 1];
  if (gridPower.valid) {
    gridPowerSamples[gridPowerSampleNext].watts = gridPower.value;
    gridPowerSamples[gridPowerSampleNext].sampledAt = meterUpdatedAt;
    gridPowerSampleNext =
        (gridPowerSampleNext + 1) % GOE_GRID_POWER_SAMPLE_CAPACITY;
    if (gridPowerSampleCount < GOE_GRID_POWER_SAMPLE_CAPACITY) {
      ++gridPowerSampleCount;
    }
    meterPowerLastValidAt = meterUpdatedAt;
    meterPowerConsecutiveErrors = 0;
  } else {
    ++meterPowerErrorCount;
    ++meterPowerConsecutiveErrors;
  }
  modbusClient.end();
  Serial2.flush();
  Serial2.end();
  delay(100);
  Serial2.begin(19200, SERIAL_8N1, RXD2, TXD2);
  Serial.println("DS100 poll complete; UART restored to TLX 19200 baud");
}

bool getDs100PowerQualityEstimate(float &apparentPowerVa, float &powerFactor) {
  apparentPowerVa = 0.0f;
  for (size_t phase = 0; phase < 3; ++phase) {
    const MeterReading &voltage = meterReadings[phase];
    const MeterReading &current = meterReadings[phase + 3];
    if (!voltage.valid || !current.valid) {
      return false;
    }
    apparentPowerVa += fabsf(voltage.value * current.value);
  }

  const MeterReading &activePower = meterReadings[METER_READING_COUNT - 1];
  if (!activePower.valid || apparentPowerVa == 0.0f) {
    return false;
  }
  powerFactor = fabsf(activePower.value) / apparentPowerVa;
  return true;
}

// All values from TLX in one json respons without units
void CreateJsonAll(void){
  jsonDocument.clear();
  for (int i = 0; i < MyTLX.DATA_ENUMS; i++) {
    jsonDocument["Product"] =            MyTLX.TLX.ProductNumber.c_str();
    jsonDocument["Serial"]  =            MyTLX.TLX.SerialNumber.c_str();
    jsonDocument["OpModeTxt"] =          MyTLX.TLX.OpModeTxt.c_str();
    jsonDocument[MyTLX.TLX.ParName[i]] = MyTLX.MeasString((DanfossTLX::Par_e)i);
  }  
  JsonObject ds100 = jsonDocument["DS100"].to<JsonObject>();
  ds100["updated_ms"] = meterUpdatedAt;
  for (size_t i = 0; i < METER_READING_COUNT; ++i) {
    JsonObject reading = ds100[meterNames[i]].to<JsonObject>();
    if (meterReadings[i].valid) {
      reading["value"] = meterReadings[i].value;
    } else {
      reading["value"] = nullptr;
    }
    reading["unit"] = meterUnits[i];
    reading["valid"] = meterReadings[i].valid;
  }
  float apparentPowerVa = 0.0f;
  float powerFactor = 0.0f;
  if (getDs100PowerQualityEstimate(apparentPowerVa, powerFactor)) {
    ds100["apparent_power_estimate_va"] = apparentPowerVa;
    ds100["power_factor_estimate"] = powerFactor;
  } else {
    ds100["apparent_power_estimate_va"] = nullptr;
    ds100["power_factor_estimate"] = nullptr;
  }
  JsonObject charger = jsonDocument["goe"].to<JsonObject>();
  charger["valid"] = goEStatus.valid;
  charger["updated_ms"] = goEStatus.updatedAt;
  charger["error"] = goEStatus.error;
  charger["pv_control_enabled"] =
      GOE_PV_CONTROL_ENABLED && chargingMode == ChargingMode::PvAdaptive;
  charger["charging_mode"] = chargingModeText(chargingMode);
  charger["mode_transition_state"] = chargingModeTransitionState;
  charger["mode_transition_pending"] = chargingModeTransitionPending;
  charger["automatic_pv_return_armed"] = manualPvReturnArmed;
  charger["manual_vehicle_connected_seen"] = manualCarConnectedSeen;
  charger["manual_pv_night_observed"] = manualPvNightObserved;
  charger["manual_current_a"] = manualCurrentA;
  charger["manual_phase_preference"] =
      phasePreferenceText(manualPhasePreference);
  charger["manual_start_wait_remaining_ms"] =
      manualStartStopWaitRemainingMs();
  charger["pv_control_state"] = pvControlState;
  charger["pv_control_action"] = pvControlAction;
  charger["pv_control_state_duration_ms"] = pvControlStateDurationMs();
  uint32_t pvStartWaitMs = 0;
  if (pvStartWaitRemainingMs(pvStartWaitMs)) {
    charger["pv_start_wait_remaining_ms"] = pvStartWaitMs;
  } else {
    charger["pv_start_wait_remaining_ms"] = nullptr;
  }
  charger["car_unplug_stop_pending"] = carUnplugStopPending;
  charger["pv_target_current_a"] = pvTargetCurrentA;
  charger["ds100_power_error_count"] = meterPowerErrorCount;
  charger["ds100_power_consecutive_errors"] = meterPowerConsecutiveErrors;
  charger["pv_safety_reserve_w"] = GOE_PV_SAFETY_RESERVE_W;
  if (pvStartThresholdValid) {
    charger["pv_start_threshold_w"] = pvStartThresholdW;
  } else {
    charger["pv_start_threshold_w"] = nullptr;
  }
  if (gridPowerMeanValid) {
    charger["ds100_power_mean_w"] = gridPowerMeanW;
    charger["pv_grid_export_mean_w"] = -gridPowerMeanW;
  } else {
    charger["ds100_power_mean_w"] = nullptr;
    charger["pv_grid_export_mean_w"] = nullptr;
  }
  if (pvPolicyGridPowerMeanValid) {
    charger["pv_policy_grid_power_mean_w"] = pvPolicyGridPowerMeanW;
  } else {
    charger["pv_policy_grid_power_mean_w"] = nullptr;
  }
  if (pvAvailablePowerMeanValid) {
    charger["pv_available_power_mean_w"] = pvAvailablePowerMeanW;
  } else {
    charger["pv_available_power_mean_w"] = nullptr;
  }
  if (goEStatus.valid) {
    charger["type"] = goEStatus.chargerType;
    charger["firmware"] = goEStatus.firmware;
    charger["charge_allowed"] = goEStatus.chargeAllowed;
    if (goEStatus.chargeAllowedChangedAtValid) {
      charger["charge_allowed_last_change"] = goEStatus.chargeAllowedChangedAt;
    } else {
      charger["charge_allowed_last_change"] = nullptr;
    }
    charger["requested_current_a"] = goEStatus.requestedCurrent;
    if (goEStatus.allowedCurrentValid) {
      charger["allowed_current_a"] = goEStatus.allowedCurrent;
    } else {
      charger["allowed_current_a"] = nullptr;
    }
    charger["force_mode"] = goEStatus.forceMode;
    if (goEStatus.phasePreferenceValid) {
      charger["phase_preference"] = goEStatus.phasePreference;
      charger["phase_preference_text"] =
          phasePreferenceText(goEStatus.phasePreference);
    } else {
      charger["phase_preference"] = nullptr;
      charger["phase_preference_text"] = "Unavailable";
    }
    if (goEStatus.forceSinglePhaseValid) {
      charger["force_single_phase"] = goEStatus.forceSinglePhase;
    } else {
      charger["force_single_phase"] = nullptr;
    }
    if (goEStatus.phaseCountValid) {
      charger["phase_count"] = goEStatus.phaseCount;
    } else {
      charger["phase_count"] = nullptr;
    }
    if (goEStatus.phaseWishValid) {
      charger["phase_wish"] = goEStatus.phaseWish;
    } else {
      charger["phase_wish"] = nullptr;
    }
    if (phaseAvailableSurplusValid) {
      charger["phase_available_surplus_w"] = phaseAvailableSurplusW;
    } else {
      charger["phase_available_surplus_w"] = nullptr;
    }
    charger["phase_target"] = phaseTargetInitialized
        ? phaseTarget
        : goEStatus.phasePreference;
    charger["phase_target_text"] = phasePreferenceText(
        phaseTargetInitialized ? phaseTarget : goEStatus.phasePreference);
    charger["phase_target_pending"] =
        phaseTargetInitialized && phaseTarget != goEStatus.phasePreference;
    if (goEStatus.minPhaseToggleWaitValid) {
      charger["minimum_phase_toggle_wait_ms"] = goEStatus.minPhaseToggleWaitMs;
    } else {
      charger["minimum_phase_toggle_wait_ms"] = nullptr;
    }
    if (goEStatus.minPhaseWishTimeValid) {
      charger["minimum_phase_wish_time_ms"] = goEStatus.minPhaseWishTimeMs;
    } else {
      charger["minimum_phase_wish_time_ms"] = nullptr;
    }
    if (goEStatus.carStateValid) {
      charger["car_state"] = goEStatus.carState;
      charger["car_state_text"] = carStateText(goEStatus.carState);
    } else {
      charger["car_state"] = nullptr;
      charger["car_state_text"] = "Unavailable";
    }
    charger["error_code"] = goEStatus.errorCode;
    if (goEStatus.lastButtonPressValid) {
      charger["last_button_press_ms"] = goEStatus.lastButtonPressMs;
    } else {
      charger["last_button_press_ms"] = nullptr;
    }
    if (goEStatus.lastChangedFromChargingValid) {
      charger["last_changed_from_charging_ms"] = goEStatus.lastChangedFromChargingMs;
    } else {
      charger["last_changed_from_charging_ms"] = nullptr;
    }
    if (goEStatus.lastChangedToChargingValid) {
      charger["last_changed_to_charging_ms"] = goEStatus.lastChangedToChargingMs;
    } else {
      charger["last_changed_to_charging_ms"] = nullptr;
    }
    JsonArray energy = charger["nrg"].to<JsonArray>();
    for (uint8_t i = 0; i < goEStatus.energyCount; ++i) {
      energy.add(goEStatus.energy[i]);
    }
  }
  serializeJson(jsonDocument, jsonbuffer);  
}
  
void CreateJson(const char *tag, float value, const char *unit) {  
  jsonDocument.clear();
  jsonDocument["type"] = tag;
  jsonDocument["value"] = value;
  jsonDocument["unit"] = unit;
  serializeJson(jsonDocument, jsonbuffer);  
}
void CreateJsonTxt(const char *tag, const char *value) {  
  jsonDocument.clear();
  jsonDocument["type"] = tag;
  jsonDocument["value"] = value;
  serializeJson(jsonDocument, jsonbuffer);  
}
  
// Web page that holds all parameters/measurements in a table
void HandleRoot() {
  constexpr uint32_t pageRefreshSeconds = 15;
  const uint32_t manualStartWaitMs = manualStartStopWaitRemainingMs();
  const uint32_t pvStateDurationSeconds = pvControlStateDurationMs() / 1000;
  uint32_t pvStartWaitMs = 0;
  const bool pvStartWaitActive = pvStartWaitRemainingMs(pvStartWaitMs);
  float apparentPowerVa = 0.0f;
  float powerFactor = 0.0f;
  const bool powerQualityEstimateValid =
      getDs100PowerQualityEstimate(apparentPowerVa, powerFactor);
  String transitionDescription = chargingModeTransitionState;
  if (manualStartWaitMs > 0) {
    transitionDescription =
        "Wartezeit nach dem letzten manuellen Stopp: noch " +
        String((manualStartWaitMs + 59999) / 60000) +
        " min bis zur erneuten Ladefreigabe.";
  }
  String msg = "<!DOCTYPE html><html><head><meta http-equiv=\"refresh\" content=\"" +
      String(pageRefreshSeconds) +
      "\"><title>Vehicle charge controller</title></head><style>tr:nth-child(even) {background-color: #f3f3f3;}button,input,select{margin:0.25rem;padding:0.4rem;}</style><body><h1>Vehicle charge controller</h1><h2>Lademodus</h2><p>Aktuell: " +
      String(chargingModeText(chargingMode)) + " — " +
      transitionDescription + (chargingModeTransitionPending ? " (Übergang läuft)" : "") +
      "</p><p>Go-e-Taster: Start mit 16 A / Auto. Rueckkehr zu PV-Adaptiv bei erkanntem Ausstecken oder wenn der Wechselrichter nach der Nacht wieder On Grid meldet.</p><form method=\"post\" action=\"/mode\"><button name=\"mode\" value=\"off\">Aus</button><button name=\"mode\" value=\"pv\">PV-Adaptiv</button></form><form method=\"post\" action=\"/phase\"><label>Phasenwahl (gilt auch im PV-Modus) <select name=\"phase\"><option value=\"0\"" +
      String(manualPhasePreference == 0 ? " selected" : "") +
      ">Auto (PV-Überschuss)</option><option value=\"1\"" +
      String(manualPhasePreference == 1 ? " selected" : "") +
      ">Force_1</option><option value=\"2\"" +
      String(manualPhasePreference == 2 ? " selected" : "") +
      ">Force_3</option></select></label><button type=\"submit\">Phasenwahl übernehmen</button></form><form method=\"post\" action=\"/mode\"><input type=\"hidden\" name=\"mode\" value=\"start\"><label>Strom <input type=\"range\" name=\"current_a\" min=\"6\" max=\"16\" value=\"" +
      String(manualCurrentA) +
      "\" oninput=\"this.nextElementSibling.value=this.value\"><output>" +
      String(manualCurrentA) +
      "</output> A</label><button type=\"submit\">Start</button></form><p>Der Strom wird vor dem Start gesetzt. Eine Phasenänderung erfolgt erst bei bestätigter Ladefreigabe und nach den go-e-Mindestwartezeiten. Auto wählt im PV-Modus die Phasen nach Überschuss; Force_1/Force_3 bleiben fest.</p><h2>Messwerte</h2><table><tr><th>Description</th><th>Value</th><th>Unit</th><th>Indeks</th></tr>";
  if(MyTLX.TLX.ProductNumber.c_str()[0] != 0 ){ msg += "<tr><td>Product Number</td><td>" + MyTLX.TLX.ProductNumber + "</td><td></td><td></td></tr>" ;}
  if(MyTLX.TLX.SerialNumber.c_str()[0]  != 0 ){ msg += "<tr><td>Serial Number</td><td>" + MyTLX.TLX.SerialNumber + "</td><td></td><td></td></tr>" ;}
  if(MyTLX.TLX.OpModeTxt.c_str()[0] != 0){ msg += "<tr><td>Operation mode</td><td>" + MyTLX.TLX.OpModeTxt + "</td><td></td><td></td></tr>" ;}
  for (int i = 0; i < MyTLX.DATA_ENUMS; i++) {
    msg +=  "<tr><td>" + MyTLX.TLX.Name[i] +"</td><td>"+MyTLX.MeasString((DanfossTLX::Par_e)i)+"</td><td>"+MyTLX.TLX.Unit[i]+"</td><td>"+MyTLX.TLX.ParName[i] + "</td></tr>";
  }
  msg += "<tr><th colspan=\"4\">DS100 Netzanschluss</th></tr>";
  for (size_t i = 0; i < METER_READING_COUNT; ++i) {
    msg += "<tr><td>" + String(meterNames[i]) + "</td><td>";
    msg += meterReadings[i].valid ? String(meterReadings[i].value, 3) : "invalid";
    msg += "</td><td>" + String(meterUnits[i]) + "</td><td>";
    msg += "0x" + String(meterRegisters[i], HEX) + "</td></tr>";
  }
  msg += "<tr><td>Estimated apparent power / power factor</td><td>" +
         (powerQualityEstimateValid
              ? String(apparentPowerVa, 0) + " / " + String(powerFactor, 3)
              : String("n/a")) +
         "</td><td>VA / -</td><td>currents x phase voltages vs 0x0420</td></tr>";
  msg += "<tr><td>DS100 update (ms)</td><td>" + String(meterUpdatedAt) +
         "</td><td></td><td></td></tr>";
  msg += "<tr><th colspan=\"4\">go-e Charger (PV control)</th></tr>";
  msg += "<tr><td>Charging mode / transition</td><td>" +
         String(chargingModeText(chargingMode)) + " / " +
         chargingModeTransitionState +
         "</td><td></td><td></td></tr>";
  msg += "<tr><td>Status</td><td>" +
         String(goEStatus.valid ? "valid" : "unavailable") +
         "</td><td></td><td></td></tr>";
  msg += "<tr><td>PV adaptive control enabled / state / action</td><td>" +
         String(GOE_PV_CONTROL_ENABLED &&
                        chargingMode == ChargingMode::PvAdaptive
                    ? "yes"
                    : "no") + " / " +
         pvControlState + " / " + pvControlAction +
         "</td><td></td><td></td></tr>";
  msg += "<tr><td>PV state duration</td><td><span id=\"pv-state-age\" data-seconds=\"" +
         String(pvStateDurationSeconds) + "\">" +
         String(pvStateDurationSeconds) +
         " s</span></td><td>s</td><td></td></tr>";
  msg += "<tr><td>PV start wait remaining</td><td>";
  if (pvStartWaitActive) {
    const uint32_t pvStartWaitSeconds = (pvStartWaitMs + 999) / 1000;
    msg += "<span id=\"pv-start-wait\" data-seconds=\"" +
           String(pvStartWaitSeconds) + "\">" +
           String(pvStartWaitSeconds) + " s</span>";
  } else if (strcmp(pvControlState, "WAITING_FOR_START_RESERVE") == 0) {
    msg += "reserve below threshold; timer reset";
  } else {
    msg += "not active";
  }
  msg += "</td><td>s</td><td>5-min reserve / minimum start interval</td></tr>";
  msg += "<tr><td>PV target current / safety reserve</td><td>" +
         String(pvTargetCurrentA) + " / " +
         String(GOE_PV_SAFETY_RESERVE_W, 0) +
         "</td><td>A / W</td><td></td></tr>";
  msg += "<tr><td>DS100 power errors (total / consecutive)</td><td>" +
         String(meterPowerErrorCount) + " / " +
         String(meterPowerConsecutiveErrors) +
         "</td><td></td><td>0x0420</td></tr>";
  msg += "<tr><td>60 s real DS100 / policy input / available power</td><td>" +
         (gridPowerMeanValid ? String(gridPowerMeanW, 0) : String("n/a")) +
         " / " +
         (pvPolicyGridPowerMeanValid
              ? String(pvPolicyGridPowerMeanW, 0)
              : String("n/a")) +
         " / " +
         (pvAvailablePowerMeanValid
              ? String(pvAvailablePowerMeanW, 0)
              : String("n/a")) +
         "</td><td>W / W / W</td><td>policy input + go-e</td></tr>";
  if (goEStatus.valid) {
    msg += "<tr><td>Type / firmware</td><td>" + goEStatus.chargerType +
           " / " + goEStatus.firmware + "</td><td></td><td></td></tr>";
    msg += "<tr><td>Charge allowed</td><td>" +
           String(goEStatus.chargeAllowed ? "yes" : "no") +
           "</td><td></td><td>alw</td></tr>";
    msg += "<tr><td>Last charge-allowed change</td><td>" +
           (goEStatus.chargeAllowedChangedAtValid
                ? String(static_cast<long long>(goEStatus.chargeAllowedChangedAt))
                : String("n/a")) +
           "</td><td></td><td>alwt</td></tr>";
    msg += "<tr><td>Requested / allowed current</td><td>" +
           String(goEStatus.requestedCurrent) + " / " +
           (goEStatus.allowedCurrentValid ? String(goEStatus.allowedCurrent) : String("n/a")) +
           "</td><td>A</td><td>amp / acu</td></tr>";
    msg += "<tr><td>Car state / error code</td><td>" +
           (goEStatus.carStateValid
                ? String(carStateText(goEStatus.carState)) + " (" + String(goEStatus.carState) + ")"
                : String("Unavailable")) + " / " +
           String(goEStatus.errorCode) +
           "</td><td></td><td>car / err</td></tr>";
    msg += "<tr><td>Force mode / status age</td><td>" +
           String(goEStatus.forceMode) + " / " +
           String(millis() - goEStatus.updatedAt) +
           "</td><td>ms</td><td>frc</td></tr>";
    msg += "<tr><td>Phase preference / force single phase</td><td>" +
           (goEStatus.phasePreferenceValid
                ? String(phasePreferenceText(goEStatus.phasePreference))
                : String("n/a")) + " / " +
           (goEStatus.forceSinglePhaseValid
                ? String(goEStatus.forceSinglePhase ? "yes" : "no")
                : String("n/a")) +
           "</td><td></td><td>psm / fsp</td></tr>";
    msg += "<tr><td>Phase count / wish</td><td>" +
           (goEStatus.phaseCountValid
                ? String(goEStatus.phaseCount)
                : String("n/a")) + " / " +
           (goEStatus.phaseWishValid
                ? String(goEStatus.phaseWish)
                : String("n/a")) +
           "</td><td></td><td>pnp / pwm</td></tr>";
    msg += "<tr><td>Phase available surplus</td><td>" +
           (phaseAvailableSurplusValid
                ? String(phaseAvailableSurplusW, 0)
                : String("n/a")) +
           "</td><td>W</td><td>DS100 export + go-e active power</td></tr>";
    msg += "<tr><td>Last change from / to charging</td><td>" +
           (goEStatus.lastChangedFromChargingValid
                ? String(static_cast<long long>(goEStatus.lastChangedFromChargingMs))
                : String("n/a")) + " / " +
           (goEStatus.lastChangedToChargingValid
                ? String(static_cast<long long>(goEStatus.lastChangedToChargingMs))
                : String("n/a")) +
           "</td><td>ms</td><td>lccfc / lcctc</td></tr>";
    msg += "<tr><td>nrg raw</td><td>";
    for (uint8_t i = 0; i < goEStatus.energyCount; ++i) {
      if (i) msg += ",";
      msg += String(goEStatus.energy[i]);
    }
    msg += "</td><td></td><td>nrg</td></tr>";
  } else {
    msg += "<tr><td>Read error</td><td>" + goEStatus.error +
           "</td><td></td><td></td></tr>";
  }
  msg += "<script>setInterval(function(){var age=document.getElementById('pv-state-age');if(age){var seconds=Number(age.dataset.seconds)+1;age.dataset.seconds=seconds;age.textContent=seconds+' s';}var wait=document.getElementById('pv-start-wait');if(wait){var seconds=Math.max(0,Number(wait.dataset.seconds)-1);wait.dataset.seconds=seconds;wait.textContent=seconds+' s';}},1000);</script></body></html>";
  server.send(200, "text/html", msg);
}

bool parseBoundedIntegerArgument(
    const char *name, int minimum, int maximum, int &value) {
  if (!server.hasArg(name)) {
    return false;
  }

  String rawValue = server.arg(name);
  rawValue.trim();
  if (rawValue.isEmpty()) {
    return false;
  }
  for (size_t i = 0; i < rawValue.length(); ++i) {
    if (rawValue[i] < '0' || rawValue[i] > '9') {
      return false;
    }
  }

  const long parsedValue = rawValue.toInt();
  if (parsedValue < minimum || parsedValue > maximum) {
    return false;
  }
  value = static_cast<int>(parsedValue);
  return true;
}

void HandleChargingModeRequest() {
  if (!server.hasArg("mode")) {
    server.send(400, "text/plain", "Missing charging mode");
    return;
  }

  const String requestedMode = server.arg("mode");
  bool accepted = false;
  if (requestedMode == "off") {
    accepted = requestChargingMode(ChargingMode::Off);
  } else if (requestedMode == "pv") {
    accepted = requestChargingMode(ChargingMode::PvAdaptive);
  } else if (requestedMode == "start") {
    int currentA = 0;
    if (!parseBoundedIntegerArgument("current_a", GOE_PV_MIN_CURRENT_A,
                                     GOE_PV_MAX_CURRENT_A, currentA)) {
      server.send(400, "text/plain", "Start requires current_a 6-16");
      return;
    }
    accepted = requestChargingMode(
        ChargingMode::ManualPower, currentA, manualPhasePreference);
  } else {
    server.send(400, "text/plain", "Unsupported charging mode");
    return;
  }

  if (!accepted) {
    server.send(400, "text/plain", chargingModeTransitionState);
    return;
  }

  server.sendHeader("Location", "/");
  server.send(303, "text/plain", "Charging mode transition requested");
}

void HandlePhasePreferenceRequest() {
  int phase = 0;
  if (!parseBoundedIntegerArgument(
          "phase", static_cast<int>(GoEPhasePreference::Auto),
          static_cast<int>(GoEPhasePreference::ForceThreePhases), phase)) {
    server.send(400, "text/plain", "Phase preference must be 0-2");
    return;
  }

  const uint8_t requestedPreference = static_cast<uint8_t>(phase);
  if (requestedPreference == manualPhasePreference) {
    server.sendHeader("Location", "/");
    server.send(303, "text/plain", "Phase preference unchanged");
    return;
  }
  manualPhasePreference = requestedPreference;
  modePhaseRequestedAt = millis();
  phaseTargetInitialized = false;

  if (chargingMode == ChargingMode::ManualPower) {
    chargingModeTransitionState = "PHASE_PREFERENCE_CHANGED";
    chargingModeTransitionPending = true;
    modeCommandAwaitingStatus = false;
    modeCommandClockInitialized = false;
  }

  Serial.printf("phase preference selected: %s\n",
                phasePreferenceText(manualPhasePreference));
  server.sendHeader("Location", "/");
  server.send(303, "text/plain", "Phase preference updated");
}

// Send json object of requested parameter/value. See DanfossTLX::ParName for valid names
void ReturnTLXMeasValue() {
  String uri = server.uri();
  DanfossTLX::Par_e ParEnum = MyTLX.DATA_ENUMS; 
  bool found = false;
  if (uri.length() > 3 && uri.startsWith("/")){
    uri.remove(0,1);
    //Serial.println(uri);
    for (int i = 0; i < MyTLX.DATA_ENUMS; i++) {
      // Serial.println( uri + "<>" + MyTLX.TLX.ParName[i] );
      uri.toUpperCase();
      String Name = MyTLX.TLX.ParName[i];
      Name.toUpperCase();
      if ( uri == Name ){
        found = true;
        CreateJson(MyTLX.TLX.ParName[i].c_str(), MyTLX.TLX.Meas[i], MyTLX.TLX.Unit[i].c_str() );
        server.send(200, "application/json", jsonbuffer);
        break;
      }
    }
  }
  // Wrong parameter - return txt result for debug
  // http://#IP#/Unkown?Earth=MostlyHarmless&Answer=42
  // Pararameter not recognized 
  // URI: /Unkown
  // Method: GET
  // Arguments: 2
  //  Earth: MostlyHarmless
  //  Answer: 42
  
  if(!found){
   String msg = "Pararameter not recognized \n\n";
   msg += "URI: ";
   msg += server.uri();
   msg += "\nMethod: ";
   msg += (server.method() == HTTP_GET) ? "GET" : "POST";
   msg += "\nArguments: ";
   msg += server.args();
   msg += "\n";
   for (uint8_t i = 0; i < server.args(); i++) {
     msg += " " + server.argName(i) + ": " + server.arg(i) + "\n";
   }
   server.send(404, "text/plain", msg);
  }
}

void setup()
{
  Serial.begin(115200) ;  // Debug console
  Serial.println(CodeDate + Program);
  Serial.println(Arduino);
  Serial.println(Board);
  Serial.println(String("Compile: ") + String(__DATE__)  +" " + String(__TIME__));
  Serial.println(String(__FILE__));

  startupSafeStopPending = true;
  Serial.println("Startup safety: waiting for fresh go-e status before PV or phase control");

  WiFi.mode(WIFI_STA); // Connect to AP
  WiFi.begin(SECRET_SSID, SECRET_WIFI_PSWD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.print("Connected to ");
  Serial.println(SECRET_SSID);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  server.on("/", HandleRoot);
  server.on("/OpModeTxt", []() { CreateJsonTxt("OpModeTxt", MyTLX.TLX.OpModeTxt.c_str());      server.send(200, "application/json", jsonbuffer); }); 
  server.on("/Product", []()   { CreateJsonTxt("Product",   MyTLX.TLX.ProductNumber.c_str() ); server.send(200, "application/json", jsonbuffer); }); 
  server.on("/SerialTxt", []() { CreateJsonTxt("Serial",    MyTLX.TLX.SerialNumber.c_str() );  server.send(200, "application/json", jsonbuffer); }); 
  server.on("/All",       []() { CreateJsonAll();  server.send(200, "application/json", jsonbuffer); }); 
  server.on("/mode", HTTP_GET, []() {
    server.send(405, "text/plain", "Use POST to change the charging mode");
  });
  server.on("/mode", HTTP_POST, HandleChargingModeRequest);
  server.on("/phase", HTTP_POST, HandlePhasePreferenceRequest);
  server.onNotFound(ReturnTLXMeasValue);
  server.begin();
  Serial.println("HTTP server started\nSetup ended");
}

void loop()
{
  const unsigned long  WIFITIME  = 30000;
  const unsigned long  TIMETLX   = 800; // 36 par x 2 times a minute -> ~830ms each (Not to block server.handleClient();
  static unsigned long TimeTLX = millis();

  server.handleClient();
  // Wraparround every ~50 days;
  // Should not be neceesary after changer to unsigned long
  if (millis() < TimeTLX){
    TimeTLX = millis();
  }
 
  if (millis() - TimeTLX >  TIMETLX){
    MyTLX.GetParameters();
    ++tlxReadsSinceMeter;
    TimeTLX = millis();
    if (tlxReadsSinceMeter >= METER_POLL_EVERY_TLX_READS) {
      readMeterSnapshot();
      tlxReadsSinceMeter = 0;
    }
  }

  if (lastGoEPoll == 0 || millis() - lastGoEPoll >= GOE_POLL_INTERVAL_MS) {
    lastGoEPoll = millis();
    pollGoEStatus();
  }
  updateCarUnplugSafety();
  updateManualModeAutomaticReturn();
  updateGoEPhasePolicy(chargingMode);
  updateGoEPvChargingPolicy();
  updatePvControlStateTimer();
  
  // Test if connected
  if(WiFi.status() != WL_CONNECTED){
    Serial.println("Restart Wifi");
    WiFi.disconnect();
    WiFi.begin(SECRET_SSID, SECRET_WIFI_PSWD);
    unsigned long timenow=millis();
    while (WiFi.status() != WL_CONNECTED  && millis() - timenow < WIFITIME){
      delay(500);
      Serial.print(".");
    }
    if(WiFi.status() != WL_CONNECTED){
      Serial.println("Timeout");
    }
  }
  delay(2);
}