"""Patch em crossink/src/main.cpp: include + registerOtaAppName("CrossInk")."""

path = "crossink/src/main.cpp"
src = open(path).read()

old1 = "#include <HalGPIO.h>"
new1 = '#include <HalGPIO.h>\n#include "OtaApps.h"'
assert old1 in src, "HalGPIO include not found"
src = src.replace(old1, new1, 1)

old2 = "  gpio.begin();\n  powerManager.begin();"
new2 = '  gpio.begin();\n  registerOtaAppName("CrossInk");\n  powerManager.begin();'
assert old2 in src, "gpio.begin()/powerManager.begin() sequence not found"
src = src.replace(old2, new2)

open(path, "w").write(src)
print("main.cpp: OK")
