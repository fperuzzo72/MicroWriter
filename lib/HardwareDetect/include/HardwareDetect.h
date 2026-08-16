#pragma once
#include <cstdint>

// Hardware detection for Xteink X3 vs X4.
//
// The X3 e-reader shares most of its hardware design with the X4 (same
// ESP32-C3 MCU, same button layout, same SPI pin assignments for the
// e-ink panel), but differs in:
//
//   1. Display panel: 792x528 vs X4's 800x480, and a completely
//      different display controller IC with its own command set.
//
//   2. I2C peripherals: the X3 adds three chips that aren't present on
//      the X4:
//        - BQ27220 (TI fuel gauge) at 0x55
//        - DS3231 (analog RTC) at 0x68
//        - QMI8658 (QST IMU) at 0x6B or 0x6A
//
// We detect which device we're running on by I2C-probing for the X3-only
// chips at boot. Detection runs twice (to reject transient noise) and
// caches the result to NVS flash, so subsequent boots skip the probe.
//
// Ported from Crosspoint master's open-x4-sdk.

namespace sumi {

enum class DeviceType : uint8_t {
  X4 = 0,  // Default / original Xteink X4 (800x480)
  X3 = 1,  // Xteink X3 (792x528, different controller)
};

class HardwareDetect {
 public:
  // Run the detection logic. Called once at boot before display init.
  // Reads NVS override first ("cphw:dev_ovr" — 0=auto, 1=force X4, 2=force X3).
  // Then reads cached result ("cphw:dev_det").
  // On first boot, probes I2C and caches the result.
  static DeviceType detect();

  // Get the detected type (after detect() has been called).
  static DeviceType current() { return current_; }
  static bool isX3() { return current_ == DeviceType::X3; }
  static bool isX4() { return current_ == DeviceType::X4; }

  // Force-set the device type (for testing / recovery). Writes to NVS override.
  static void setOverride(DeviceType type);
  // Clear the override (go back to auto-detect).
  static void clearOverride();

 private:
  static DeviceType current_;
};

}  // namespace sumi
