#pragma once

#include <cstdint>
#include <cstddef>

namespace sumi {

/**
 * Caches the rendered 1-bit sleep screen framebuffer to SD card.
 * Cache key: FNV-1a hash of (sleep mode + image path + theme hash).
 * Cache hit: ~50ms (SD read) vs 1-3s (BMP parse + dither + render).
 */
class SleepScreenCache {
public:
  static constexpr const char* CACHE_DIR = "/.sumi/sleep_cache";
  static constexpr size_t BUFFER_SIZE = 48000;  // 800x480/8

  // Try to load cached framebuffer. Returns true if cache hit.
  static bool load(uint32_t cacheKey, uint8_t* framebuffer);

  // Save framebuffer to cache after rendering.
  static void save(uint32_t cacheKey, const uint8_t* framebuffer);

  // Compute cache key from current sleep settings + image path
  static uint32_t computeKey(uint8_t sleepMode, const char* imagePath);

  // Clear all cached screens
  static void clearAll();

private:
  // FNV-1a 32-bit hash
  static uint32_t fnv1a(const uint8_t* data, size_t len);
};

}  // namespace sumi
