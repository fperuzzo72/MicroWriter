#include "EpubProvider.h"

#include <cstring>

#include <Utf8.h>

namespace sumi {

Result<void> EpubProvider::open(const char* path, const char* cacheDir) {
  close();

  epub = std::make_shared<Epub>(path, cacheDir);

  if (!epub->load()) {
    epub.reset();
    return ErrVoid(Error::ParseFailed);
  }

  // Populate metadata
  meta.clear();
  meta.type = ContentType::Epub;

  // UTF-8 safe copy: titles/authors may contain CJK text. A naive strncpy
  // into a fixed-size buffer can slice a 3-byte codepoint mid-sequence,
  // which later renders as '?' at the broken character.
  utf8SafeCopy(meta.title, epub->getTitle().c_str(), sizeof(meta.title));
  utf8SafeCopy(meta.author, epub->getAuthor().c_str(), sizeof(meta.author));

  // Paths are ASCII-safe but reuse the helper for consistency.
  utf8SafeCopy(meta.cachePath, epub->getCachePath().c_str(), sizeof(meta.cachePath));
  utf8SafeCopy(meta.coverPath, epub->getCoverBmpPath().c_str(), sizeof(meta.coverPath));

  meta.totalPages = epub->getSpineItemsCount();
  meta.currentPage = 0;
  meta.progressPercent = 0;

  // Parse sumi: content hint from dc:subject
  const std::string& subject = epub->getSubject();
  meta.hint = parseContentHint(subject.c_str());

  return Ok();
}

void EpubProvider::close() {
  epub.reset();
  meta.clear();
}

uint32_t EpubProvider::pageCount() const { return epub ? epub->getSpineItemsCount() : 0; }

uint16_t EpubProvider::tocCount() const { return epub ? epub->getTocItemsCount() : 0; }

Result<TocEntry> EpubProvider::getTocEntry(uint16_t index) const {
  if (!epub || index >= tocCount()) {
    return Err<TocEntry>(Error::InvalidState);
  }

  auto tocItem = epub->getTocItem(index);
  TocEntry entry;
  utf8SafeCopy(entry.title, tocItem.title.c_str(), sizeof(entry.title));
  entry.pageIndex = epub->getSpineIndexForTocIndex(index);
  entry.depth = tocItem.level;

  return Ok(entry);
}

}  // namespace sumi
