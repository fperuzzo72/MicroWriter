#include "HomeState.h"

#include <Arduino.h>
#include <Bitmap.h>
#include <CoverHelpers.h>
#include <GfxRenderer.h>
#include <SDCardManager.h>
#include <Utf8.h>
#include <esp_system.h>

#include <algorithm>
#include <cmath>

#include "../config.h"
#include "../core/BootMode.h"
#include "../core/Core.h"
#include "../core/InputDrainGuard.h"
#include "../content/LibraryIndex.h"
#include "../content/RecentBooks.h"
#include "Battery.h"
#include "FontManager.h"
#include "MappedInputManager.h"
#include "PluginHostState.h"
#include "PluginListState.h"
#include "ThemeManager.h"

namespace sumi {

HomeState::HomeState(GfxRenderer& renderer) : renderer_(renderer) {}

HomeState::~HomeState() = default;

void HomeState::enter(Core& core) {
  // Drain queued button events so a power-button wake doesn't trigger actions
  InputDrainGuard::drain(core);

  Serial.println("[HOME] Entering");
  core_ = &core;  // Store for theme loading

  // Load last book info if content is still open
  loadLastBook(core);

  buildMenu();
  updateBattery();

  view_.needsRender = true;
}

void HomeState::exit(Core& core) {
  Serial.println("[HOME] Exiting");
  view_.clear();
}

void HomeState::buildMenu() {
  view_.clearMenu();
  if (view_.hasBook) {
    view_.addMenuItem("Continue reading", ui::HomeView::MenuTarget::ContinueReading);
  }
  view_.addMenuItem("Files", ui::HomeView::MenuTarget::Files);
  view_.addMenuItem("Write", ui::HomeView::MenuTarget::Write);
  view_.addMenuItem("Dictionary", ui::HomeView::MenuTarget::Dictionary);
  view_.addMenuItem("Games", ui::HomeView::MenuTarget::Games);
  view_.addMenuItem("Settings", ui::HomeView::MenuTarget::Settings);
}

void HomeState::loadLastBook(Core& core) {
  // Reset cover state
  coverBmpPath_.clear();
  hasCoverImage_ = false;
  coverLoadFailed_ = false;
  currentBookHash_ = 0;

  // If content already open, use it
  if (core.content.isOpen()) {
    const auto& meta = core.content.metadata();
    view_.setBook(meta.title, meta.author, core.buf.path);
    currentBookHash_ = LibraryIndex::hashPath(core.buf.path);

    if (core.settings.showImages) {
      coverBmpPath_ = core.content.getThumbnailPath();
      if (!coverBmpPath_.empty() && SdMan.exists(coverBmpPath_.c_str())) {
        hasCoverImage_ = true;
      }
    }
    view_.hasCoverBmp = hasCoverImage_;
    return;
  }

  // Try to get book info from RecentBooks (avoids opening EPUB just for metadata)
  const char* savedPath = core.settings.lastBookPath;
  if (savedPath[0] == '\0' || !core.storage.exists(savedPath)) {
    view_.clearBook();
    return;
  }

  // Try RecentBooks for title/author (much cheaper than opening EPUB)
  RecentBooks::Entry recentEntry;
  if (RecentBooks::getMostRecent(core, recentEntry) && strcmp(recentEntry.path, savedPath) == 0) {
    view_.setBook(recentEntry.title, recentEntry.author, savedPath);
    utf8SafeCopy(core.buf.path, savedPath, sizeof(core.buf.path));
    
    // Use persisted thumbnail path from RecentBooks (no hash re-derivation)
    uint32_t hash = LibraryIndex::hashPath(savedPath);
    currentBookHash_ = hash;
    if (core.settings.showImages && recentEntry.hasThumb() && SdMan.exists(recentEntry.thumbPath)) {
      coverBmpPath_ = recentEntry.thumbPath;
      hasCoverImage_ = true;
    }
    view_.hasCoverBmp = hasCoverImage_;

    // Get progress from LibraryIndex - use minimal stack
    LibraryIndex::Entry libEntry;
    if (LibraryIndex::findByHash(core, hash, libEntry)) {
      view_.bookCurrentPage = libEntry.currentPage;
      view_.bookTotalPages = libEntry.totalPages;
      view_.bookProgress = libEntry.progressPercent();
      const char* dot = strrchr(savedPath, '.');
      view_.isChapterBased = dot && (strcasecmp(dot, ".epub") == 0);
    }
    return;
  }
  
  // Fallback: Open content to get metadata (slower, uses more memory).
  // Skip when heap is fragmented — a large EPUB like Les Misérables can
  // trigger std::bad_alloc inside ContentOpfParser which ESP-IDF can't
  // unwind, crashing the device. Better to show the path without
  // title/cover than to risk a boot-loop. RecentBooks will pick up
  // metadata the first time the user actually opens the book.
  if (ESP.getMaxAllocHeap() < 16384) {
    Serial.printf("[HOME] Heap too tight (largest=%u) for EPUB pre-load, showing path only\n",
                  ESP.getMaxAllocHeap());
    const char* filename = strrchr(savedPath, '/');
    filename = filename ? filename + 1 : savedPath;
    view_.setBook(filename, "", savedPath);
    utf8SafeCopy(core.buf.path, savedPath, sizeof(core.buf.path));
    currentBookHash_ = LibraryIndex::hashPath(savedPath);
    view_.hasCoverBmp = false;
    return;
  }

  // Reader-crash guard: if the previous session crashed while opening a
  // book, main.cpp's boot logic routes us to Home instead of auto-
  // resuming Reader (see "Reader crash guard: going Home"). But if we
  // then turn around and open the SAME EPUB here just to grab metadata
  // for the home card, we re-hit the parser bug and loop. Skip the
  // open and show the filename — exactly what the heap-tight fallback
  // above does. The user can still navigate to a different book; the
  // bad one will only get re-attempted when they explicitly open it
  // from the file browser. Aozora-derived EPUBs (こころ #00773 and
  // friends) reliably trip this — their content.opf takes 67+ seconds
  // and the nav.xhtml TOC parse throws bad_alloc.
  if (core.settings.readerLoadAttempts > 0) {
    Serial.printf("[HOME] Previous session crashed (attempts=%d) — skipping EPUB pre-load for %s\n",
                  core.settings.readerLoadAttempts, savedPath);
    const char* filename = strrchr(savedPath, '/');
    filename = filename ? filename + 1 : savedPath;
    view_.setBook(filename, "", savedPath);
    utf8SafeCopy(core.buf.path, savedPath, sizeof(core.buf.path));
    currentBookHash_ = LibraryIndex::hashPath(savedPath);
    view_.hasCoverBmp = false;
    return;
  }

  auto result = core.content.open(savedPath, SUMI_CACHE_DIR);
  if (result.ok()) {
    const auto& meta = core.content.metadata();
    view_.setBook(meta.title, meta.author, savedPath);
    utf8SafeCopy(core.buf.path, savedPath, sizeof(core.buf.path));
    currentBookHash_ = LibraryIndex::hashPath(savedPath);

    if (core.settings.showImages) {
      coverBmpPath_ = core.content.getThumbnailPath();
      if (!coverBmpPath_.empty() && SdMan.exists(coverBmpPath_.c_str())) {
        hasCoverImage_ = true;
      }
    }
    view_.hasCoverBmp = hasCoverImage_;
    core.content.close();
  } else {
    view_.clearBook();
  }
}

void HomeState::updateBattery() {
  int percent = batteryMonitor.readPercentage();
  view_.setBattery(percent);
}

void HomeState::openSelectedBook(Core& core) {
  if (!view_.hasBook || view_.bookPath[0] == '\0') return;
  utf8SafeCopy(core.buf.path, view_.bookPath, sizeof(core.buf.path));
  // Save lastBookPath for "continue reading" on next cold boot
  utf8SafeCopy(core.settings.lastBookPath, core.buf.path, sizeof(core.settings.lastBookPath));
  core.settings.transitionReturnTo = 0;  // ReturnTo::HOME
  core.settings.saveToFile();
  pendingOpen_ = true;
}

StateTransition HomeState::launchMenuTarget(Core& core, ui::HomeView::MenuTarget target) {
  using Target = ui::HomeView::MenuTarget;

  // Look up a registered plugin by name and hand it to PluginHostState —
  // same mechanism PluginListState uses when the user picks an entry from
  // its own list, just skipping that intermediate screen for the handful
  // of plugins that get a direct row on Home. Falls back to PluginList
  // (the full picker) if the name isn't registered, so this never dead-ends.
#if FEATURE_PLUGINS
  auto launchPluginNamed = [this](const char* name) -> StateTransition {
    if (hostState_) {
      for (int i = 0; i < PluginListState::pluginCount; i++) {
        if (strcmp(PluginListState::plugins[i].name, name) == 0) {
          hostState_->setPluginFactory(PluginListState::plugins[i].factory);
          return StateTransition::to(StateId::PluginHost);
        }
      }
    }
    return StateTransition::to(StateId::PluginList);
  };
#else
  auto launchPluginNamed = [](const char*) -> StateTransition { return StateTransition::stay(StateId::Home); };
#endif

  switch (target) {
    case Target::ContinueReading:
      openSelectedBook(core);
      break;
    case Target::Files:
      return StateTransition::to(StateId::FileList);
    case Target::Settings:
      return StateTransition::to(StateId::Settings);
    case Target::Games:
      // Several games are registered (Chess, Sudoku, SumiBoy, ...) — send
      // the user to the picker rather than guessing which one they want.
      return StateTransition::to(StateId::PluginList);
    case Target::Write:
      // TODO(MicroWriter): swap "Notes" for the ported MicroSlate plugin
      // once it exists. Notes stays reachable from the Games/Apps picker
      // either way — this only changes what "Write" on Home launches.
      return launchPluginNamed("Notes");
    case Target::Dictionary:
      return launchPluginNamed("Dictionary");
  }
  return StateTransition::stay(StateId::Home);
}

StateTransition HomeState::update(Core& core) {
  Event e;
  while (core.events.pop(e)) {
    switch (e.type) {
      case EventType::ButtonPress:
        switch (e.button) {
          case Button::Back:
            break;

          case Button::Center:
            if (const auto* entry = view_.selectedEntry()) {
              StateTransition t = launchMenuTarget(core, entry->target);
              if (t.next != StateId::Home) return t;
            }
            break;

          case Button::Left:
            // Open the file browser.
            return StateTransition::to(StateId::FileList);

          case Button::Right:
            // Open settings.
            return StateTransition::to(StateId::Settings);

          case Button::Up:
            view_.moveSelectionUp();
            break;

          case Button::Down:
            view_.moveSelectionDown();
            break;

          case Button::Power:
            break;
        }
        break;

      case EventType::ButtonLongPress:
        if (e.button == Button::Power) {
          return StateTransition::to(StateId::Sleep);
        }
        break;

      default:
        break;
    }

    // Check if a book open was requested (sets path on core.buf.path)
    if (pendingOpen_) {
      pendingOpen_ = false;
      return StateTransition::to(StateId::Reader);
    }
  }

  return StateTransition::stay(StateId::Home);
}

void HomeState::render(Core& core) {
  if (!view_.needsRender) {
    return;
  }

  const Theme& theme = THEME;

  // Load cover from SD card every time (simple, always correct)
  if (hasCoverImage_ && !coverLoadFailed_) {
    renderCoverToCard();
  }

  // Resolve external font for title/author (may trigger SD load on first call)
  view_.titleFontId = (theme.readerFontFamilySmall[0] != '\0')
                          ? FONT_MANAGER.getFontId(theme.readerFontFamilySmall, theme.uiFontId)
                          : theme.uiFontId;

  // Render rest of UI (text boxes will draw on top of cover)
  ui::render(renderer_, theme, view_);

  renderer_.displayBuffer(EInkDisplay::HALF_REFRESH);
  view_.needsRender = false;
  core.display.markDirty();

  // First-boot welcome overlay — show once when no .sumi folder exists yet
  if (core.settings.isFirstBoot) {
    core.settings.isFirstBoot = false;
    delay(2000);  // Let home screen settle before overlaying

    const int W = renderer_.getScreenWidth();   // 480
    const int pad = 24;
    const int boxX = pad;
    const int boxW = W - 2 * pad;
    const int boxY = 480;
    const int boxH = 280;

    // White box with double border
    renderer_.fillRect(boxX, boxY, boxW, boxH, false);
    renderer_.drawRect(boxX, boxY, boxW, boxH, true);
    renderer_.drawRect(boxX + 1, boxY + 1, boxW - 2, boxH - 2, true);

    const int textX = boxX + 20;
    const int textW = boxW - 40;
    const int lineH = renderer_.getLineHeight(theme.menuFontId) + 4;
    int y = boxY + 24;

    // Title
    renderer_.drawCenteredText(theme.menuFontId, y, "Welcome to SUMI", theme.primaryTextBlack, EpdFontFamily::BOLD);
    y += lineH + 12;

    // Body text
    const char* lines[] = {
      "SUMI reads any EPUB, but files",
      "optimized on sumi.page load faster,",
      "look sharper, and use less memory.",
      "",
      "Open sumi.page in Chrome or Edge",
      "with Bluetooth enabled to convert",
      "and send files wirelessly.",
    };
    for (const char* line : lines) {
      if (line[0] == '\0') {
        y += lineH / 2;
      } else {
        renderer_.drawCenteredText(theme.smallFontId, y, line, theme.primaryTextBlack);
        y += renderer_.getLineHeight(theme.smallFontId) + 3;
      }
    }

    renderer_.displayBuffer(EInkDisplay::FAST_REFRESH);
  }
}

void HomeState::renderCoverToCard() {
  FsFile file;
  if (!SdMan.openFileForRead("HOME", coverBmpPath_, file)) {
    coverLoadFailed_ = true;
    Serial.printf("[%lu] [HOME] Failed to open cover BMP: %s\n", millis(), coverBmpPath_.c_str());
    return;
  }

  Bitmap bitmap(file);
  if (bitmap.parseHeaders() != BmpReaderError::Ok) {
    file.close();
    coverLoadFailed_ = true;
    Serial.printf("[%lu] [HOME] Failed to parse cover BMP: %s\n", millis(), coverBmpPath_.c_str());
    return;
  }

  const auto layout = ui::calculateBookBlockLayout(renderer_, THEME);

  // Compute scale (same logic as drawBitmap) to find actual drawn size
  float scale = 1.0f;
  if (bitmap.getWidth() > layout.coverMaxW)
    scale = (float)layout.coverMaxW / (float)bitmap.getWidth();
  if (bitmap.getHeight() > layout.coverMaxH)
    scale = std::min(scale, (float)layout.coverMaxH / (float)bitmap.getHeight());

  int drawnW = (int)(bitmap.getWidth() * scale);
  int drawnH = (int)(bitmap.getHeight() * scale);

  // Center within the cover area
  int drawX = layout.coverX + (layout.coverMaxW - drawnW) / 2;
  int drawY = layout.coverY + (layout.coverMaxH - drawnH) / 2;

  renderer_.drawBitmap(bitmap, drawX, drawY, layout.coverMaxW, layout.coverMaxH);
  file.close();
}


}  // namespace sumi
