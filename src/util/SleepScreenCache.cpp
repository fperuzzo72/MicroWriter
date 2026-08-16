#include "SleepScreenCache.h"

#include <Arduino.h>
#include <SDCardManager.h>

#include <cstring>

namespace sumi {

uint32_t SleepScreenCache::fnv1a(const uint8_t* data, size_t len) {
  uint32_t hash = 0x811c9dc5u;
  for (size_t i = 0; i < len; i++) {
    hash ^= data[i];
    hash *= 0x01000193u;
  }
  return hash;
}

uint32_t SleepScreenCache::computeKey(uint8_t sleepMode, const char* imagePath) {
  // Hash sleep mode byte
  uint32_t hash = fnv1a(&sleepMode, 1);

  // Fold in image path if present
  if (imagePath && imagePath[0]) {
    size_t pathLen = strlen(imagePath);
    // Continue the hash chain from where we left off
    for (size_t i = 0; i < pathLen; i++) {
      hash ^= static_cast<uint8_t>(imagePath[i]);
      hash *= 0x01000193u;
    }
  }

  return hash;
}

bool SleepScreenCache::load(uint32_t cacheKey, uint8_t* framebuffer) {
  if (!framebuffer) return false;

  char path[48];
  snprintf(path, sizeof(path), "%s/%08lx.raw", CACHE_DIR, (unsigned long)cacheKey);

  FsFile file;
  if (!SdMan.openFileForRead("SLC", path, file)) {
    return false;
  }

  // Validate file size before reading
  uint64_t fileSize = file.size();
  if (fileSize != BUFFER_SIZE) {
    Serial.printf("[SLC] Cache file size mismatch: %lu (expected %u)\n",
                  (unsigned long)fileSize, (unsigned)BUFFER_SIZE);
    file.close();
    return false;
  }

  int bytesRead = file.read(framebuffer, BUFFER_SIZE);
  file.close();

  if (bytesRead < 0 || static_cast<size_t>(bytesRead) != BUFFER_SIZE) {
    Serial.printf("[SLC] Cache read incomplete: %d bytes\n", bytesRead);
    return false;
  }

  Serial.printf("[SLC] Cache hit: %s\n", path);
  return true;
}

void SleepScreenCache::save(uint32_t cacheKey, const uint8_t* framebuffer) {
  if (!framebuffer) return;

  // Ensure cache directory exists
  SdMan.ensureDirectoryExists(CACHE_DIR);

  char path[48];
  snprintf(path, sizeof(path), "%s/%08lx.raw", CACHE_DIR, (unsigned long)cacheKey);

  FsFile file;
  if (!SdMan.openFileForWrite("SLC", path, file)) {
    Serial.println("[SLC] Failed to open cache file for writing");
    return;
  }

  size_t written = file.write(framebuffer, BUFFER_SIZE);
  SdMan.syncAndClose(file);

  if (written != BUFFER_SIZE) {
    Serial.printf("[SLC] Cache write incomplete: %u bytes\n", (unsigned)written);
    // Remove partial file
    SdMan.remove(path);
    return;
  }

  Serial.printf("[SLC] Cached: %s\n", path);
}

void SleepScreenCache::clearAll() {
  FsFile dir = SdMan.open(CACHE_DIR);
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    return;
  }

  char name[48];
  FsFile entry;
  int removed = 0;
  while (entry.openNext(&dir, O_RDONLY)) {
    if (entry.isDirectory()) {
      entry.close();
      continue;
    }
    entry.getName(name, sizeof(name));
    entry.close();

    // Only remove .raw files in the cache directory
    const char* ext = strrchr(name, '.');
    if (ext && strcasecmp(ext, ".raw") == 0) {
      char fullPath[80];
      snprintf(fullPath, sizeof(fullPath), "%s/%s", CACHE_DIR, name);
      SdMan.remove(fullPath);
      removed++;
    }
  }
  dir.close();

  Serial.printf("[SLC] Cleared %d cached sleep screen(s)\n", removed);
}

}  // namespace sumi
