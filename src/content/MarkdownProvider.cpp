#include "MarkdownProvider.h"

#include <cstring>

#include <Utf8.h>

namespace sumi {

Result<void> MarkdownProvider::open(const char* path, const char* cacheDir) {
  close();

  markdown.reset(new Markdown(path, cacheDir));

  if (!markdown->load()) {
    markdown.reset();
    return ErrVoid(Error::ParseFailed);
  }

  // Populate metadata
  meta.clear();
  meta.type = ContentType::Markdown;

  // UTF-8 safe copy — titles may contain CJK; see EpubProvider for rationale.
  utf8SafeCopy(meta.title, markdown->getTitle().c_str(), sizeof(meta.title));
  meta.author[0] = '\0';  // Markdown doesn't have author
  utf8SafeCopy(meta.cachePath, markdown->getCachePath().c_str(), sizeof(meta.cachePath));
  utf8SafeCopy(meta.coverPath, markdown->getCoverBmpPath().c_str(), sizeof(meta.coverPath));

  // Markdown uses file size, not pages
  meta.totalPages = 1;  // Will be updated by reader
  meta.currentPage = 0;
  meta.progressPercent = 0;

  return Ok();
}

void MarkdownProvider::close() {
  markdown.reset();
  meta.clear();
}

uint32_t MarkdownProvider::pageCount() const {
  if (!markdown) return 0;

  // Estimate pages based on file size (same as TXT)
  constexpr size_t BYTES_PER_PAGE = 2048;
  size_t fileSize = markdown->getFileSize();
  return (fileSize + BYTES_PER_PAGE - 1) / BYTES_PER_PAGE;
}

}  // namespace sumi
