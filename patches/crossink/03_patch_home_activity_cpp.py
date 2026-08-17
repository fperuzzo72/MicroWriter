"""Patch em crossink/src/activities/home/HomeActivity.cpp.

O menu do CrossInk é montado dinamicamente (HomeMenuEntries, capacidade fixa
de 8) e reconstruído em ~8 pontos diferentes do arquivo (tema cheio, tema
carrossel, tema minimal, contagem de itens...). Em vez de alterar a assinatura
das 3 funções builder e todos os call sites (frágil para um patch
automatizado), usamos um ponteiro de escopo de arquivo (g_otaApps/
g_otaAppCount) preenchido em onEnter() e lido dentro das duas funções builder
compartilhadas (appendHomeMenuItems e buildMinimalMenuItems) — todos os 8
call sites passam a incluir os itens OTA automaticamente, sem precisar tocar
neles.
"""

path = "crossink/src/activities/home/HomeActivity.cpp"
src = open(path).read()


def apply(old, new, label):
    global src
    assert old in src, f"pattern not found: {label}"
    src = src.replace(old, new)


# 1. Enum HomeMenuAction: adiciona OtaApp + ponteiro de escopo de arquivo
#    para os apps OTA detectados.
old_enum = """enum class HomeMenuAction {
  BrowseFiles,
  ContinueReading,
  RecentBooks,
  OpdsBrowser,
  ReadingStats,
  Bookmarks,
  FileTransfer,
  Settings,
};"""
new_enum = """enum class HomeMenuAction {
  BrowseFiles,
  ContinueReading,
  RecentBooks,
  OpdsBrowser,
  ReadingStats,
  Bookmarks,
  FileTransfer,
  Settings,
  OtaApp,
};

// Preenchido por HomeActivity::onEnter() a cada entrada na Home. Lido pelas
// funções builder abaixo para anexar entradas de apps OTA irmãos
// (MicroSlate) ao menu, sem precisar mudar a assinatura das builder
// functions nem os ~8 call sites que as usam.
const OtaAppEntry* g_otaApps = nullptr;
int g_otaAppCount = 0;"""
apply(old_enum, new_enum, "enum HomeMenuAction")

# 2. Campo otaIndex na struct HomeMenuEntry (default -1: não é um app OTA).
old_struct = """struct HomeMenuEntry {
  const char* label;
  UIIcon icon;
  HomeMenuAction action;
};"""
new_struct = """struct HomeMenuEntry {
  const char* label;
  UIIcon icon;
  HomeMenuAction action;
  int otaIndex = -1;  // índice em g_otaApps[], válido apenas quando action == OtaApp
};"""
apply(old_struct, new_struct, "struct HomeMenuEntry")

# 3. Capacidade do array de menu: 8 fixos + até MAX_OTA_APPS extras.
apply(
    "static constexpr int kCapacity = 8;",
    "static constexpr int kCapacity = 8 + MAX_OTA_APPS;",
    "kCapacity",
)

# 4. appendHomeMenuItems: usado por buildHomeMenuItems E
#    buildSelectableHomeMenuItems — cobre o menu "cheio" (temas clássico e
#    carrossel) em todos os pontos onde é chamado.
old_append_end = """  items.push({tr(STR_FILE_TRANSFER), Transfer, HomeMenuAction::FileTransfer});
  items.push({tr(STR_SETTINGS_TITLE), Settings, HomeMenuAction::Settings});
}"""
new_append_end = """  items.push({tr(STR_FILE_TRANSFER), Transfer, HomeMenuAction::FileTransfer});
  items.push({tr(STR_SETTINGS_TITLE), Settings, HomeMenuAction::Settings});

  for (int i = 0; i < g_otaAppCount; i++) {
    items.push({g_otaApps[i].name, Text, HomeMenuAction::OtaApp, i});
  }
}"""
apply(old_append_end, new_append_end, "appendHomeMenuItems end")

# 5. buildMinimalMenuItems: cobre o tema minimal separadamente, já que ele
#    não passa por appendHomeMenuItems.
old_minimal_end = """  items.push({tr(STR_FILE_TRANSFER), Transfer, HomeMenuAction::FileTransfer});
  return items;
}"""
new_minimal_end = """  items.push({tr(STR_FILE_TRANSFER), Transfer, HomeMenuAction::FileTransfer});

  for (int i = 0; i < g_otaAppCount; i++) {
    items.push({g_otaApps[i].name, Text, HomeMenuAction::OtaApp, i});
  }

  return items;
}"""
apply(old_minimal_end, new_minimal_end, "buildMinimalMenuItems end")

# 6. getMenuItemCount(): usado para navegação/paginação no tema carrossel, é
#    calculado à parte (não usa as builder functions).
old_count = """  if (hasBookmarks || hasClippings) {
    count++;
  }
  return count;
}"""
new_count = """  if (hasBookmarks || hasClippings) {
    count++;
  }
  count += otaAppCount;
  return count;
}"""
apply(old_count, new_count, "getMenuItemCount tail")

# 7. onEnter(): detectar apps OTA e publicar no ponteiro de escopo de
#    arquivo, logo após hasOpdsServers ser calculado.
old_enter = "  hasOpdsServers = OPDS_STORE.hasServers();"
new_enter = (
    "  hasOpdsServers = OPDS_STORE.hasServers();\n"
    "  otaAppCount = detectOtaApps(otaApps, MAX_OTA_APPS);\n"
    "  g_otaApps = otaApps;\n"
    "  g_otaAppCount = otaAppCount;"
)
assert old_enter in src, "onEnter hasOpdsServers line not found"
src = src.replace(old_enter, new_enter, 1)  # só a primeira ocorrência (onEnter)

# 8. Dispatch — menu minimal (switch dentro de minimalMenuOpen).
old_switch_minimal = """          case HomeMenuAction::FileTransfer:
            onFileTransferOpen();
            break;
          case HomeMenuAction::ContinueReading:
          case HomeMenuAction::Settings:
            break;
        }"""
new_switch_minimal = """          case HomeMenuAction::FileTransfer:
            onFileTransferOpen();
            break;
          case HomeMenuAction::OtaApp:
            switchToOtaApp(otaApps[menuItems[minimalMenuIndex].otaIndex].partitionSubtype);
            break;
          case HomeMenuAction::ContinueReading:
          case HomeMenuAction::Settings:
            break;
        }"""
apply(old_switch_minimal, new_switch_minimal, "minimal menu switch")

# 9. Dispatch — menu cheio (switch dentro do Confirm handler principal).
old_switch_full = """      case HomeMenuAction::FileTransfer:
        onFileTransferOpen();
        break;
      case HomeMenuAction::Settings:
        onSettingsOpen();
        break;
    }
  }
}"""
new_switch_full = """      case HomeMenuAction::FileTransfer:
        onFileTransferOpen();
        break;
      case HomeMenuAction::Settings:
        onSettingsOpen();
        break;
      case HomeMenuAction::OtaApp:
        switchToOtaApp(otaApps[menuItems[menuSelectedIndex].otaIndex].partitionSubtype);
        break;
    }
  }
}"""
apply(old_switch_full, new_switch_full, "full menu switch")

open(path, "w").write(src)
print("HomeActivity.cpp: OK")
