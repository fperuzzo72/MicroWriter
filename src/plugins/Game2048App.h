#pragma once

/**
 * @file Game2048App.h
 * @brief 2048 number-merging puzzle game for SUMI
 *
 * Slide tiles on a 4x4 grid. Equal tiles merge and double.
 * Reach 2048 to win (continue playing allowed).
 * Game over when no valid moves remain.
 */

#include "../config.h"

#if FEATURE_PLUGINS && FEATURE_GAMES

#include <Arduino.h>
#include <SDCardManager.h>
#include <cstring>

#include "PluginHelpers.h"
#include "PluginInterface.h"
#include "PluginRenderer.h"

namespace sumi {

#define GAME2048_SAVE_PATH "/.sumi/game_2048.bin"

class Game2048App : public PluginInterface {
 public:
  explicit Game2048App(PluginRenderer& renderer) : d_(renderer) {}

  const char* name() const override { return "2048"; }
  PluginRunMode runMode() const override { return PluginRunMode::Simple; }

  void init(int screenW, int screenH) override {
    screenW_ = screenW;
    screenH_ = screenH;
    landscape_ = isLandscapeMode(screenW, screenH);
    computeLayout();
    loadHighScore();
    newGame();
  }

  // =========================================================================
  // Layout
  // =========================================================================
  void computeLayout() {
    int headerH = PLUGIN_HEADER_H + 4;  // title row
    int scoreH = 22;                     // score row
    int footerH = PLUGIN_FOOTER_H;

    int availH = screenH_ - headerH - scoreH - footerH - 2 * PLUGIN_MARGIN;
    int availW = screenW_ - 2 * PLUGIN_MARGIN;

    cellSize_ = min(availW, availH) / 4;
    int gridSide = cellSize_ * 4;

    gridX_ = (screenW_ - gridSide) / 2;
    gridY_ = headerH + scoreH + (availH - gridSide) / 2 + PLUGIN_MARGIN;
  }

  // =========================================================================
  // New game / reset
  // =========================================================================
  void newGame() {
    memset(grid_, 0, sizeof(grid_));
    score_ = 0;
    gameOver_ = false;
    won_ = false;
    spawnTile();
    spawnTile();
    needsFullRedraw = true;
  }

  // =========================================================================
  // Tile spawning
  // =========================================================================
  void spawnTile() {
    // Collect empty cells
    uint8_t emptyR[16], emptyC[16];
    int emptyCount = 0;
    for (int r = 0; r < 4; r++) {
      for (int c = 0; c < 4; c++) {
        if (grid_[r][c] == 0 && emptyCount < 16) {
          emptyR[emptyCount] = r;
          emptyC[emptyCount] = c;
          emptyCount++;
        }
      }
    }
    if (emptyCount == 0) return;

    int idx = random(0, emptyCount);
    // 90% chance of 2, 10% chance of 4
    grid_[emptyR[idx]][emptyC[idx]] = (random(0, 10) < 9) ? 2 : 4;
  }

  // =========================================================================
  // Core merge logic
  // =========================================================================

  // Slide a single row left. Returns true if anything moved.
  bool slideRowLeft(uint16_t row[4]) {
    bool moved = false;

    // Compact: remove zeros
    uint16_t temp[4] = {};
    int pos = 0;
    for (int i = 0; i < 4; i++) {
      if (row[i] != 0) temp[pos++] = row[i];
    }

    // Merge adjacent equal tiles
    for (int i = 0; i < 3; i++) {
      if (temp[i] != 0 && temp[i] == temp[i + 1]) {
        temp[i] *= 2;
        score_ += temp[i];
        temp[i + 1] = 0;
        moved = true;
      }
    }

    // Compact again
    uint16_t result[4] = {};
    pos = 0;
    for (int i = 0; i < 4; i++) {
      if (temp[i] != 0) result[pos++] = temp[i];
    }

    // Check if moved
    for (int i = 0; i < 4; i++) {
      if (row[i] != result[i]) moved = true;
      row[i] = result[i];
    }
    return moved;
  }

  // Reverse a 4-element row in place.
  void reverseRow(uint16_t row[4]) {
    uint16_t t = row[0]; row[0] = row[3]; row[3] = t;
    t = row[1]; row[1] = row[2]; row[2] = t;
  }

  // Transpose the 4x4 grid.
  void transposeGrid() {
    for (int r = 0; r < 4; r++) {
      for (int c = r + 1; c < 4; c++) {
        uint16_t t = grid_[r][c];
        grid_[r][c] = grid_[c][r];
        grid_[c][r] = t;
      }
    }
  }

  bool moveLeft() {
    bool moved = false;
    for (int r = 0; r < 4; r++) {
      if (slideRowLeft(grid_[r])) moved = true;
    }
    return moved;
  }

  bool moveRight() {
    bool moved = false;
    for (int r = 0; r < 4; r++) {
      reverseRow(grid_[r]);
      if (slideRowLeft(grid_[r])) moved = true;
      reverseRow(grid_[r]);
    }
    return moved;
  }

  bool moveUp() {
    transposeGrid();
    bool moved = moveLeft();
    transposeGrid();
    return moved;
  }

  bool moveDown() {
    transposeGrid();
    bool moved = moveRight();
    transposeGrid();
    return moved;
  }

  // =========================================================================
  // Game state checks
  // =========================================================================
  bool hasMovesLeft() {
    for (int r = 0; r < 4; r++) {
      for (int c = 0; c < 4; c++) {
        if (grid_[r][c] == 0) return true;
        if (c < 3 && grid_[r][c] == grid_[r][c + 1]) return true;
        if (r < 3 && grid_[r][c] == grid_[r + 1][c]) return true;
      }
    }
    return false;
  }

  bool hasWon() {
    for (int r = 0; r < 4; r++) {
      for (int c = 0; c < 4; c++) {
        if (grid_[r][c] >= 2048) return true;
      }
    }
    return false;
  }

  void afterMove(bool moved) {
    if (!moved) return;
    spawnTile();

    if (!won_ && hasWon()) {
      won_ = true;
    }

    if (!hasMovesLeft()) {
      gameOver_ = true;
    }

    // Update high score
    if (score_ > highScore_) {
      highScore_ = score_;
      saveHighScore();
    }
  }

  // =========================================================================
  // High score persistence
  // =========================================================================
  void loadHighScore() {
    highScore_ = 0;
    FsFile f = SdMan.open(GAME2048_SAVE_PATH, O_RDONLY);
    if (f) {
      uint32_t val = 0;
      const int bytesRead = f.read((uint8_t*)&val, sizeof(uint32_t));
      f.close();
      if (bytesRead == sizeof(uint32_t)) {
        highScore_ = val;
      }
    }
  }

  void saveHighScore() {
    SdMan.mkdir("/.sumi");
    // Atomic — see docs/ATOMIC_WRITE_DESIGN.md. The high-score file is
    // 4 bytes; a power loss between O_TRUNC and write would lose the
    // user's hard-earned 2048 record.
    FsFile f;
    if (!SdMan.atomicOpenWrite("2048", GAME2048_SAVE_PATH, f)) return;
    f.write((uint8_t*)&highScore_, sizeof(uint32_t));
    if (!SdMan.atomicCommit(f, GAME2048_SAVE_PATH)) {
      SdMan.atomicAbort(f, GAME2048_SAVE_PATH);
    }
  }

  // =========================================================================
  // Input
  // =========================================================================
  bool handleInput(PluginButton btn) override {
    if (gameOver_) {
      if (btn == PluginButton::Center) {
        newGame();
        return true;
      }
      if (btn == PluginButton::Back) return false;
      return true;
    }

    bool moved = false;
    switch (btn) {
      case PluginButton::Up:    moved = moveUp(); break;
      case PluginButton::Down:  moved = moveDown(); break;
      case PluginButton::Left:  moved = moveLeft(); break;
      case PluginButton::Right: moved = moveRight(); break;
      case PluginButton::Center:
        // Center does nothing during play
        return true;
      case PluginButton::Back:
        return false;  // exit plugin
      default:
        return true;
    }

    afterMove(moved);
    return true;
  }

  // =========================================================================
  // Drawing
  // =========================================================================
  void draw() override {
    // Header
    PluginUI::drawHeader(d_, "2048", screenW_);

    // Score line below header
    int scoreY = PLUGIN_HEADER_H + 2;
    d_.setTextColor(GxEPD_BLACK);

    // Score left
    char scoreBuf[32];
    snprintf(scoreBuf, sizeof(scoreBuf), "Score: %lu", (unsigned long)score_);
    d_.setCursor(PLUGIN_MARGIN, scoreY + 14);
    d_.print(scoreBuf);

    // High score right
    char hiBuf[32];
    snprintf(hiBuf, sizeof(hiBuf), "Best: %lu", (unsigned long)highScore_);
    int16_t tx, ty;
    uint16_t tw, th;
    d_.getTextBounds(hiBuf, 0, 0, &tx, &ty, &tw, &th);
    d_.setCursor(screenW_ - tw - PLUGIN_MARGIN, scoreY + 14);
    d_.print(hiBuf);

    // Draw grid
    drawGrid();

    // Footer
    if (gameOver_) {
      PluginUI::drawFooter(d_, "GAME OVER", "OK:New  BACK:Exit", screenW_, screenH_);
    } else if (won_) {
      PluginUI::drawFooter(d_, "You reached 2048!", "BACK:Exit", screenW_, screenH_);
    } else {
      PluginUI::drawFooter(d_, "D-pad: Slide", "BACK: Exit", screenW_, screenH_);
    }

    // Game over overlay
    if (gameOver_) {
      char statsBuf[32];
      snprintf(statsBuf, sizeof(statsBuf), "Score: %lu", (unsigned long)score_);
      PluginUI::drawGameOver(d_, "No moves left!", statsBuf, screenW_, screenH_);
    }
  }

  void drawGrid() {
    int cs = cellSize_;
    int ox = gridX_;
    int oy = gridY_;

    // Outer border
    d_.drawRect(ox - 1, oy - 1, cs * 4 + 2, cs * 4 + 2, GxEPD_BLACK);

    for (int r = 0; r < 4; r++) {
      for (int c = 0; c < 4; c++) {
        int x = ox + c * cs;
        int y = oy + r * cs;
        uint16_t val = grid_[r][c];

        if (val == 0) {
          // Empty cell: white with border
          d_.fillRect(x, y, cs, cs, GxEPD_WHITE);
          d_.drawRect(x, y, cs, cs, GxEPD_BLACK);
        } else {
          // Determine fill style based on value tier
          bool filled = shouldFillCell(val);

          if (filled) {
            d_.fillRect(x, y, cs, cs, GxEPD_BLACK);
            d_.setTextColor(GxEPD_WHITE);
          } else {
            d_.fillRect(x, y, cs, cs, GxEPD_WHITE);
            d_.drawRect(x, y, cs, cs, GxEPD_BLACK);
            // Draw inner border for medium tiles
            if (val >= 8) {
              d_.drawRect(x + 1, y + 1, cs - 2, cs - 2, GxEPD_BLACK);
            }
            d_.setTextColor(GxEPD_BLACK);
          }

          // Draw number centered
          char numBuf[8];
          snprintf(numBuf, sizeof(numBuf), "%u", val);
          PluginUI::drawTextCentered(d_, numBuf, x, y, cs, cs);
        }
      }
    }
  }

  // Higher-value tiles get filled (inverted) for visual distinction
  // on a 1-bit e-ink display.
  bool shouldFillCell(uint16_t val) {
    // 2,4 = white bg; 8,16,32 = white bg thick border;
    // 64+ = black bg (inverted)
    return val >= 64;
  }

 private:
  PluginRenderer& d_;

  // Grid state
  uint16_t grid_[4][4] = {};
  uint32_t score_ = 0;
  uint32_t highScore_ = 0;
  bool gameOver_ = false;
  bool won_ = false;

  // Layout
  int screenW_ = 0, screenH_ = 0;
  bool landscape_ = false;
  int cellSize_ = 0;
  int gridX_ = 0, gridY_ = 0;
};

}  // namespace sumi

#endif  // FEATURE_PLUGINS && FEATURE_GAMES
