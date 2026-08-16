#pragma once
#include <HardwareSerial.h>
#include <SdFat.h>

#include <iostream>

// ──────────────────────────────────────────────────────────────────────
// On-disk endianness contract
// ──────────────────────────────────────────────────────────────────────
//
// writePod / readPod copy raw bytes between memory and a stream. They
// do NOT swap bytes. The on-disk representation is therefore the host's
// NATIVE byte order at the time of write.
//
// SUMI ships exclusively on ESP32-C3 (RISC-V, little-endian). Every
// .bin file format on disk is little-endian. If the firmware is ever
// ported to a big-endian target, all multi-byte writePod / readPod call
// sites must be wrapped in explicit htole32 / le32toh helpers BEFORE the
// new firmware ships, or files written by existing C3 devices will read
// with byte-swapped numerics on the BE host. Audit #22.
// ──────────────────────────────────────────────────────────────────────

namespace serialization {
template <typename T>
static void writePod(std::ostream& os, const T& value) {
  os.write(reinterpret_cast<const char*>(&value), sizeof(T));
}

template <typename T>
static void writePod(FsFile& file, const T& value) {
  file.write(reinterpret_cast<const uint8_t*>(&value), sizeof(T));
}

template <typename T>
static void readPod(std::istream& is, T& value) {
  is.read(reinterpret_cast<char*>(&value), sizeof(T));
}

template <typename T>
static void readPod(FsFile& file, T& value) {
  // Zero-initialize before read so a partial/failed read leaves a
  // deterministic value instead of stack garbage. The return value
  // is not checked here (callers validate), but SIZE_MAX-from-memset
  // is still better than random stack bits.
  T temp{};
  if (file.read(reinterpret_cast<uint8_t*>(&temp), sizeof(T)) == static_cast<int>(sizeof(T))) {
    value = temp;
  }
  // On failure: `value` retains its prior default (from the struct initializer).
}

template <typename T>
[[nodiscard]] static bool readPodChecked(FsFile& file, T& value) {
  return file.read(reinterpret_cast<uint8_t*>(&value), sizeof(T)) == sizeof(T);
}

static void writeString(std::ostream& os, const std::string& s) {
  const uint32_t len = s.size();
  writePod(os, len);
  os.write(s.data(), len);
}

static void writeString(FsFile& file, const std::string& s) {
  const uint32_t len = s.size();
  writePod(file, len);
  file.write(reinterpret_cast<const uint8_t*>(s.data()), len);
}

[[nodiscard]] static bool readString(std::istream& is, std::string& s) {
  uint32_t len;
  readPod(is, len);
  if (!is.good()) {
    s.clear();
    return false;
  }
  if (len > 65536) {  // Sanity check: no string should be > 64KB
    s.clear();
    is.setstate(std::ios::failbit);
    return false;
  }
  s.resize(len);
  is.read(&s[0], len);
  return is.good();
}

[[nodiscard]] static bool readString(FsFile& file, std::string& s) {
  uint32_t len;
  if (file.read(reinterpret_cast<uint8_t*>(&len), sizeof(len)) != sizeof(len)) {
    s.clear();
    return false;
  }
  if (len > 65536) {  // Sanity check: no string should be > 64KB
    Serial.printf("[SER] String length %u exceeds max, file corrupt\n", len);
    s.clear();
    return false;
  }
  s.resize(len);
  if (len > 0 && file.read(reinterpret_cast<uint8_t*>(&s[0]), len) != static_cast<int>(len)) {
    s.clear();
    return false;
  }
  return true;
}

template <typename T>
static void readPodValidated(FsFile& file, T& value, T maxValue) {
  T temp = 0;
  // Only commit the read if it actually filled the full type. A truncated
  // read would leave `temp` uninitialized and any subsequent < maxValue
  // check would be comparing against stack garbage; if the garbage happened
  // to be below the max, we'd write corrupted bytes into the live setting.
  const int bytesRead = file.read(reinterpret_cast<uint8_t*>(&temp), sizeof(T));
  if (bytesRead != static_cast<int>(sizeof(T))) {
    return;  // Leave `value` at its prior default.
  }
  if (temp < maxValue) {
    value = temp;
  }
}
}  // namespace serialization
