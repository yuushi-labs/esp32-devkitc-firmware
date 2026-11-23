// ESP32-DevKitC firmware entry point.
//
// Behaviour (see SPEC.md):
//   - boot banner with version, chip model, chip id and reset reason
//   - onboard LED on GPIO2 blinking at a 1 s period
//   - 5 s heartbeat log with uptime, free heap and LED mode
//   - serial commands: help, ping, info, led on/off/blink

#include <Arduino.h>
#include <esp_system.h>

#include "app_config.h"
#include "command_handler.h"

namespace {

char g_lineStorage[fw::kMaxLineLength + 1];
fw::LineBuffer g_lineBuffer(g_lineStorage, sizeof(g_lineStorage));

fw::LedMode g_ledMode = fw::LedMode::kBlink;
bool g_ledState = false;
uint32_t g_lastBlinkMs = 0;
uint32_t g_lastHeartbeatMs = 0;

const char* resetReasonName(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_POWERON:
      return "power-on";
    case ESP_RST_EXT:
      return "external-pin";
    case ESP_RST_SW:
      return "software";
    case ESP_RST_PANIC:
      return "panic";
    case ESP_RST_INT_WDT:
      return "interrupt-watchdog";
    case ESP_RST_TASK_WDT:
      return "task-watchdog";
    case ESP_RST_WDT:
      return "watchdog";
    case ESP_RST_DEEPSLEEP:
      return "deep-sleep";
    case ESP_RST_BROWNOUT:
      return "brownout";
    case ESP_RST_SDIO:
      return "sdio";
    case ESP_RST_UNKNOWN:
    default:
      return "unknown";
  }
}

fw::DeviceStatus currentStatus() {
  fw::DeviceStatus status{};
  status.version = FW_VERSION;
  status.chipModel = ESP.getChipModel();
  status.chipRevision = ESP.getChipRevision();
  status.chipId = ESP.getEfuseMac();
  status.uptimeMs = millis();
  status.freeHeap = ESP.getFreeHeap();
  status.ledMode = g_ledMode;
  status.resetReason = resetReasonName(esp_reset_reason());
  return status;
}

void writeLed(bool on) {
  g_ledState = on;
  digitalWrite(fw::kLedPin, on ? HIGH : LOW);
}

void setLedMode(fw::LedMode mode) {
  g_ledMode = mode;
  if (mode == fw::LedMode::kOn) {
    writeLed(true);
  } else if (mode == fw::LedMode::kOff) {
    writeLed(false);
  } else {
    g_lastBlinkMs = millis();
  }
}

void printBanner() {
  char buffer[256];
  const fw::DeviceStatus status = currentStatus();
  fw::formatBanner(status, buffer, sizeof(buffer));
  Serial.print(buffer);
}

void printInfo() {
  char buffer[384];
  const fw::DeviceStatus status = currentStatus();
  fw::formatInfo(status, buffer, sizeof(buffer));
  Serial.print(buffer);
}

void printHeartbeat() {
  char buffer[128];
  const fw::DeviceStatus status = currentStatus();
  fw::formatHeartbeat(status, buffer, sizeof(buffer));
  Serial.print(buffer);
}

void handleLine(const char* line, bool overflowed) {
  if (overflowed) {
    Serial.printf("error: command too long (max %u characters)\n",
                  static_cast<unsigned>(fw::kMaxLineLength));
    Serial.println("type 'help' for a list of commands");
    return;
  }

  const fw::Command command = fw::parseCommand(line);
  switch (command.type) {
    case fw::CommandType::kNone:
      return;
    case fw::CommandType::kHelp:
      Serial.print(fw::commandHelpText());
      return;
    case fw::CommandType::kPing:
      Serial.println("pong");
      return;
    case fw::CommandType::kInfo:
      printInfo();
      return;
    case fw::CommandType::kLedOn:
    case fw::CommandType::kLedOff:
    case fw::CommandType::kLedBlink:
      setLedMode(command.ledMode);
      Serial.printf("led mode: %s\n", fw::ledModeName(command.ledMode));
      return;
    case fw::CommandType::kUnknown:
      break;
  }

  Serial.printf("error: unknown command '%s'\n", line);
  Serial.println("type 'help' for a list of commands");
}

void pollSerial() {
  while (Serial.available() > 0) {
    const char c = static_cast<char>(Serial.read());
    if (g_lineBuffer.feed(c)) {
      handleLine(g_lineBuffer.line(), g_lineBuffer.overflowed());
      g_lineBuffer.clear();
    }
  }
}

void serviceLed(uint32_t nowMs) {
  if (g_ledMode != fw::LedMode::kBlink) {
    return;
  }
  if (nowMs - g_lastBlinkMs >= fw::kBlinkIntervalMs) {
    g_lastBlinkMs = nowMs;
    writeLed(!g_ledState);
  }
}

void serviceHeartbeat(uint32_t nowMs) {
  if (nowMs - g_lastHeartbeatMs >= fw::kHeartbeatIntervalMs) {
    g_lastHeartbeatMs = nowMs;
    printHeartbeat();
  }
}

}  // namespace

void setup() {
  Serial.begin(fw::kSerialBaud);
  pinMode(fw::kLedPin, OUTPUT);
  setLedMode(fw::LedMode::kBlink);

  const uint32_t nowMs = millis();
  g_lastBlinkMs = nowMs;
  g_lastHeartbeatMs = nowMs;

  printBanner();
}

void loop() {
  pollSerial();

  const uint32_t nowMs = millis();
  serviceLed(nowMs);
  serviceHeartbeat(nowMs);
}
