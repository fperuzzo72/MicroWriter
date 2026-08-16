// Regression test for paragraph truncation bug.
//
// Bug: when a paragraph spans more pages than the caller's maxPages budget,
// the parser used to suspend XML mid-block via `stopRequested_=true`. By the
// time XML actually suspended, control had returned up the stack to
// startNewTextBlock, which reset currentTextBlock — destroying any words
// from the old paragraph that hadn't been laid out into lines yet. Users
// saw paragraphs cut off at the top of subsequent pages after BLE upload
// or large EPUB ingest.
//
// Fix: the parser's completePageFn callback returning false is now a SOFT
// signal — the parser keeps producing pages for the remainder of the
// current block, then suspends XML at the next clean boundary. The wrapped
// callback in EpubChapterParser accepts overflow pages.
//
// This test models that contract at the block level (paragraphs ↔ lines)
// and asserts every word from the input survives end-to-end.
//
// The ChapterHtmlSlimParser source pulls in ESP/freertos headers and a
// renderer — too heavy for a host-side test. We model the contract here
// with a lightweight parser whose semantics match the fix; the real fix
// is verified end-to-end via the emulator smoke and the firmware integration test.

#include "test_utils.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

// A "page" carries one or more "lines" of words. We model a chapter as a
// sequence of paragraphs (blocks); each block is laid out into one or more
// lines. The parser emits a page when its line accumulator overflows the
// viewport.
struct Page {
  std::vector<std::string> lines;  // each line is space-joined words
};

using PageCallback = std::function<bool(std::unique_ptr<Page>)>;

// Mirrors the soft-suspend contract:
//   - addLineToPage emits a page when full, calls completePageFn
//   - if completePageFn returns false, set pendingMaxPagesSuspend_ but KEEP
//     adding lines so the rest of the current block is preserved
//   - at end of each block (block boundary), suspend if pending
class BlockAwareParser {
 public:
  // Each inner vector is a block's words. A block lays out into ceil(words/linesPerBlock) lines.
  BlockAwareParser(std::vector<std::vector<std::string>> blocks, size_t wordsPerLine, size_t linesPerPage)
      : blocks_(std::move(blocks)), wordsPerLine_(wordsPerLine), linesPerPage_(linesPerPage) {}

  // Returns true if more content remains (caller should call again).
  bool parsePages(const PageCallback& completePageFn, size_t maxPages) {
    pagesEmittedThisCall_ = 0;
    pendingMaxPagesSuspend_ = false;
    bool stopRequested = false;

    while (blockIdx_ < blocks_.size() && !stopRequested) {
      // Lay out the current block into lines starting from wordIdx_.
      const auto& block = blocks_[blockIdx_];
      while (wordIdx_ < block.size()) {
        // Build one line of up to wordsPerLine_ words.
        std::string line;
        for (size_t w = 0; w < wordsPerLine_ && wordIdx_ < block.size(); ++w) {
          if (!line.empty()) line += " ";
          line += block[wordIdx_++];
        }

        // addLineToPage: append to currentPage; when full, emit.
        currentLines_.push_back(std::move(line));
        if (currentLines_.size() >= linesPerPage_) {
          auto p = std::make_unique<Page>();
          p->lines = std::move(currentLines_);
          currentLines_.clear();
          if (!completePageFn(std::move(p))) {
            // Soft-stop: keep extracting until the block is drained,
            // suspend at the clean boundary below.
            pendingMaxPagesSuspend_ = true;
          }
          pagesEmittedThisCall_++;
        }
      }

      // End of block — clean boundary. Honour deferred soft-stop.
      blockIdx_++;
      wordIdx_ = 0;
      if (pendingMaxPagesSuspend_) {
        stopRequested = true;
      }
    }

    // End-of-content: flush trailing partial page (always — soft-stop is moot at EOF).
    if (blockIdx_ >= blocks_.size() && !currentLines_.empty()) {
      auto p = std::make_unique<Page>();
      p->lines = std::move(currentLines_);
      currentLines_.clear();
      completePageFn(std::move(p));
      pagesEmittedThisCall_++;
    }

    (void)maxPages;  // budget enforcement is done in the wrapped callback
    return blockIdx_ < blocks_.size();
  }

 private:
  std::vector<std::vector<std::string>> blocks_;
  size_t wordsPerLine_;
  size_t linesPerPage_;
  size_t blockIdx_ = 0;
  size_t wordIdx_ = 0;
  std::vector<std::string> currentLines_;
  bool pendingMaxPagesSuspend_ = false;
  size_t pagesEmittedThisCall_ = 0;
};

// Mirrors EpubChapterParser::wrappedCallback's new contract:
//   - ALWAYS emit the page (no early return)
//   - return false ONLY as a soft-stop signal
struct WrappedCallback {
  std::vector<std::unique_ptr<Page>> emitted;
  size_t budget;
  size_t pagesCreated = 0;
  bool hitMax = false;

  explicit WrappedCallback(size_t b) : budget(b) {}

  bool operator()(std::unique_ptr<Page> page) {
    emitted.push_back(std::move(page));
    pagesCreated++;
    if (budget > 0 && pagesCreated >= budget) {
      hitMax = true;
      return false;
    }
    return true;
  }
};

static std::vector<std::string> collectAllWords(const std::vector<std::unique_ptr<Page>>& pages) {
  std::vector<std::string> out;
  for (const auto& p : pages) {
    for (const auto& line : p->lines) {
      std::istringstream iss(line);
      std::string w;
      while (iss >> w) out.push_back(w);
    }
  }
  return out;
}

static std::vector<std::string> flattenInput(const std::vector<std::vector<std::string>>& blocks) {
  std::vector<std::string> out;
  for (const auto& b : blocks) {
    for (const auto& w : b) out.push_back(w);
  }
  return out;
}

int main() {
  TestUtils::TestRunner runner("SoftSuspendParagraph");

  // Test 1: One paragraph that spans more pages than maxPages allows.
  // Old hard-stop bug would lose words after the budget was hit.
  {
    std::vector<std::vector<std::string>> blocks = {{
        // 60 words in one paragraph -> 60/4 = 15 lines -> 15/3 = 5 pages
        "alpha", "bravo", "charlie", "delta", "echo", "foxtrot", "golf", "hotel",
        "india", "juliet", "kilo", "lima", "mike", "november", "oscar", "papa",
        "quebec", "romeo", "sierra", "tango", "uniform", "victor", "whiskey", "xray",
        "yankee", "zulu", "one", "two", "three", "four", "five", "six",
        "seven", "eight", "nine", "ten", "eleven", "twelve", "thirteen", "fourteen",
        "fifteen", "sixteen", "seventeen", "eighteen", "nineteen", "twenty", "twentyone", "twentytwo",
        "twentythree", "twentyfour", "twentyfive", "twentysix", "twentyseven", "twentyeight", "twentynine", "thirty",
        "thirtyone", "thirtytwo", "thirtythree", "thirtyfour"
    }};

    BlockAwareParser parser(blocks, /*wordsPerLine=*/4, /*linesPerPage=*/3);
    WrappedCallback cb(/*budget=*/2);  // budget < 5 pages this paragraph needs
    bool hasMore = parser.parsePages([&](std::unique_ptr<Page> p) { return cb(std::move(p)); }, 2);

    runner.expectTrue(cb.hitMax, "single_block_hit_max");
    // Single-block chapter — once the only block is fully drained there's
    // nothing more to do, even though we exceeded the budget.
    runner.expectFalse(hasMore, "single_block_no_more_after_drain");
    runner.expectEq(static_cast<size_t>(5), cb.emitted.size(), "single_block_drained_to_clean_boundary");

    // No words lost
    auto inputWords = flattenInput(blocks);
    auto outputWords = collectAllWords(cb.emitted);
    runner.expectEq(inputWords.size(), outputWords.size(), "single_block_word_count_match");
    bool wordsMatch = (inputWords == outputWords);
    runner.expectTrue(wordsMatch, "single_block_word_order_match");
  }

  // Test 2: Multi-block chapter — soft-stop suspends at block boundaries
  // across multiple parsePages calls, every word survives the round-trip.
  {
    std::vector<std::vector<std::string>> blocks = {
        {"para1-w1", "para1-w2", "para1-w3", "para1-w4", "para1-w5", "para1-w6", "para1-w7", "para1-w8"},
        {"para2-w1", "para2-w2", "para2-w3", "para2-w4", "para2-w5", "para2-w6"},
        {"para3-w1", "para3-w2", "para3-w3", "para3-w4", "para3-w5", "para3-w6", "para3-w7", "para3-w8",
         "para3-w9", "para3-w10", "para3-w11", "para3-w12"},
    };

    BlockAwareParser parser(blocks, /*wordsPerLine=*/2, /*linesPerPage=*/2);

    std::vector<std::unique_ptr<Page>> allPages;
    bool hasMore = true;
    int callCount = 0;

    // Drain the chapter across multiple budget-1 calls; each call must
    // hit the soft-stop AT a block boundary (never mid-block).
    while (hasMore && callCount < 20) {
      WrappedCallback cb(1);
      hasMore = parser.parsePages([&](std::unique_ptr<Page> p) { return cb(std::move(p)); }, 1);
      // At least one page emitted per call (otherwise we'd loop forever)
      runner.expectTrue(cb.emitted.size() >= 1, "multi_block_progress_each_call");
      for (auto& p : cb.emitted) allPages.push_back(std::move(p));
      callCount++;
    }
    runner.expectFalse(hasMore, "multi_block_eventually_complete");

    // Every word from the input must appear in the emitted pages, in order.
    auto inputWords = flattenInput(blocks);
    auto outputWords = collectAllWords(allPages);
    runner.expectEq(inputWords.size(), outputWords.size(), "multi_block_word_count_match");
    bool wordsMatch = (inputWords == outputWords);
    runner.expectTrue(wordsMatch, "multi_block_word_order_match");
  }

  // Test 3: Long paragraph (50 lines) like the Asimov / Heinlein bug reports,
  // followed by a small trailing paragraph. The long paragraph alone vastly
  // exceeds maxPages, so soft-stop must drain it completely; the trailing
  // paragraph is then carried over to the next parsePages call.
  {
    std::vector<std::string> longPara;
    for (int i = 1; i <= 200; ++i) {
      longPara.push_back("word" + std::to_string(i));
    }
    std::vector<std::string> tailPara{"tail-a", "tail-b", "tail-c", "tail-d"};
    std::vector<std::vector<std::string>> blocks = {longPara, tailPara};

    BlockAwareParser parser(blocks, /*wordsPerLine=*/4, /*linesPerPage=*/5);
    // 200 words / 4 = 50 lines / 5 = 10 pages for longPara
    WrappedCallback cb(/*budget=*/2);
    bool hasMore = parser.parsePages([&](std::unique_ptr<Page> p) { return cb(std::move(p)); }, 2);

    // Soft-stop drains the entire long paragraph before suspending.
    runner.expectTrue(cb.hitMax, "long_para_hit_max");
    runner.expectTrue(hasMore, "long_para_has_more_for_tail");
    runner.expectEq(static_cast<size_t>(10), cb.emitted.size(), "long_para_drained_all_10_pages");

    // All 200 words from the long para emitted in this first call.
    auto outputWords = collectAllWords(cb.emitted);
    runner.expectEq(static_cast<size_t>(200), outputWords.size(), "long_para_all_200_words_emitted");
  }

  // Test 4: Page count may exceed budget under soft-stop. Confirms the
  // contract change: the cache layer must accept overflow pages.
  {
    std::vector<std::string> longPara;
    for (int i = 1; i <= 60; ++i) longPara.push_back("w" + std::to_string(i));
    std::vector<std::vector<std::string>> blocks = {longPara};

    BlockAwareParser parser(blocks, 4, 3);  // 60/4 = 15 lines / 3 = 5 pages
    WrappedCallback cb(2);
    parser.parsePages([&](std::unique_ptr<Page> p) { return cb(std::move(p)); }, 2);

    // Budget was 2, but the block needed 5 pages — caller got all 5.
    runner.expectTrue(cb.emitted.size() > 2, "overflow_ack_count_exceeds_budget");
    runner.expectEq(static_cast<size_t>(5), cb.emitted.size(), "overflow_full_block_emitted");
  }

  return runner.allPassed() ? 0 : 1;
}
