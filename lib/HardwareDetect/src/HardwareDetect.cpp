#include "HardwareDetect.h"

#include <Arduino.h>
#include <Preferences.h>
#include <Wire.h>

namespace sumi {

// Static initialization
DeviceType HardwareDetect::current_ = DeviceType::X4;

namespace {

// NVS storage (matches Crosspoint's key names so future users with an X3
// already booted with Crosspoint get "for free" detection cache).
constexpr char HW_NAMESPACE[] = "cphw";
constexpr char NVS_KEY_DEV_OVERRIDE[] = "dev_ovr";  // 0=auto, 1=X4, 2=X3
constexpr char NVS_KEY_DEV_CACHED[] = "dev_det";    // 0=unknown, 1=X4, 2=X3

// X3 I2C pins + frequency
constexpr int X3_I2C_SDA = 20;
constexpr int X3_I2C_SCL = 0;
constexpr uint32_t X3_I2C_FREQ = 400000;

// X3-specific chip addresses
constexpr uint8_t I2C_ADDR_BQ27220 = 0x55;  // Fuel gauge
constexpr uint8_t BQ27220_SOC_REG = 0x2C;   // State of charge (%)
constexpr uint8_t BQ27220_VOLT_REG = 0x08;  // Voltage (mV)

constexpr uint8_t I2C_ADDR_DS3231 = 0x68;  // RTC
constexpr uint8_t DS3231_SEC_REG = 0x00;   // Seconds (BCD)

constexpr uint8_t I2C_ADDR_QMI8658 = 0x6B;       // IMU
constexpr uint8_t I2C_ADDR_QMI8658_ALT = 0x6A;   // Fallback address
constexpr uint8_t QMI8658_WHO_AM_I_REG = 0x00;
constexpr uint8_t QMI8658_WHO_AM_I_VALUE = 0x05;

// NVS encoding (matches Crosspoint exactly):
//   0 = Unknown (never detected — auto-probe)
//   1 = X4
//   2 = X3
enum class NvsDeviceValue : uint8_t { Unknown = 0, X4 = 1, X3 = 2 };

NvsDeviceValue readNvsDeviceValue(const char* key, NvsDeviceValue fallback) {
  Preferences prefs;
  if (!prefs.begin(HW_NAMESPACE, /*readOnly=*/true)) {
    return fallback;
  }
  const uint8_t raw = prefs.getUChar(key, static_cast<uint8_t>(fallback));
  prefs.end();
  if (raw == static_cast<uint8_t>(NvsDeviceValue::X4) ||
      raw == static_cast<uint8_t>(NvsDeviceValue::X3)) {
    return static_cast<NvsDeviceValue>(raw);
  }
  return NvsDeviceValue::Unknown;
}

void writeNvsDeviceValue(const char* key, NvsDeviceValue value) {
  Preferences prefs;
  if (!prefs.begin(HW_NAMESPACE, /*readOnly=*/false)) {
    return;
  }
  prefs.putUChar(key, static_cast<uint8_t>(value));
  prefs.end();
}

DeviceType nvsToDeviceType(NvsDeviceValue value) {
  return value == NvsDeviceValue::X3 ? DeviceType::X3 : DeviceType::X4;
}

struct X3ProbeResult {
  bool bq27220 = false;
  bool ds3231 = false;
  bool qmi8658 = false;

  uint8_t score() const {
    return static_cast<uint8_t>(bq27220) + static_cast<uint8_t>(ds3231) + static_cast<uint8_t>(qmi8658);
  }
};

bool readI2CReg8(uint8_t addr, uint8_t reg, uint8_t* outValue) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }
  if (Wire.requestFrom(addr, static_cast<uint8_t>(1), static_cast<uint8_t>(true)) < 1) {
    return false;
  }
  *outValue = Wire.read();
  return true;
}

bool readI2CReg16LE(uint8_t addr, uint8_t reg, uint16_t* outValue) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }
  if (Wire.requestFrom(addr, static_cast<uint8_t>(2), static_cast<uint8_t>(true)) < 2) {
    while (Wire.available()) {
      Wire.read();
    }
    return false;
  }
  const uint8_t lo = Wire.read();
  const uint8_t hi = Wire.read();
  *outValue = (static_cast<uint16_t>(hi) << 8) | lo;
  return true;
}

// BQ27220 signature: state of charge (0-100) and a reasonable voltage reading.
bool probeBQ27220Signature() {
  uint16_t soc = 0;
  uint16_t voltageMv = 0;
  if (!readI2CReg16LE(I2C_ADDR_BQ27220, BQ27220_SOC_REG, &soc)) {
    return false;
  }
  if (soc > 100) {
    return false;
  }
  if (!readI2CReg16LE(I2C_ADDR_BQ27220, BQ27220_VOLT_REG, &voltageMv)) {
    return false;
  }
  return voltageMv >= 2500 && voltageMv <= 5000;
}

// DS3231 signature: seconds register in BCD format (tens<=5, ones<=9).
bool probeDS3231Signature() {
  uint8_t sec = 0;
  if (!readI2CReg8(I2C_ADDR_DS3231, DS3231_SEC_REG, &sec)) {
    return false;
  }
  const uint8_t tensDigit = (sec >> 4) & 0x07;
  const uint8_t onesDigit = sec & 0x0F;
  return tensDigit <= 5 && onesDigit <= 9;
}

// QMI8658 signature: WHO_AM_I register returns 0x05.
bool probeQMI8658Signature() {
  uint8_t whoami = 0;
  if (readI2CReg8(I2C_ADDR_QMI8658, QMI8658_WHO_AM_I_REG, &whoami) && whoami == QMI8658_WHO_AM_I_VALUE) {
    return true;
  }
  if (readI2CReg8(I2C_ADDR_QMI8658_ALT, QMI8658_WHO_AM_I_REG, &whoami) && whoami == QMI8658_WHO_AM_I_VALUE) {
    return true;
  }
  return false;
}

X3ProbeResult runX3ProbePass() {
  X3ProbeResult result;
  Wire.begin(X3_I2C_SDA, X3_I2C_SCL, X3_I2C_FREQ);
  Wire.setTimeOut(6);

  result.bq27220 = probeBQ27220Signature();
  result.ds3231 = probeDS3231Signature();
  result.qmi8658 = probeQMI8658Signature();

  Wire.end();
  // Release I2C pins so they don't interfere with X4. Both must use
  // INPUT_PULLDOWN, not bare INPUT — otherwise:
  //   GPIO 20 (UART0_RXD on X4) floats and isUsbConnected reads junk,
  //   misfiring Serial.begin. Cause of intermittent "where are my boot
  //   logs" reports on otherwise-healthy hardware.
  //   GPIO 0 (battery ADC on X4) similarly floats; downstream battery
  //   reads sample whatever leakage is on the trace.
  // The chip's INPUT_PULLDOWN is ~45 kΩ — weak enough to be overpowered
  // by any actual signal source (USB host, ADC reference), strong
  // enough to guarantee a clean LOW when the pin is unconnected.
  pinMode(20, INPUT_PULLDOWN);
  pinMode(0, INPUT_PULLDOWN);
  return result;
}

DeviceType detectDeviceTypeWithFingerprint() {
  // Explicit override for recovery/support:
  //   0 = auto, 1 = force X4, 2 = force X3
  const NvsDeviceValue overrideValue = readNvsDeviceValue(NVS_KEY_DEV_OVERRIDE, NvsDeviceValue::Unknown);
  if (overrideValue == NvsDeviceValue::X3 || overrideValue == NvsDeviceValue::X4) {
    if (Serial) {
      Serial.printf("[HW] Device override active: %s\n", overrideValue == NvsDeviceValue::X3 ? "X3" : "X4");
    }
    return nvsToDeviceType(overrideValue);
  }

  const NvsDeviceValue cachedValue = readNvsDeviceValue(NVS_KEY_DEV_CACHED, NvsDeviceValue::Unknown);
  if (cachedValue == NvsDeviceValue::X3 || cachedValue == NvsDeviceValue::X4) {
    if (Serial) {
      Serial.printf("[HW] Using cached device type: %s\n", cachedValue == NvsDeviceValue::X3 ? "X3" : "X4");
    }
    return nvsToDeviceType(cachedValue);
  }

  // No cache yet: run two active X3 fingerprint probes and persist the result.
  // Two passes rejects transient I2C noise on first boot.
  const X3ProbeResult pass1 = runX3ProbePass();
  delay(2);
  const X3ProbeResult pass2 = runX3ProbePass();

  const uint8_t score1 = pass1.score();
  const uint8_t score2 = pass2.score();
  if (Serial) {
    Serial.printf("[HW] X3 probe scores: pass1=%u(bq=%d rtc=%d imu=%d) pass2=%u(bq=%d rtc=%d imu=%d)\n",
                  score1, pass1.bq27220, pass1.ds3231, pass1.qmi8658,
                  score2, pass2.bq27220, pass2.ds3231, pass2.qmi8658);
  }
  const bool x3Confirmed = (score1 >= 2) && (score2 >= 2);
  const bool x4Confirmed = (score1 == 0) && (score2 == 0);

  if (x3Confirmed) {
    writeNvsDeviceValue(NVS_KEY_DEV_CACHED, NvsDeviceValue::X3);
    if (Serial) Serial.println("[HW] Detected device: X3 (cached)");
    return DeviceType::X3;
  }

  if (x4Confirmed) {
    writeNvsDeviceValue(NVS_KEY_DEV_CACHED, NvsDeviceValue::X4);
    if (Serial) Serial.println("[HW] Detected device: X4 (cached)");
    return DeviceType::X4;
  }

  // Conservative fallback for first boot with inconclusive probes.
  // Don't cache this — try again on next boot.
  if (Serial) Serial.println("[HW] Inconclusive probe — falling back to X4");
  return DeviceType::X4;
}

}  // namespace

DeviceType HardwareDetect::detect() {
  current_ = detectDeviceTypeWithFingerprint();
  return current_;
}

void HardwareDetect::setOverride(DeviceType type) {
  NvsDeviceValue value = (type == DeviceType::X3) ? NvsDeviceValue::X3 : NvsDeviceValue::X4;
  writeNvsDeviceValue(NVS_KEY_DEV_OVERRIDE, value);
  if (Serial) {
    Serial.printf("[HW] Override set: %s. Reboot to apply.\n", type == DeviceType::X3 ? "X3" : "X4");
  }
}

void HardwareDetect::clearOverride() {
  Preferences prefs;
  if (prefs.begin(HW_NAMESPACE, /*readOnly=*/false)) {
    prefs.remove(NVS_KEY_DEV_OVERRIDE);
    prefs.end();
  }
  if (Serial) Serial.println("[HW] Override cleared. Reboot for auto-detect.");
}

}  // namespace sumi
