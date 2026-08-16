#pragma once

#include "../config.h"

#if FEATURE_PLUGINS

#include <Arduino.h>

#include "PluginHelpers.h"
#include "PluginInterface.h"
#include "PluginRenderer.h"
#include "../ThemeManager.h"
#include "../core/Core.h"

namespace sumi {

/**
 * @file PomodoroApp.h
 * @brief Pomodoro technique timer for SUMI e-reader
 *
 * Cycle: Focus -> Short Break -> Focus -> Short Break ->
 *        Focus -> Short Break -> Focus -> Long Break -> repeat
 *
 * Default durations: 25 min focus, 5 min short break, 15 min long break.
 * Prevents auto-sleep while timer is running via core.input.markActivity().
 */
class PomodoroApp : public PluginInterface {
 public:
  explicit PomodoroApp(PluginRenderer& renderer) : renderer_(renderer) {}

  const char* name() const override { return "Pomodoro"; }
  PluginRunMode runMode() const override { return PluginRunMode::WithUpdate; }

  void init(int screenW, int screenH) override {
    screenW_ = screenW;
    screenH_ = screenH;
    resetAll();
    needsFullRedraw = true;
  }

  void cleanup() override {}

  // ------------------------------------------------------------------
  // Input
  // ------------------------------------------------------------------
  bool handleInput(PluginButton btn) override {
    switch (btn) {
      case PluginButton::Center:
        onStartPause();
        return true;

      case PluginButton::Left:
        onReset();
        return true;

      case PluginButton::Back:
        // Pause if running, then let host exit
        if (running_ && !paused_) {
          pause();
          needsFullRedraw = true;
        }
        return false;  // host will exit plugin

      case PluginButton::Up:
      case PluginButton::Down:
      case PluginButton::Right:
        return true;  // no-op, consume

      default:
        return false;
    }
  }

  // ------------------------------------------------------------------
  // Periodic update (called at ~10 Hz by PluginHostState)
  // ------------------------------------------------------------------
  bool update() override {
    if (!running_ || paused_) return false;

    // Keep device awake while timer is active
    core.input.markActivity();

    unsigned long remaining = getRemainingMs();

    // Phase complete
    if (remaining == 0) {
      onPhaseComplete();
      return true;  // request redraw
    }

    // Throttle display updates: redraw every 10 seconds
    int displaySecs = static_cast<int>(remaining / 1000);
    int bucket = displaySecs / 10;
    if (bucket != lastDisplayBucket_) {
      lastDisplayBucket_ = bucket;
      return true;  // request redraw
    }

    return false;
  }

  // ------------------------------------------------------------------
  // Drawing
  // ------------------------------------------------------------------
  void draw() override {
    const Theme& t = THEME_MANAGER.current();
    GfxRenderer& gfx = renderer_.gfx();

    // Host clears to white; re-clear to theme background for dark-mode support
    gfx.clearScreen(t.backgroundColor);

    // --- Phase label at top ---
    const char* phaseLabel = phaseName();
    gfx.drawCenteredText(t.uiFontId, 55, phaseLabel, t.primaryTextBlack);

    // Separator line
    gfx.drawLine(20, 70, screenW_ - 20, 70, t.primaryTextBlack);

    // --- Large MM:SS timer in center ---
    unsigned long remaining = getRemainingMs();
    int mins = static_cast<int>(remaining / 60000);
    int secs = static_cast<int>((remaining / 1000) % 60);
    char timeBuf[8];
    snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", mins, secs);

    // Use the reader font for a large, readable timer display
    int timerY = screenH_ / 2 - 10;
    gfx.drawCenteredText(t.readerFontIdLarge, timerY, timeBuf, t.primaryTextBlack);

    // --- Pomodoro count below timer ---
    char countBuf[16];
    snprintf(countBuf, sizeof(countBuf), "%d of %d", pomodoroCount_, POMODOROS_PER_CYCLE);
    int countY = timerY + 60;
    gfx.drawCenteredText(t.smallFontId, countY, countBuf, t.primaryTextBlack);

    // --- Status line ---
    const char* status = "";
    if (!running_) {
      status = "Ready";
    } else if (paused_) {
      status = "Paused";
    } else {
      status = (phase_ == Focus) ? "Stay focused!" : "Take a break";
    }
    gfx.drawCenteredText(t.smallFontId, countY + 35, status, t.primaryTextBlack);

    // --- Button bar at bottom ---
    drawButtonBar(gfx, t);

    // PluginHostState handles displayBuffer() after draw() returns
    needsFullRedraw = false;
  }

  void drawPartial() override {
    // For e-ink, partial updates still do a full draw but with fast refresh
    // (PluginHostState handles the refresh mode based on partialCount_)
    draw();
  }

 private:
  // ------------------------------------------------------------------
  // Constants
  // ------------------------------------------------------------------
  static constexpr unsigned long FOCUS_MS       = 25UL * 60 * 1000;
  static constexpr unsigned long SHORT_BREAK_MS =  5UL * 60 * 1000;
  static constexpr unsigned long LONG_BREAK_MS  = 15UL * 60 * 1000;
  static constexpr int POMODOROS_PER_CYCLE = 4;

  // ------------------------------------------------------------------
  // State
  // ------------------------------------------------------------------
  enum Phase : uint8_t { Focus, ShortBreak, LongBreak };

  PluginRenderer& renderer_;
  int screenW_ = 0;
  int screenH_ = 0;

  Phase phase_ = Focus;
  int pomodoroCount_ = 0;     // completed focus sessions in current cycle

  unsigned long timerStartMs_ = 0;
  unsigned long pausedMs_ = 0;       // total accumulated paused time
  unsigned long lastPauseMs_ = 0;    // millis() when last paused
  bool running_ = false;
  bool paused_ = false;

  int lastDisplayBucket_ = -1;  // for throttling redraws

  // ------------------------------------------------------------------
  // Timer helpers
  // ------------------------------------------------------------------
  unsigned long getDurationMs() const {
    switch (phase_) {
      case Focus:      return FOCUS_MS;
      case ShortBreak: return SHORT_BREAK_MS;
      case LongBreak:  return LONG_BREAK_MS;
    }
    return FOCUS_MS;
  }

  unsigned long getElapsedMs() const {
    if (!running_) return 0;
    unsigned long now = millis();
    unsigned long totalPaused = pausedMs_;
    if (paused_) totalPaused += (now - lastPauseMs_);
    unsigned long elapsed = now - timerStartMs_ - totalPaused;
    return elapsed;
  }

  unsigned long getRemainingMs() const {
    unsigned long elapsed = getElapsedMs();
    unsigned long duration = getDurationMs();
    return elapsed >= duration ? 0 : duration - elapsed;
  }

  const char* phaseName() const {
    switch (phase_) {
      case Focus:      return "FOCUS";
      case ShortBreak: return "SHORT BREAK";
      case LongBreak:  return "LONG BREAK";
    }
    return "FOCUS";
  }

  // ------------------------------------------------------------------
  // Actions
  // ------------------------------------------------------------------
  void resetAll() {
    phase_ = Focus;
    pomodoroCount_ = 0;
    running_ = false;
    paused_ = false;
    timerStartMs_ = 0;
    pausedMs_ = 0;
    lastPauseMs_ = 0;
    lastDisplayBucket_ = -1;
  }

  void startTimer() {
    timerStartMs_ = millis();
    pausedMs_ = 0;
    lastPauseMs_ = 0;
    running_ = true;
    paused_ = false;
    lastDisplayBucket_ = -1;
    Serial.printf("[POMODORO] Started: %s\n", phaseName());
  }

  void pause() {
    if (running_ && !paused_) {
      paused_ = true;
      lastPauseMs_ = millis();
      Serial.println("[POMODORO] Paused");
    }
  }

  void resume() {
    if (running_ && paused_) {
      pausedMs_ += (millis() - lastPauseMs_);
      paused_ = false;
      Serial.println("[POMODORO] Resumed");
    }
  }

  void onStartPause() {
    if (!running_) {
      // First start or after reset
      startTimer();
    } else if (paused_) {
      resume();
    } else {
      pause();
    }
    needsFullRedraw = true;
  }

  void onReset() {
    // Reset the current phase timer
    if (running_) {
      startTimer();  // restart current phase from zero
    }
    needsFullRedraw = true;
    Serial.println("[POMODORO] Reset current phase");
  }

  void onPhaseComplete() {
    Serial.printf("[POMODORO] Phase complete: %s (%d of %d)\n",
                  phaseName(), pomodoroCount_, POMODOROS_PER_CYCLE);

    if (phase_ == Focus) {
      pomodoroCount_++;
      if (pomodoroCount_ >= POMODOROS_PER_CYCLE) {
        // Completed full cycle, take long break
        phase_ = LongBreak;
        pomodoroCount_ = 0;
      } else {
        phase_ = ShortBreak;
      }
    } else {
      // Break ended, back to focus
      phase_ = Focus;
    }

    // Auto-start the next phase
    startTimer();
    needsFullRedraw = true;
  }

  // ------------------------------------------------------------------
  // UI helpers
  // ------------------------------------------------------------------
  void drawButtonBar(GfxRenderer& gfx, const Theme& t) {
    int y = screenH_ - PLUGIN_FOOTER_H;
    gfx.drawLine(0, y, screenW_, y, t.primaryTextBlack);

    // Left: "Back"
    int leftX = PLUGIN_MARGIN;
    int textY = screenH_ - 5;
    renderer_.setTextColor(t.primaryTextBlack ? GxEPD_BLACK : GxEPD_WHITE);
    renderer_.setCursor(leftX, textY);
    renderer_.print("Back");

    // Center: "Start" / "Pause" / "Resume"
    const char* centerLabel = "Start";
    if (running_ && !paused_) centerLabel = "Pause";
    else if (running_ && paused_) centerLabel = "Resume";
    int16_t tx, ty;
    uint16_t tw, th;
    renderer_.getTextBounds(centerLabel, 0, 0, &tx, &ty, &tw, &th);
    renderer_.setCursor((screenW_ - tw) / 2, textY);
    renderer_.print(centerLabel);

    // Right: "Reset"
    const char* rightLabel = "Reset";
    renderer_.getTextBounds(rightLabel, 0, 0, &tx, &ty, &tw, &th);
    renderer_.setCursor(screenW_ - tw - PLUGIN_MARGIN, textY);
    renderer_.print(rightLabel);
  }
};

}  // namespace sumi

#endif  // FEATURE_PLUGINS
