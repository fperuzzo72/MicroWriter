"""Patch em crosspoint/src/main.cpp: include + registerOtaAppName("CrossPoint")."""

path = "crosspoint/src/main.cpp"
src = open(path).read()

old1 = '#include "util/ScreenshotUtil.h"'
new1 = '#include "util/ScreenshotUtil.h"\n#include "OtaApps.h"'
assert old1 in src, "util/ScreenshotUtil.h include not found"
src = src.replace(old1, new1)

old2 = "  gpio.begin();\n  powerManager.begin();"
new2 = '  gpio.begin();\n  registerOtaAppName("CrossPoint");\n  powerManager.begin();'
assert old2 in src, "gpio.begin()/powerManager.begin() sequence not found"
src = src.replace(old2, new2)

open(path, "w").write(src)
print("main.cpp: OK")
