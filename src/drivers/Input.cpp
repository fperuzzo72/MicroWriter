#include "Input.h"

#include <Arduino.h>
#include <InputManager.h>
#include <MappedInputManager.h>

// Global input managers (defined in main.cpp)
extern InputManager inputManager;
extern MappedInputManager& mappedInput;

namespace sumi {
namespace drivers {

Result<void> Input::init(EventQueue& eventQueue) {
  if (initialized_) {
    return Ok();
  }

  queue_ = &eventQueue;
  lastActivityMs_ = millis();
  prevButtonState_ = 0;
  currButtonState_ = 0;
  initialized_ = true;

  return Ok();
}

void Input::shutdown() {
  queue_ = nullptr;
  initialized_ = false;
}

void Input::poll() {
  if (!initialized_ || !queue_) {
    return;
  }

  // Save previous state
  prevButtonState_ = currButtonState_;
  currButtonState_ = 0;

  // Check each button
  checkButton(Button::Up, 1 << 0);
  checkButton(Button::Down, 1 << 1);
  checkButton(Button::Left, 1 << 2);
  checkButton(Button::Right, 1 << 3);
  checkButton(Button::Center, 1 << 4);
  checkButton(Button::Back, 1 << 5);
  checkButton(Button::Power, 1 << 6);

  // Diagnostic: log state changes so we can see what the ADC ladder is
  // reading on real hardware (where [BTN] logs don't fire). Only
  // logs on transitions so a held button doesn't spam the serial.
  if (currButtonState_ != prevButtonState_ && Serial) {
    Serial.printf("[INPUT] state: 0x%02x -> 0x%02x\n",
                  prevButtonState_, currButtonState_);
  }
}

void Input::checkButton(Button btn, uint8_t mask) {
  bool wasDown = (prevButtonState_ & mask) != 0;
  bool isDown = false;

  // Map our Button to MappedInputManager::Button
  MappedInputManager::Button mappedBtn;
  switch (btn) {
    case Button::Up:
      mappedBtn = MappedInputManager::Button::Up;
      break;
    case Button::Down:
      mappedBtn = MappedInputManager::Button::Down;
      break;
    case Button::Left:
      mappedBtn = MappedInputManager::Button::Left;
      break;
    case Button::Right:
      mappedBtn = MappedInputManager::Button::Right;
      break;
    case Button::Center:
      mappedBtn = MappedInputManager::Button::Confirm;
      break;
    case Button::Back:
      mappedBtn = MappedInputManager::Button::Back;
      break;
    case Button::Power:
      mappedBtn = MappedInputManager::Button::Power;
      break;
  }

  isDown = mappedInput.isPressed(mappedBtn);

  if (isDown) {
    currButtonState_ |= mask;
  }

  int idx = static_cast<int>(btn);

  // Button just pressed
  if (isDown && !wasDown) {
    pressStartMs_[idx] = millis();
    queue_->push(Event::buttonPress(btn));
    lastActivityMs_ = millis();
  }

  // Button held - check for long press
  if (isDown && wasDown) {
    uint32_t heldMs = millis() - pressStartMs_[idx];
    if (heldMs >= LONG_PRESS_MS) {
      // Only fire once per press
      if (pressStartMs_[idx] != 0) {
        queue_->push(Event::buttonLongPress(btn));
        pressStartMs_[idx] = 0;  // Mark as fired
      }
    }
  }

  // Button released
  if (!isDown && wasDown) {
    queue_->push(Event::buttonRelease(btn));
    lastActivityMs_ = millis();
  }
}

uint32_t Input::idleTimeMs() const { return millis() - lastActivityMs_; }

void Input::markActivity() { lastActivityMs_ = millis(); }

bool Input::isPressed(Button btn) const {
  MappedInputManager::Button mappedBtn;
  switch (btn) {
    case Button::Up:
      mappedBtn = MappedInputManager::Button::Up;
      break;
    case Button::Down:
      mappedBtn = MappedInputManager::Button::Down;
      break;
    case Button::Left:
      mappedBtn = MappedInputManager::Button::Left;
      break;
    case Button::Right:
      mappedBtn = MappedInputManager::Button::Right;
      break;
    case Button::Center:
      mappedBtn = MappedInputManager::Button::Confirm;
      break;
    case Button::Back:
      mappedBtn = MappedInputManager::Button::Back;
      break;
    case Button::Power:
      mappedBtn = MappedInputManager::Button::Power;
      break;
  }
  return mappedInput.isPressed(mappedBtn);
}

void Input::resyncState() {
  currButtonState_ = 0;
  if (isPressed(Button::Up)) currButtonState_ |= (1 << 0);
  if (isPressed(Button::Down)) currButtonState_ |= (1 << 1);
  if (isPressed(Button::Left)) currButtonState_ |= (1 << 2);
  if (isPressed(Button::Right)) currButtonState_ |= (1 << 3);
  if (isPressed(Button::Center)) currButtonState_ |= (1 << 4);
  if (isPressed(Button::Back)) currButtonState_ |= (1 << 5);
  if (isPressed(Button::Power)) currButtonState_ |= (1 << 6);
  prevButtonState_ = currButtonState_;
}

MappedInputManager& Input::raw() { return mappedInput; }

}  // namespace drivers
}  // namespace sumi
