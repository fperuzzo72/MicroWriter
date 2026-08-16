#pragma once
#include <cstdint>
#include <string>

namespace sumi {

// Per-book reader setting overrides, stored in <cachePath>/overrides.bin
// Value of -1 means "use global setting" (no override)
struct BookOverrides {
  int8_t fontSize = -1;        // -1 = global, 0-3 = override
  int8_t textDarkness = -1;    // -1 = global, 0-3 = override
  int8_t showImages = -1;      // -1 = global, 0-2 = override
  int8_t lineSpacing = -1;     // -1 = global, 0-3 = override
  int8_t hyphenation = -1;     // -1 = global, 0-1 = override

  bool hasAnyOverride() const {
    return fontSize != -1 || textDarkness != -1 || showImages != -1 ||
           lineSpacing != -1 || hyphenation != -1;
  }

  // Load from cache directory, returns default (all -1) if no file
  static BookOverrides load(const std::string& cachePath);

  // Save to cache directory
  static void save(const std::string& cachePath, const BookOverrides& overrides);
};

}  // namespace sumi
