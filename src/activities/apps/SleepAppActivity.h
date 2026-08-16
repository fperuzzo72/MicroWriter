#pragma once

#include <string>
#include <vector>

#include "../Activity.h"
#include "util/ButtonNavigator.h"

class SleepAppActivity final : public Activity {
  ButtonNavigator buttonNavigator;
  std::vector<std::string> directories;
  int selectedIndex = 0;

  void loadDirectories();
  void openSelectedDirectory();

 public:
  explicit SleepAppActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("SleepApp", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
};
