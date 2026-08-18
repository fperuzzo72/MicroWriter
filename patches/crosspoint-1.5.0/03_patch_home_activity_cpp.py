"""Patch em crosspoint/src/activities/home/HomeActivity.cpp — quatro pontos
de mudança.

Igual à estratégia usada em patches/crosspoint/ (1.4.1): OTA apps ficam em
índices além de SETTINGS_MENU. indexToMenuItem() retorna NONE para esses
índices — o default: do switch já os ignora. Adicionamos o tratamento do
índice OTA dentro desse default.

Única diferença real vs. 1.4.1: na v1.5.0 o CrossPoint moveu o corpo do
antigo `if (mappedInput.wasReleased(Confirm)) { switch(...) { ... } }` para
dentro de uma lambda `activateSelection` (agora reusada também pelos novos
handlers de toque/swipe introduzidos nessa versão). O switch em si e seus
cases são idênticos aos da 1.4.1 — só a estrutura de chaves ao redor mudou
(a lambda fecha com `};` logo após o switch, em vez do aninhamento
if/switch/function de quatro níveis da 1.4.1). O patch 3c abaixo foi
reescrito para esse novo fechamento; 3a/3b/3d são idênticos à 1.4.1.
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

# 3c. loop: adicionar tratamento OTA dentro do default: do switch, agora
#     dentro da lambda activateSelection (fecha com "    }\n  };").
old_switch_full = "      default:\n        break;\n    }\n  };"
new_switch_full = (
    "      default: {\n"
    "        // Índices além de SETTINGS_MENU são OTA apps\n"
    "        const int settingsMenuCount = hasOpdsServers ? 5 : 4;\n"
    "        const int otaIdx = menuIndex - settingsMenuCount;\n"
    "        if (otaIdx >= 0 && otaIdx < otaAppCount) {\n"
    "          switchToOtaApp(otaApps[otaIdx].partitionSubtype);\n"
    "        }\n"
    "        break;\n"
    "      }\n"
    "    }\n"
    "  };"
)
assert old_switch_full in src, "activateSelection switch default: block not found"
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
