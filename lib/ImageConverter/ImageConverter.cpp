#include "ImageConverter.h"

#include <FsHelpers.h>
#include <HardwareSerial.h>
#include <JpegToBmpConverter.h>
#include <PngToBmpConverter.h>
#include <SDCardManager.h>

namespace {

class JpegImageConverter : public ImageConverter {
 public:
  bool convert(FsFile& input, Print& output, const ImageConvertConfig& config) override {
    // Quick mode: simple threshold instead of dithering
    if (config.quickMode) {
      return JpegToBmpConverter::jpegFileToBmpStreamQuick(input, output, config.maxWidth, config.maxHeight);
    }
    if (config.maxWidth == 480 && config.maxHeight == 800 && !config.shouldAbort) {
      return config.oneBit ? JpegToBmpConverter::jpegFileTo1BitBmpStream(input, output)
                           : JpegToBmpConverter::jpegFileToBmpStream(input, output);
    }
    return config.oneBit
               ? JpegToBmpConverter::jpegFileTo1BitBmpStreamWithSize(input, output, config.maxWidth, config.maxHeight)
               : JpegToBmpConverter::jpegFileToBmpStreamWithSize(input, output, config.maxWidth, config.maxHeight,
                                                                 config.shouldAbort);
  }

  const char* formatName() const override { return "JPEG"; }
};

class PngImageConverter : public ImageConverter {
 public:
  bool convert(FsFile& input, Print& output, const ImageConvertConfig& config) override {
    // Quick mode: simple threshold instead of dithering
    if (config.quickMode) {
      return PngToBmpConverter::pngFileToBmpStreamQuick(input, output, config.maxWidth, config.maxHeight);
    }
    // Note: PNG converter always produces 2-bit output. Unlike JPEG, PNG does not support
    // 1-bit dithering (oneBit flag is ignored). PNG thumbnails will be slightly larger but
    // render at the same speed since the display hardware handles both formats equally.
    return PngToBmpConverter::pngFileToBmpStreamWithSize(input, output, config.maxWidth, config.maxHeight,
                                                         config.shouldAbort);
  }

  const char* formatName() const override { return "PNG"; }
};

class BmpImageConverter : public ImageConverter {
 public:
  bool convert(FsFile& input, Print& output, const ImageConvertConfig& config) override {
    (void)config;
    uint8_t buffer[512];
    while (input.available()) {
      // FsFile::read returns int — -1 on error. Storing directly into
      // size_t would turn that into SIZE_MAX and feed a bogus length to
      // Print::write, which would then iterate into unmapped memory.
      const int bytesRead = input.read(buffer, sizeof(buffer));
      if (bytesRead <= 0) return false;
      const size_t n = static_cast<size_t>(bytesRead);
      if (output.write(buffer, n) != n) {
        return false;
      }
    }
    return true;
  }

  const char* formatName() const override { return "BMP"; }
};

JpegImageConverter jpegConverter;
PngImageConverter pngConverter;
BmpImageConverter bmpConverter;

}  // namespace

ImageConverter* ImageConverterFactory::getConverter(const std::string& filePath) {
  if (FsHelpers::isJpegFile(filePath)) {
    return &jpegConverter;
  }
  if (FsHelpers::isPngFile(filePath)) {
    return &pngConverter;
  }
  if (FsHelpers::isBmpFile(filePath)) {
    return &bmpConverter;
  }
  return nullptr;
}

bool ImageConverterFactory::convertToBmp(const std::string& inputPath, const std::string& outputPath,
                                         const ImageConvertConfig& config) {
  ImageConverter* converter = getConverter(inputPath);
  if (!converter) {
    Serial.printf("[%lu] [%s] Unsupported image format: %s\n", millis(), config.logTag, inputPath.c_str());
    return false;
  }

  FsFile inputFile;
  if (!SdMan.openFileForRead(config.logTag, inputPath, inputFile)) {
    Serial.printf("[%lu] [%s] Failed to open input file: %s\n", millis(), config.logTag, inputPath.c_str());
    return false;
  }

  FsFile outputFile;
  if (!SdMan.openFileForWrite(config.logTag, outputPath, outputFile)) {
    inputFile.close();
    Serial.printf("[%lu] [%s] Failed to create output file: %s\n", millis(), config.logTag, outputPath.c_str());
    return false;
  }

  const bool success = converter->convert(inputFile, outputFile, config);

  inputFile.close();                 // reader — no sync needed
  SdMan.syncAndClose(outputFile);    // writer — force dirty sectors to disk

  if (success) {
    Serial.printf("[%lu] [%s] Converted %s to BMP: %s\n", millis(), config.logTag, converter->formatName(),
                  outputPath.c_str());
  } else {
    Serial.printf("[%lu] [%s] Failed to convert %s to BMP\n", millis(), config.logTag, converter->formatName());
    SdMan.remove(outputPath.c_str());
  }

  return success;
}

bool ImageConverterFactory::isSupported(const std::string& filePath) { return FsHelpers::isImageFile(filePath); }
