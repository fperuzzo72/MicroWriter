#include "PlainTextParser.h"

#include <Epub/Page.h>
#include <Epub/ParsedText.h>
#include <Epub/hyphenation/Hyphenator.h>
#include <GfxRenderer.h>
#include <SDCardManager.h>
#include <Utf8.h>

#include <utility>

namespace {
constexpr size_t READ_CHUNK_SIZE = 4096;

bool isWhitespace(char c) { return c == ' ' || c == '\r' || c == '\n' || c == '\t'; }
}  // namespace

PlainTextParser::PlainTextParser(std::string filepath, GfxRenderer& renderer, const RenderConfig& config)
    : filepath_(std::move(filepath)), renderer_(renderer), config_(config) {}

void PlainTextParser::reset() {
  currentOffset_ = 0;
  hasMore_ = true;
  isRtl_ = false;
}

bool PlainTextParser::parsePages(const std::function<void(std::unique_ptr<Page>)>& onPageComplete, uint16_t maxPages,
                                 const AbortCallback& shouldAbort) {
  // Hyphenator::setPreferredLanguage holds a process-global cached
  // language picked from the last EPUB that was opened. Without this
  // reset, opening a French EPUB then a plain .txt would apply French
  // hyphenation patterns to the TXT content (and vice versa). TXT has
  // no language metadata — clear the preference so fallback hyphenation
  // (the safe generic algorithm) applies instead of potentially
  // wrong-language patterns.
  Hyphenator::setPreferredLanguage("");

  FsFile file;
  if (!SdMan.openFileForRead("TXT", filepath_, file)) {
    Serial.printf("[TXT] Failed to open file: %s\n", filepath_.c_str());
    return false;
  }

  fileSize_ = file.size();
  if (currentOffset_ > 0) {
    file.seek(currentOffset_);
  }

  const int lineHeight = static_cast<int>(renderer_.getLineHeight(config_.fontId) * config_.lineCompression);
  const int maxLinesPerPage = config_.viewportHeight / lineHeight;

  uint8_t buffer[READ_CHUNK_SIZE + 1];
  std::unique_ptr<ParsedText> currentBlock;
  std::unique_ptr<Page> currentPage;
  int16_t currentPageY = 0;
  uint16_t pagesCreated = 0;
  std::string partialWord;
  uint16_t abortCheckCounter = 0;

  auto startNewPage = [&]() {
    currentPage.reset(new Page());
    currentPageY = 0;
  };

  auto addLineToPage = [&](std::shared_ptr<TextBlock> line) {
    if (!currentPage) {
      startNewPage();
    }

    if (currentPageY + lineHeight > config_.viewportHeight) {
      onPageComplete(std::move(currentPage));
      pagesCreated++;
      startNewPage();

      if (maxPages > 0 && pagesCreated >= maxPages) {
        return false;
      }
    }

    currentPage->elements.push_back(std::make_shared<PageLine>(line, 0, currentPageY));
    currentPageY += lineHeight;
    return true;
  };

  auto flushBlock = [&]() -> bool {
    if (!currentBlock || currentBlock->isEmpty()) return true;

    bool continueProcessing = true;
    currentBlock->layoutAndExtractLines(renderer_, config_.fontId, config_.viewportWidth,
                                        [&](const std::shared_ptr<TextBlock>& line) {
                                          if (!continueProcessing) return;
                                          if (!addLineToPage(line)) {
                                            continueProcessing = false;
                                          }
                                        });

    currentBlock.reset();
    return continueProcessing;
  };

  if (currentOffset_ == 0) {
    // FsFile::read returns int: positive bytesRead or negative on error.
    // Previously we stored it directly into `size_t`, which turned a -1
    // error into SIZE_MAX and the subsequent `buffer[peekBytes] = '\0'`
    // would write far out of bounds.
    const int peekBytes = file.read(buffer, READ_CHUNK_SIZE);
    if (peekBytes > 0) {
      buffer[peekBytes] = '\0';
      isRtl_ = ScriptDetector::containsArabic(reinterpret_cast<const char*>(buffer));
    }
    file.seekSet(0);
  }

  startNewPage();
  currentBlock.reset(new ParsedText(static_cast<TextBlock::BLOCK_STYLE>(config_.paragraphAlignment),
                                    config_.indentLevel, config_.hyphenation, true, isRtl_));

  while (file.available() > 0) {
    // Check for abort every few iterations
    if (shouldAbort && (++abortCheckCounter % 10 == 0) && shouldAbort()) {
      Serial.printf("[TXT] Aborted by external request\n");
      currentOffset_ = file.position();
      hasMore_ = true;
      file.close();
      return false;
    }

    const int bytesRead = file.read(buffer, READ_CHUNK_SIZE);
    if (bytesRead <= 0) break;  // 0 = EOF, <0 = I/O error — abort the loop either way

    buffer[bytesRead] = '\0';

    for (size_t i = 0; i < bytesRead; i++) {
      char c = static_cast<char>(buffer[i]);

      // Handle newlines as paragraph breaks
      if (c == '\n') {
        // Flush partial word
        if (!partialWord.empty()) {
          currentBlock->addWord(partialWord, EpdFontFamily::REGULAR);
          partialWord.clear();
        }

        // Flush current block (paragraph)
        if (!flushBlock()) {
          currentOffset_ = file.position() - (bytesRead - i - 1);
          hasMore_ = true;
          file.close();

          // Complete final page if it has content
          if (currentPage && !currentPage->elements.empty()) {
            onPageComplete(std::move(currentPage));
          }
          return true;
        }

        // Start new paragraph
        currentBlock.reset(new ParsedText(static_cast<TextBlock::BLOCK_STYLE>(config_.paragraphAlignment),
                                          config_.indentLevel, config_.hyphenation, true, isRtl_));

        // Add paragraph spacing
        switch (config_.spacingLevel) {
          case 1:
            currentPageY += lineHeight / 4;
            break;
          case 3:
            currentPageY += lineHeight;
            break;
        }
        continue;
      }

      if (isWhitespace(c)) {
        if (!partialWord.empty()) {
          currentBlock->addWord(partialWord, EpdFontFamily::REGULAR);
          partialWord.clear();
        }
        continue;
      }

      // Unicode whitespace (NBSP, narrow NBSP, EN/EM/thin spaces,
      // ideographic space) also acts as a word boundary. See
      // lib/Utf8 utf8UnicodeWhitespaceBytes() for the full list.
      if (int skip = utf8UnicodeWhitespaceBytes(reinterpret_cast<const char*>(buffer) + i,
                                                static_cast<int>(bytesRead - i))) {
        if (!partialWord.empty()) {
          currentBlock->addWord(partialWord, EpdFontFamily::REGULAR);
          partialWord.clear();
        }
        i += static_cast<size_t>(skip - 1);  // loop increments
        continue;
      }

      partialWord += c;

      // Prevent extremely long words from accumulating. CJK text has no
      // inter-word whitespace so partialWord would grow unbounded without
      // this guard. When we do flush early, step back over any trailing
      // UTF-8 continuation bytes so the emitted word ends on a codepoint
      // boundary — otherwise a CJK character would be split across two
      // word fragments and render as '?' at the join.
      if (partialWord.length() > 100) {
        size_t safeLen = partialWord.length();
        while (safeLen > 0
               && (static_cast<unsigned char>(partialWord[safeLen - 1]) & 0xC0) == 0x80) {
          --safeLen;
        }
        // If the byte at safeLen-1 is a multi-byte lead, the continuation
        // bytes needed to complete that codepoint haven't arrived yet —
        // keep them in partialWord for the next iteration.
        if (safeLen > 0) {
          const unsigned char lead = static_cast<unsigned char>(partialWord[safeLen - 1]);
          if (lead >= 0xC0) --safeLen;
        }
        if (safeLen > 0) {
          currentBlock->addWord(partialWord.substr(0, safeLen), EpdFontFamily::REGULAR);
          partialWord.erase(0, safeLen);
        }
      }
    }

    // Check if we hit max pages
    if (maxPages > 0 && pagesCreated >= maxPages) {
      currentOffset_ = file.position();
      hasMore_ = (currentOffset_ < fileSize_);
      file.close();
      return true;
    }
  }

  // Flush remaining content
  if (!partialWord.empty()) {
    currentBlock->addWord(partialWord, EpdFontFamily::REGULAR);
  }
  flushBlock();

  // Complete final page
  if (currentPage && !currentPage->elements.empty()) {
    onPageComplete(std::move(currentPage));
    pagesCreated++;
  }

  file.close();
  currentOffset_ = fileSize_;
  hasMore_ = false;

  Serial.printf("[TXT] Parsed %d pages from %s\n", pagesCreated, filepath_.c_str());
  return true;
}
