"""Patch em crosspoint/src/activities/home/HomeActivity.cpp — quatro pontos
de mudança.

Na 1.4 o loop usa switch(indexToMenuItem(...)) com default: break. A
estratégia: OTA apps ficam em índices além de SETTINGS_MENU.
indexToMenuItem() retorna NONE para esses índices — o default: do switch
já os ignora. Adicionamos um bloco separado após o switch para tratar o
índice OTA.
"""

path = "crosspoint/src/activities/home/HomeActivity.cpp"
src = open(path).read()

# 3a. getMenuItemCount: incluir otaAppCount na contagem
old_count = "int count = 4;  // File Browser, Recents, File transfer, Settings"
new_count = "int count = 4 + otaAppCount;  // File Browser, Recents, File transfer, Settings, OTA apps"
assert old_count in src, "getMenuItemCount base count not found"
src = src.replace(old_count, new_count)

# 3b. onEnter: detectar OTA apps logo após hasOpdsServers
old_enter = "hasOpdsServers = OPDS_STORE.hasServers();"
new_enter = "hasOpdsServers = OPDS_STORE.hasServers();\n  otaAppCount = detectOtaApps(otaApps, MAX_OTA_APPS);"
assert old_enter in src, "hasOpdsServers assignment not found"
src = src.replace(old_enter, new_enter)

# 3c. loop: adicionar tratamento OTA após o bloco switch/case.
#     O default: já está presente — inserimos o bloco OTA dentro dele,
#     substituindo "default:\n          break;" pelo bloco expandido.
old_switch_full = '        default:\n          break;\n      }\n    }\n  }\n}'
new_switch_full = (
    '        default: {\n'
    '          // Índices além de SETTINGS_MENU são OTA apps\n'
    '          const int settingsMenuCount = hasOpdsServers ? 5 : 4;\n'
    '          const int otaIdx = menuIndex - settingsMenuCount;\n'
    '          if (otaIdx >= 0 && otaIdx < otaAppCount) {\n'
    '            switchToOtaApp(otaApps[otaIdx].partitionSubtype);\n'
    '          }\n'
    '          break;\n'
    '        }\n      }\n    }\n  }\n}'
)
assert old_switch_full in src, "menu switch default: block not found"
src = src.replace(old_switch_full, new_switch_full)

# 3d. render: adicionar OTA apps no vetor antes de GUI.drawButtonMenu
old_draw = "  GUI.drawButtonMenu("
new_draw = (
    "  for (int i = 0; i < otaAppCount; i++) {\n"
    "    menuItems.push_back(otaApps[i].name);\n"
    "    menuIcons.push_back(Text);\n"
    "  }\n\n  GUI.drawButtonMenu("
)
assert old_draw in src, "GUI.drawButtonMenu( call not found"
src = src.replace(old_draw, new_draw)

open(path, "w").write(src)
print("HomeActivity.cpp: OK")
