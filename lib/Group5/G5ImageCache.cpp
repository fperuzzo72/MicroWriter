#include "G5ImageCache.h"

#include <SDCardManager.h>

bool G5ImageCache::compressToFile(const uint8_t* bitmap, int width, int height, const char* path) {
  if (!bitmap || width <= 0 || height <= 0 || !path) {
    return false;
  }

  // Validate dimensions fit in uint16_t header fields
  if (width > UINT16_MAX || height > UINT16_MAX) {
    return false;
  }

  const int rowBytes = (width + 7) / 8;

  // Estimate buffer size - Group5 typically achieves good compression,
  // but we allocate for worst case
  const size_t maxCompressedSize = estimateMaxCompressedSize(width, height);

  // Allocate compression buffer
  uint8_t* compressBuffer = new (std::nothrow) uint8_t[maxCompressedSize];
  if (!compressBuffer) {
    return false;
  }

  G5ENCODER encoder;
  int result = encoder.init(width, height, compressBuffer, maxCompressedSize);
  if (result != G5_SUCCESS) {
    delete[] compressBuffer;
    return false;
  }

  // Encode all rows
  for (int y = 0; y < height; y++) {
    result = encoder.encodeLine(const_cast<uint8_t*>(bitmap + y * rowBytes));
    if (result != G5_SUCCESS && result != G5_ENCODE_COMPLETE) {
      delete[] compressBuffer;
      return false;
    }
  }

  const int compressedSize = encoder.size();

  // Write to file
  FsFile outFile;
  if (!SdMan.openFileForWrite("G5C", path, outFile)) {
    delete[] compressBuffer;
    return false;
  }

  // Write header
  G5ImageHeader header;
  header.magic = G5_MAGIC;
  header.width = width;
  header.height = height;
  header.compressedSize = compressedSize;

  if (outFile.write(reinterpret_cast<uint8_t*>(&header), sizeof(header)) != sizeof(header)) {
    outFile.close();
    SdMan.remove(path);
    delete[] compressBuffer;
    return false;
  }

  // Write compressed data
  if (outFile.write(compressBuffer, compressedSize) != static_cast<size_t>(compressedSize)) {
    outFile.close();
    SdMan.remove(path);
    delete[] compressBuffer;
    return false;
  }

  // syncAndClose so the compressed (multi-sector) payload is committed to
  // disk. SdFat will silently truncate a bare close() on multi-sector
  // writes; a dirty shutdown after this path would leave a half-written
  // .g5 file that then fails magic/size checks on next open.
  SdMan.syncAndClose(outFile);
  delete[] compressBuffer;
  return true;
}

bool G5ImageCache::decompressFromFile(const char* path, std::function<void(const uint8_t*, int, int)> rowCallback) {
  if (!path || !rowCallback) {
    return false;
  }

  FsFile inFile;
  if (!SdMan.openFileForRead("G5C", path, inFile)) {
    return false;
  }

  // Read header
  G5ImageHeader header;
  if (inFile.read(reinterpret_cast<uint8_t*>(&header), sizeof(header)) != sizeof(header)) {
    inFile.close();
    return false;
  }

  if (header.magic != G5_MAGIC) {
    inFile.close();
    return false;
  }

  // Sanity-cap untrusted header fields before allocating. A corrupted
  // .g5c cache with compressedSize = 0xFFFFFFFF would try to allocate
  // ~4 GB on a 380KB-heap ESP32-C3 and abort() the device. Real Group5
  // images top out around 50-100 KB compressed; 512 KB is a generous
  // ceiling that still catches the pathological case. Width/height
  // come from the same header so they need bounds too — max SUMI
  // screen dimensions are 800×800 at worst (landscape+overscan), so
  // 4096 is conservatively huge.
  constexpr uint32_t kMaxCompressedSize = 512u * 1024u;
  constexpr uint32_t kMaxDimension = 4096u;
  if (header.compressedSize == 0 || header.compressedSize > kMaxCompressedSize ||
      header.width == 0 || header.width > kMaxDimension ||
      header.height == 0 || header.height > kMaxDimension) {
    Serial.printf("[G5C] Implausible header: w=%u h=%u cs=%u — discarding cache\n",
                  header.width, header.height, header.compressedSize);
    inFile.close();
    return false;
  }

  const int rowBytes = (header.width + 7) / 8;

  // Allocate buffers
  uint8_t* compressedData = new (std::nothrow) uint8_t[header.compressedSize];
  uint8_t* rowBuffer = new (std::nothrow) uint8_t[rowBytes];

  if (!compressedData || !rowBuffer) {
    delete[] compressedData;
    delete[] rowBuffer;
    inFile.close();
    return false;
  }

  // Read compressed data
  if (inFile.read(compressedData, header.compressedSize) != header.compressedSize) {
    delete[] compressedData;
    delete[] rowBuffer;
    inFile.close();
    return false;
  }
  inFile.close();

  // Decode
  G5DECODER decoder;
  int result = decoder.init(header.width, header.height, compressedData, header.compressedSize);
  if (result != G5_SUCCESS) {
    delete[] compressedData;
    delete[] rowBuffer;
    return false;
  }

  for (int y = 0; y < header.height; y++) {
    result = decoder.decodeLine(rowBuffer);
    if (result != G5_SUCCESS && result != G5_DECODE_COMPLETE) {
      delete[] compressedData;
      delete[] rowBuffer;
      return false;
    }
    rowCallback(rowBuffer, rowBytes, y);
  }

  delete[] compressedData;
  delete[] rowBuffer;
  return true;
}

bool G5ImageCache::readHeader(const char* path, G5ImageHeader& header) {
  if (!path) {
    return false;
  }

  FsFile inFile;
  if (!SdMan.openFileForRead("G5C", path, inFile)) {
    return false;
  }

  if (inFile.read(reinterpret_cast<uint8_t*>(&header), sizeof(header)) != sizeof(header)) {
    inFile.close();
    return false;
  }
  inFile.close();

  return header.magic == G5_MAGIC;
}

size_t G5ImageCache::estimateMaxCompressedSize(int width, int height) {
  // Group5 can theoretically expand data in worst case (random noise)
  // Worst case: horizontal mode with long codes for every pair
  // Safe estimate: raw size + 50% overhead
  const size_t rawSize = static_cast<size_t>((width + 7) / 8) * height;
  return rawSize + (rawSize / 2) + 1024;  // Extra margin for safety
}
