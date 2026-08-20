#pragma once

#include <Arduino.h>
#include <driver/adc.h>

class InputManager {
 public:
  InputManager();
  void begin();
  uint8_t getState();

  /**
   * Updates the button states. Should be called regularly in the main loop.
   */
  void update();

  /**
   * Returns true if the button was being held at the time of the last #update() call.
   *
   * @param buttonIndex the button indexes
   * @return the button current press state
   */
  bool isPressed(uint8_t buttonIndex) const;

 /**
   * Returns true if the button went from unpressed to pressed between the last two #update() calls.
   *
   * This differs from #isPressed() in that pressing and holding a button will cause this function
   * to return true after the first #update() call, but false on subsequent calls, whereas #isPressed()
   * will continue to return true.
   *
   * @param buttonIndex
   * @return the button pressed state
   */
  bool wasPressed(uint8_t buttonIndex) const;

  /**
   * Returns true if any button started being pressed between the last two #update() calls
   *
   * @return true if any button started being pressed between the last two #update() calls
   */
  bool wasAnyPressed() const;

  /**
   * Returns true if the button went from pressed to unpressed between the last two #update() calls
   *
   * @param buttonIndex the button indexes
   * @return the button release state
   */
  bool wasReleased(uint8_t buttonIndex) const;

  /**
   * Returns true if any button was released between the last two #update() calls
   *
   * @return  true if any button was released between the last two #update() calls
   */
  bool wasAnyReleased() const;

  /**
   * Returns the time between any button starting to be depressed and all buttons between released
   *
   * @return duration in milliseconds
   */
  unsigned long getHeldTime() const;

  // Button indices
  static constexpr uint8_t BTN_BACK = 0;
  static constexpr uint8_t BTN_CONFIRM = 1;
  static constexpr uint8_t BTN_LEFT = 2;
  static constexpr uint8_t BTN_RIGHT = 3;
  static constexpr uint8_t BTN_UP = 4;
  static constexpr uint8_t BTN_DOWN = 5;
  static constexpr uint8_t BTN_POWER = 6;
  // Pins
  static constexpr int BUTTON_ADC_PIN_1 = 1;
  static constexpr int BUTTON_ADC_PIN_2 = 2;
  static constexpr int POWER_BUTTON_PIN = 3;

  // Power button methods
  bool isPowerButtonPressed() const;

  // Button names
  static const char* getButtonName(uint8_t buttonIndex);

 private:
  int getButtonFromADC(int adcValue, const int ranges[], int numButtons);

  // Nothing is reported until the raw reading has been seen at rest once.
  //
  // Measured on hardware: the first two ADC conversions after boot return
  // exactly 0 on channel 1, on both reset paths, with nobody touching the
  // device -- and 0 falls in RIGHT's band, which has no floor. That is the
  // phantom RIGHT: the menu arrives already one entry down.
  //
  // The discriminator has to be *when*, not *what*: a real RIGHT press reads
  // low too, so rejecting the value would reject the button. Gating on the
  // raw state rather than the debounced one matters as well -- an earlier
  // attempt gated on isPressed(), which lags by the debounce interval and so
  // reported "idle" during the very glitch it meant to skip.
  //
  // And one idle reading is not enough. The first version settled on the
  // first sample that said "nothing pressed", which works if the glitch comes
  // first and fails if it comes *after* an idle sample -- observed as the
  // cursor jumping exactly once just after boot and never again. The measured
  // glitches were 130ms apart, so settling requires the raw state to have
  // been idle continuously for rather longer than that.
  bool settled;
  unsigned long idleSince;
  static constexpr unsigned long SETTLE_IDLE_MS = 300;

  uint8_t currentState;
  uint8_t lastState;
  uint8_t pressedEvents;
  uint8_t releasedEvents;
  unsigned long lastDebounceTime;
  unsigned long buttonPressStart;
  unsigned long buttonPressFinish;

  static constexpr int NUM_BUTTONS_1 = 4;
  static const int ADC_RANGES_1[];

  static constexpr int NUM_BUTTONS_2 = 2;
  static const int ADC_RANGES_2[];

  static constexpr int ADC_NO_BUTTON = 3800;
  static constexpr unsigned long DEBOUNCE_DELAY = 5;

  static const char* BUTTON_NAMES[];
};
