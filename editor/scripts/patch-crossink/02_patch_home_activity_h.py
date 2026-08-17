"""Patch em crossink/src/activities/home/HomeActivity.h: include + dois campos novos."""

path = "crossink/src/activities/home/HomeActivity.h"
src = open(path).read()

old1 = '#include "./FileBrowserActivity.h"'
new1 = '#include "./FileBrowserActivity.h"\n#include "OtaApps.h"'
assert old1 in src, "include FileBrowserActivity.h not found"
src = src.replace(old1, new1)

old2 = "  ButtonNavigator buttonNavigator;"
new2 = ("  ButtonNavigator buttonNavigator;\n"
        "  OtaAppEntry otaApps[MAX_OTA_APPS] = {};\n"
        "  int otaAppCount = 0;")
assert old2 in src, "buttonNavigator field not found"
src = src.replace(old2, new2)

open(path, "w").write(src)
print("HomeActivity.h: OK")
