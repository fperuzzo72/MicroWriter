#include "I18n.h"

#include <cstring>

namespace sumi {

// ============================================================================
// Translation tables — static const arrays per language.
// Must be in SAME order as StrId enum in I18n.h. The static_assert at the
// bottom of this file catches drift.
// nullptr entries fall back to English.
//
// Latin-script languages (EN/ES/FR/DE/PT/IT/PL/NL) stay within ASCII to
// match the on-device UI font coverage. CJK/Cyrillic/Arabic use native
// UTF-8 because the device has font families for those scripts.
// ============================================================================

static const char* const EN_STRINGS[] = {
  // Home
  "No book open",
  "Press \"Files\" to browse",
  "< FILES",
  "MENU >",
  "Chapter %d of %d",
  "Page %d of %d",
  "%d%% complete",
  "Chapters",
  "Files",

  // Settings
  "Settings",
  "Apps",
  "Home Art",
  "Wireless Transfer",
  "Reader",
  "Device",
  "Bluetooth",
  "Cleanup",
  "System Info",
  "App Visibility",

  // Reader labels
  "Reader Settings",
  "Theme",
  "Font",
  "Font Size",
  "Text Layout",
  "Line Spacing",
  "Text Darkness",
  "Hyphenation",
  "Anti-Aliasing",
  "Show Images",
  "Show Tables",
  "Status Bar",
  "Paragraph Alignment",
  "Reading Orientation",
  "Look up Word",
  "Lookup History",
  "Toggle Bookmark",
  "View Bookmarks",
  "All Bookmarks",

  // Reader value enums
  "XSmall", "Small", "Normal", "Large",
  "Compact", "Standard", "Relaxed",
  "Justified", "Left", "Center", "Right", "Book's Style",
  "Dark", "Extra Dark", "Maximum",
  "Show", "Placeholder", "None",
  "Portrait", "Landscape CW", "Inverted", "Landscape CCW",

  // Device labels
  "Auto Sleep Timeout",
  "Sleep Screen",
  "Startup Behavior",
  "Short Power Button",
  "Pages Per Refresh",
  "Sunlight Fading Fix",
  "Front Buttons",
  "Side Buttons",
  "Language",

  // Device value enums
  "5 min", "10 min", "15 min", "30 min", "Never",
  "Dark", "Light", "Custom", "Cover", "Last Page",
  "Last Document", "Home",
  "Ignore", "Sleep", "Page Turn", "Refresh",
  "Prev/Next", "Next/Prev",

  // Cleanup
  "Clear Book Cache",
  "Forget Bluetooth Devices",
  "Clear Device Storage",
  "Factory Reset",

  // File browser
  "File Browser",
  "No files found",

  // Plugins
  "Apps",

  // Sleep
  "SLEEPING",

  // Common actions
  "Back", "OK", "Cancel", "Delete", "ON", "OFF", "Loading...", "Error", "Yes", "No",

  // Button bar verbs
  "Open", "Run", "Select", "Go", "Toggle", "Confirm", "Enable", "Disable",

  // System info
  "Version", "Uptime", "Battery", "Free Memory", "SD Card", "Reading", "Books",
};

// ---------------------------------------------------------------------------
//  Spanish
// ---------------------------------------------------------------------------
static const char* const ES_STRINGS[] = {
  "No hay libro abierto",
  "Pulse \"Archivos\" para navegar",
  "< ARCHIVOS",
  "MENU >",
  "Capitulo %d de %d",
  "Pagina %d de %d",
  "%d%% completo",
  "Capitulos",
  "Archivos",

  "Ajustes", "Apps", "Fondo de inicio", "Transferencia",
  "Lector", "Dispositivo", "Bluetooth", "Limpieza", "Info del sistema",
  "Visibilidad de apps",

  "Ajustes del lector",
  "Tema", "Fuente", "Tamano de fuente", "Disposicion", "Interlineado",
  "Oscuridad del texto", "Guiones", "Suavizado", "Mostrar imagenes",
  "Mostrar tablas", "Barra de estado", "Alineacion", "Orientacion",
  "Buscar palabra", "Historial", "Marcador", "Ver marcadores", "Todos los marcadores",

  "MuyPeq", "Peq", "Normal", "Grande",
  "Compacto", "Estandar", "Amplio",
  "Justif.", "Izq", "Centro", "Der", "Del libro",
  "Oscuro", "Muy oscuro", "Maximo",
  "Mostrar", "Marcador", "Ninguno",
  "Vertical", "Horiz. CW", "Invertido", "Horiz. CCW",

  "Tiempo de espera", "Pantalla de bloqueo", "Al encender", "Boton breve",
  "Paginas por refresco", "Antireflejo solar", "Botones frontales", "Botones laterales",
  "Idioma",

  "5 min", "10 min", "15 min", "30 min", "Nunca",
  "Oscuro", "Claro", "Personal.", "Portada", "Ultima pag",
  "Ultimo documento", "Inicio",
  "Ignorar", "Dormir", "Pag. sig", "Refresco",
  "Prev/Sig", "Sig/Prev",

  "Borrar cache", "Olvidar Bluetooth", "Borrar almacenamiento", "Restablecer",

  "Explorador", "Sin archivos",
  "Apps",
  "DURMIENDO",

  "Atras", "OK", "Cancelar", "Eliminar", "SI", "NO", "Cargando...", "Error", "Si", "No",

  "Abrir", "Ejecutar", "Elegir", "Ir", "Alternar", "Confirmar", "Activar", "Desactivar",

  "Version", "Tiempo activo", "Bateria", "Memoria libre", "Tarjeta SD", "Lectura", "Libros",
};

// ---------------------------------------------------------------------------
//  French
// ---------------------------------------------------------------------------
static const char* const FR_STRINGS[] = {
  "Aucun livre ouvert",
  "Appuyez sur \"Fichiers\"",
  "< FICHIERS",
  "MENU >",
  "Chapitre %d sur %d",
  "Page %d sur %d",
  "%d%% lu",
  "Chapitres",
  "Fichiers",

  "Parametres", "Apps", "Art d'accueil", "Transfert sans fil",
  "Lecteur", "Appareil", "Bluetooth", "Nettoyage", "Info systeme",
  "Visibilite des apps",

  "Parametres lecteur",
  "Theme", "Police", "Taille", "Mise en page", "Interligne",
  "Noirceur du texte", "Cesure", "Anti-crenelage", "Afficher images",
  "Afficher tableaux", "Barre d'etat", "Alignement", "Orientation",
  "Chercher un mot", "Historique", "Marque-page", "Voir marque-pages", "Tous marque-pages",

  "T.petit", "Petit", "Normal", "Grand",
  "Compact", "Standard", "Detendu",
  "Justifie", "Gauche", "Centre", "Droite", "Du livre",
  "Sombre", "Tres sombre", "Maximum",
  "Afficher", "Espace", "Aucun",
  "Portrait", "Paysage CW", "Inverse", "Paysage CCW",

  "Veille auto", "Ecran de veille", "Au demarrage", "Power court",
  "Pages par rafraich.", "Correction soleil", "Boutons avant", "Boutons cotes",
  "Langue",

  "5 min", "10 min", "15 min", "30 min", "Jamais",
  "Sombre", "Clair", "Personnel", "Couverture", "Derniere page",
  "Dernier document", "Accueil",
  "Ignorer", "Veille", "Page suiv.", "Rafraichir",
  "Prec/Suiv", "Suiv/Prec",

  "Effacer le cache", "Oublier Bluetooth", "Effacer le stockage", "Reinit. usine",

  "Explorateur", "Aucun fichier",
  "Apps",
  "EN VEILLE",

  "Retour", "OK", "Annuler", "Supprimer", "OUI", "NON", "Chargement...", "Erreur", "Oui", "Non",

  "Ouvrir", "Executer", "Choisir", "Aller", "Basculer", "Confirmer", "Activer", "Desactiver",

  "Version", "Disponibilite", "Batterie", "Memoire libre", "Carte SD", "Lecture", "Livres",
};

// ---------------------------------------------------------------------------
//  German
// ---------------------------------------------------------------------------
static const char* const DE_STRINGS[] = {
  "Kein Buch offen",
  "\"Dateien\" zum Durchsuchen",
  "< DATEIEN",
  "MENU >",
  "Kapitel %d von %d",
  "Seite %d von %d",
  "%d%% gelesen",
  "Kapitel",
  "Dateien",

  "Einstellungen", "Apps", "Startbild", "Drahtlos",
  "Leser", "Gerat", "Bluetooth", "Bereinigung", "Systeminfo",
  "App-Sichtbarkeit",

  "Leser-Einstellungen",
  "Design", "Schriftart", "Schriftgrosse", "Textlayout", "Zeilenabstand",
  "Textdunkelheit", "Silbentrennung", "Kantenglattung", "Bilder anzeigen",
  "Tabellen anzeigen", "Statusleiste", "Ausrichtung", "Leseausrichtung",
  "Wort nachschlagen", "Suchverlauf", "Lesezeichen", "Lesezeichen ansehen", "Alle Lesezeichen",

  "Sehr kl.", "Klein", "Normal", "Gross",
  "Kompakt", "Standard", "Entspannt",
  "Blocksatz", "Links", "Mitte", "Rechts", "Wie Buch",
  "Dunkel", "Sehr dunkel", "Maximum",
  "Zeigen", "Platzhalter", "Keine",
  "Hochformat", "Quer CW", "Kopfuber", "Quer CCW",

  "Auto-Ruhezeit", "Ruhebildschirm", "Beim Start", "Kurzer Power",
  "Seiten pro Refresh", "Sonnenfleckfix", "Vordertasten", "Seitentasten",
  "Sprache",

  "5 Min.", "10 Min.", "15 Min.", "30 Min.", "Nie",
  "Dunkel", "Hell", "Eigenes", "Cover", "Letzte Seite",
  "Letztes Dok.", "Start",
  "Ignorieren", "Ruhe", "Blattern", "Refresh",
  "Vor/Zuruck", "Zuruck/Vor",

  "Buch-Cache loschen", "Bluetooth vergessen", "Speicher loschen", "Werkseinstellungen",

  "Dateimanager", "Keine Dateien",
  "Apps",
  "SCHLAFMODUS",

  "Zuruck", "OK", "Abbrechen", "Loschen", "AN", "AUS", "Laden...", "Fehler", "Ja", "Nein",

  "Offnen", "Starten", "Wahlen", "Los", "Umschalten", "Bestatigen", "Aktivieren", "Deaktivieren",

  "Version", "Betriebszeit", "Akku", "Freier Speicher", "SD-Karte", "Lesen", "Bucher",
};

// ---------------------------------------------------------------------------
//  Portuguese
// ---------------------------------------------------------------------------
static const char* const PT_STRINGS[] = {
  "Nenhum livro aberto",
  "Pressione \"Arquivos\"",
  "< ARQUIVOS",
  "MENU >",
  "Capitulo %d de %d",
  "Pagina %d de %d",
  "%d%% completo",
  "Capitulos",
  "Arquivos",

  "Configuracoes", "Apps", "Arte inicial", "Transferencia",
  "Leitor", "Dispositivo", "Bluetooth", "Limpeza", "Info do sistema",
  "Visibilidade de apps",

  "Config. do leitor",
  "Tema", "Fonte", "Tamanho", "Layout", "Espacamento",
  "Escuridao do texto", "Hifenizacao", "Anti-Aliasing", "Mostrar imagens",
  "Mostrar tabelas", "Barra de estado", "Alinhamento", "Orientacao",
  "Buscar palavra", "Historico", "Marcador", "Ver marcadores", "Todos marcadores",

  "MuitoPeq", "Peq", "Normal", "Grande",
  "Compacto", "Padrao", "Folgado",
  "Justif.", "Esq", "Centro", "Dir", "Do livro",
  "Escuro", "Muito escuro", "Maximo",
  "Mostrar", "Marcador", "Nenhum",
  "Retrato", "Paisagem CW", "Invertido", "Paisagem CCW",

  "Tempo de espera", "Tela de bloqueio", "Ao ligar", "Power breve",
  "Paginas por refresco", "Correcao de sol", "Botoes frontais", "Botoes laterais",
  "Idioma",

  "5 min", "10 min", "15 min", "30 min", "Nunca",
  "Escuro", "Claro", "Personal.", "Capa", "Ultima pag",
  "Ultimo documento", "Inicio",
  "Ignorar", "Dormir", "Pag. seg", "Refresh",
  "Ant/Prox", "Prox/Ant",

  "Limpar cache", "Esquecer Bluetooth", "Limpar armazenamento", "Restaurar",

  "Explorador", "Sem arquivos",
  "Apps",
  "DORMINDO",

  "Voltar", "OK", "Cancelar", "Excluir", "SIM", "NAO", "Carregando...", "Erro", "Sim", "Nao",

  "Abrir", "Executar", "Escolher", "Ir", "Alternar", "Confirmar", "Ativar", "Desativar",

  "Versao", "Tempo ativo", "Bateria", "Memoria livre", "Cartao SD", "Leitura", "Livros",
};

// ---------------------------------------------------------------------------
//  Italian
// ---------------------------------------------------------------------------
static const char* const IT_STRINGS[] = {
  "Nessun libro aperto",
  "Premi \"File\" per sfogliare",
  "< FILE",
  "MENU >",
  "Capitolo %d di %d",
  "Pagina %d di %d",
  "%d%% letto",
  "Capitoli",
  "File",

  "Impostazioni", "Apps", "Sfondo iniziale", "Trasferimento",
  "Lettore", "Dispositivo", "Bluetooth", "Pulizia", "Info sistema",
  "Visibilita app",

  "Impostazioni lettore",
  "Tema", "Font", "Dimensione", "Layout", "Interlinea",
  "Intensita testo", "Sillabazione", "Anti-alias", "Mostra immagini",
  "Mostra tabelle", "Barra di stato", "Allineamento", "Orientamento",
  "Cerca parola", "Cronologia", "Segnalibro", "Vedi segnalibri", "Tutti i segnalibri",

  "MoltoPic", "Piccolo", "Normale", "Grande",
  "Compatto", "Standard", "Rilassato",
  "Giustif.", "Sinistra", "Centro", "Destra", "Del libro",
  "Scuro", "Molto scuro", "Massimo",
  "Mostra", "Spazio", "Nessuna",
  "Verticale", "Orizz. CW", "Capovolto", "Orizz. CCW",

  "Attesa sospens.", "Schermo sospeso", "All'avvio", "Power breve",
  "Pagine per refresh", "Correzione sole", "Pulsanti front", "Pulsanti lat",
  "Lingua",

  "5 min", "10 min", "15 min", "30 min", "Mai",
  "Scuro", "Chiaro", "Person.", "Copertina", "Ultima pag",
  "Ultimo doc", "Home",
  "Ignora", "Sospendi", "Sfoglia", "Refresh",
  "Prec/Succ", "Succ/Prec",

  "Svuota cache libri", "Dimentica Bluetooth", "Svuota archiviazione", "Reset fabbrica",

  "File Browser", "Nessun file",
  "Apps",
  "IN SOSPENS.",

  "Indietro", "OK", "Annulla", "Elimina", "ACC", "SPE", "Caricamento...", "Errore", "Si", "No",

  "Apri", "Esegui", "Scegli", "Vai", "Alterna", "Conferma", "Abilita", "Disabilita",

  "Versione", "Uptime", "Batteria", "Memoria libera", "Scheda SD", "Lettura", "Libri",
};

// ---------------------------------------------------------------------------
//  Russian (Cyrillic UTF-8)
// ---------------------------------------------------------------------------
static const char* const RU_STRINGS[] = {
  "Нет открытой книги",
  "Нажмите \"Файлы\" для обзора",
  "< ФАЙЛЫ",
  "МЕНЮ >",
  "Глава %d из %d",
  "Страница %d из %d",
  "%d%% прочитано",
  "Главы",
  "Файлы",

  "Настройки", "Приложения", "Фон главной", "Передача",
  "Читалка", "Устройство", "Bluetooth", "Очистка", "О системе",
  "Видимость приложений",

  "Настройки читалки",
  "Тема", "Шрифт", "Размер шрифта", "Раскладка", "Межстрочный",
  "Темнота текста", "Переносы", "Сглаживание", "Изображения",
  "Таблицы", "Статусбар", "Выравнивание", "Ориентация",
  "Найти слово", "История поиска", "Закладка", "Закладки", "Все закладки",

  "Оч.мал", "Малый", "Обычный", "Большой",
  "Сжатый", "Станд.", "Свобод.",
  "По шир.", "Слева", "Центр", "Справа", "Как в книге",
  "Темный", "Очень темный", "Макс.",
  "Показ.", "Метка", "Нет",
  "Портрет", "Пейзаж CW", "Перевернут", "Пейзаж CCW",

  "Таймер сна", "Экран сна", "При включении", "Кор. питание",
  "Страниц на обновл.", "Корр. выцветания", "Передние кнопки", "Боковые кнопки",
  "Язык",

  "5 мин", "10 мин", "15 мин", "30 мин", "Никогда",
  "Темный", "Светлый", "Свой", "Обложка", "Посл. стр.",
  "Посл. документ", "Главная",
  "Игнор.", "Сон", "Перелист.", "Обновл.",
  "Пред/След", "След/Пред",

  "Очистить кэш книг", "Забыть Bluetooth", "Очистить память", "Сброс к заводским",

  "Файлы", "Нет файлов",
  "Приложения",
  "СПЯЩИЙ",

  "Назад", "OK", "Отмена", "Удалить", "ВКЛ", "ВЫКЛ", "Загрузка...", "Ошибка", "Да", "Нет",

  "Открыть", "Запустить", "Выбрать", "Перейти", "Переключ.", "Подтвердить", "Включить", "Выключить",

  "Версия", "Время работы", "Батарея", "Своб. память", "SD карта", "Чтение", "Книги",
};

// ---------------------------------------------------------------------------
//  Polish
// ---------------------------------------------------------------------------
static const char* const PL_STRINGS[] = {
  "Brak otwartej ksiazki",
  "Nacisnij \"Pliki\"",
  "< PLIKI",
  "MENU >",
  "Rozdzial %d z %d",
  "Strona %d z %d",
  "%d%% przeczytane",
  "Rozdzialy",
  "Pliki",

  "Ustawienia", "Aplikacje", "Tlo glowne", "Transfer",
  "Czytnik", "Urzadzenie", "Bluetooth", "Czyszczenie", "Info systemu",
  "Widocznosc aplikacji",

  "Ustawienia czytnika",
  "Motyw", "Czcionka", "Rozmiar", "Uklad", "Interlinia",
  "Ciemnosc tekstu", "Dzielenie", "Wygladzanie", "Pokaz obrazy",
  "Pokaz tabele", "Pasek stanu", "Wyrownanie", "Orientacja",
  "Szukaj slowa", "Historia", "Zakladka", "Zobacz zakladki", "Wszystkie zakladki",

  "B.maly", "Maly", "Normal", "Duzy",
  "Zwarty", "Standard", "Luzny",
  "Justuj", "Lewo", "Srodek", "Prawo", "Jak ksiazka",
  "Ciemny", "B.ciemny", "Maks.",
  "Pokaz", "Miejsce", "Brak",
  "Pionowo", "Poziom CW", "Do gory nogami", "Poziom CCW",

  "Auto-wstrzym.", "Ekran uspienia", "Przy starcie", "Krotki power",
  "Stron na odswiez.", "Korekta slonca", "Przyciski przod", "Przyciski bok",
  "Jezyk",

  "5 min", "10 min", "15 min", "30 min", "Nigdy",
  "Ciemny", "Jasny", "Wlasny", "Okladka", "Ost. strona",
  "Ost. dokument", "Glowna",
  "Ignoruj", "Uspij", "Str. nast.", "Odswiez",
  "Poprz/Nast", "Nast/Poprz",

  "Wyczysc cache", "Zapomnij Bluetooth", "Wyczysc pamiec", "Przywroc ustawienia",

  "Pliki", "Brak plikow",
  "Aplikacje",
  "USPIONY",

  "Wstecz", "OK", "Anuluj", "Usun", "WL", "WYL", "Ladowanie...", "Blad", "Tak", "Nie",

  "Otworz", "Uruchom", "Wybierz", "Przejdz", "Przelacz", "Potwierdz", "Wlacz", "Wylacz",

  "Wersja", "Czas pracy", "Bateria", "Wolna pamiec", "Karta SD", "Czytanie", "Ksiazki",
};

// ---------------------------------------------------------------------------
//  Dutch
// ---------------------------------------------------------------------------
static const char* const NL_STRINGS[] = {
  "Geen boek open",
  "Druk op \"Bestanden\"",
  "< BESTANDEN",
  "MENU >",
  "Hoofdstuk %d van %d",
  "Pagina %d van %d",
  "%d%% voltooid",
  "Hoofdstukken",
  "Bestanden",

  "Instellingen", "Apps", "Startafbeelding", "Draadloos",
  "Lezer", "Apparaat", "Bluetooth", "Opruimen", "Systeeminfo",
  "App-zichtbaarheid",

  "Lezer-instellingen",
  "Thema", "Lettertype", "Grootte", "Opmaak", "Regelafstand",
  "Tekst-donkerheid", "Afbreken", "Anti-alias", "Toon afbeeldingen",
  "Toon tabellen", "Statusbalk", "Uitlijning", "Orientatie",
  "Zoek woord", "Geschiedenis", "Bladwijzer", "Bekijk bladwijzers", "Alle bladwijzers",

  "Zeer kl", "Klein", "Normaal", "Groot",
  "Compact", "Standaard", "Ontspannen",
  "Uitgevuld", "Links", "Midden", "Rechts", "Boekstijl",
  "Donker", "Zeer donker", "Maximum",
  "Tonen", "Plaatshouder", "Geen",
  "Staand", "Liggend CW", "Omgekeerd", "Liggend CCW",

  "Slaap-timer", "Slaapscherm", "Bij opstart", "Korte power",
  "Paginas per ververs.", "Zonvlek-fix", "Voorknoppen", "Zijknoppen",
  "Taal",

  "5 min", "10 min", "15 min", "30 min", "Nooit",
  "Donker", "Licht", "Aangepast", "Omslag", "Laatste pag",
  "Laatste doc", "Start",
  "Negeren", "Slapen", "Bladeren", "Ververs",
  "Vorige/Volg", "Volg/Vorige",

  "Cache wissen", "Bluetooth vergeten", "Opslag wissen", "Fabrieksreset",

  "Bestanden", "Geen bestanden",
  "Apps",
  "SLAAPT",

  "Terug", "OK", "Annuleer", "Verwijder", "AAN", "UIT", "Laden...", "Fout", "Ja", "Nee",

  "Open", "Start", "Kies", "Ga", "Wissel", "Bevestig", "Inschakelen", "Uitschakelen",

  "Versie", "Uptime", "Batterij", "Vrij geheugen", "SD-kaart", "Lezen", "Boeken",
};

// ---------------------------------------------------------------------------
//  Japanese (UTF-8, native script)
// ---------------------------------------------------------------------------
static const char* const JA_STRINGS[] = {
  "本が開いていません",
  "「ファイル」を押す",
  "< ファイル",
  "メニュー >",
  "第%d章 / 全%d章",
  "%d / %d ページ",
  "%d%% 完了",
  "章",
  "ファイル",

  "設定", "アプリ", "ホームアート", "ワイヤレス転送",
  "リーダー", "デバイス", "Bluetooth", "クリーニング", "システム情報",
  "アプリ表示設定",

  "リーダー設定",
  "テーマ", "フォント", "文字サイズ", "レイアウト", "行間",
  "文字の濃さ", "ハイフネーション", "アンチエイリアス", "画像表示",
  "表の表示", "ステータスバー", "段落揃え", "画面の向き",
  "単語検索", "検索履歴", "しおり切替", "しおり一覧", "全しおり",

  "極小", "小", "標準", "大",
  "密", "標準", "広",
  "両端揃え", "左寄せ", "中央", "右寄せ", "書籍準拠",
  "濃い", "非常に濃い", "最大",
  "表示", "代替", "なし",
  "縦向き", "横CW", "反転", "横CCW",

  "自動スリープ", "スリープ画面", "起動時の動作", "電源短押し",
  "リフレッシュ間隔", "日光補正", "前面ボタン", "側面ボタン",
  "言語",

  "5分", "10分", "15分", "30分", "なし",
  "暗", "明", "カスタム", "表紙", "最終ページ",
  "前回の書籍", "ホーム",
  "無視", "スリープ", "ページ送り", "リフレッシュ",
  "前/次", "次/前",

  "書籍キャッシュ消去", "Bluetooth機器を忘れる", "本体ストレージ消去", "工場出荷時設定",

  "ファイル", "ファイルなし",
  "アプリ",
  "スリープ中",

  "戻る", "OK", "キャンセル", "削除", "オン", "オフ", "読込中...", "エラー", "はい", "いいえ",

  "開く", "実行", "選択", "移動", "切替", "確定", "有効化", "無効化",

  "バージョン", "稼働時間", "バッテリー", "空きメモリ", "SDカード", "読書", "書籍",
};

// ---------------------------------------------------------------------------
//  Chinese (Simplified, UTF-8)
// ---------------------------------------------------------------------------
static const char* const ZH_STRINGS[] = {
  "未打开书籍",
  "按\"文件\"浏览",
  "< 文件",
  "菜单 >",
  "第 %d 章 / 共 %d 章",
  "第 %d 页 / 共 %d 页",
  "已读 %d%%",
  "章节",
  "文件",

  "设置", "应用", "主屏壁纸", "无线传输",
  "阅读器", "设备", "蓝牙", "清理", "系统信息",
  "应用可见性",

  "阅读器设置",
  "主题", "字体", "字号", "文本布局", "行间距",
  "文字深度", "断字", "抗锯齿", "显示图像",
  "显示表格", "状态栏", "段落对齐", "阅读方向",
  "查词", "查词历史", "切换书签", "查看书签", "全部书签",

  "特小", "小", "标准", "大",
  "紧凑", "标准", "宽松",
  "两端对齐", "左对齐", "居中", "右对齐", "书本风格",
  "深", "特深", "最大",
  "显示", "占位", "无",
  "竖屏", "横屏 CW", "翻转", "横屏 CCW",

  "自动休眠", "休眠画面", "启动行为", "电源短按",
  "每页刷新", "日光褪色修正", "前按键", "侧按键",
  "语言",

  "5 分钟", "10 分钟", "15 分钟", "30 分钟", "永不",
  "深色", "浅色", "自定义", "封面", "上次页面",
  "上次文档", "主屏",
  "忽略", "休眠", "翻页", "刷新",
  "上/下", "下/上",

  "清除书籍缓存", "忘记蓝牙设备", "清除设备存储", "恢复出厂",

  "文件浏览器", "无文件",
  "应用",
  "休眠中",

  "返回", "确定", "取消", "删除", "开", "关", "加载中...", "错误", "是", "否",

  "打开", "运行", "选择", "前往", "切换", "确认", "启用", "停用",

  "版本", "运行时间", "电池", "可用内存", "SD卡", "阅读", "书籍",
};

// ---------------------------------------------------------------------------
//  Korean (Hangul, UTF-8)
// ---------------------------------------------------------------------------
static const char* const KO_STRINGS[] = {
  "열린 책 없음",
  "\"파일\"을 눌러 탐색",
  "< 파일",
  "메뉴 >",
  "%d장 / 전체 %d장",
  "%d쪽 / 전체 %d쪽",
  "%d%% 완료",
  "장",
  "파일",

  "설정", "앱", "홈 배경", "무선 전송",
  "리더", "기기", "블루투스", "정리", "시스템 정보",
  "앱 표시",

  "리더 설정",
  "테마", "글꼴", "글자 크기", "레이아웃", "줄 간격",
  "글자 농도", "하이픈", "안티에일리어싱", "이미지 표시",
  "표 표시", "상태바", "문단 정렬", "화면 방향",
  "단어 찾기", "검색 기록", "책갈피 토글", "책갈피 보기", "모든 책갈피",

  "초소", "작게", "보통", "크게",
  "빽빽", "표준", "여유",
  "양쪽맞춤", "왼쪽", "중앙", "오른쪽", "책 스타일",
  "진하게", "매우 진하게", "최대",
  "표시", "빈자리", "없음",
  "세로", "가로 CW", "뒤집기", "가로 CCW",

  "자동 절전", "절전 화면", "시작 동작", "전원 짧게",
  "새로고침 간격", "햇빛 보정", "앞면 버튼", "측면 버튼",
  "언어",

  "5분", "10분", "15분", "30분", "안 함",
  "어둡게", "밝게", "사용자", "표지", "마지막 쪽",
  "마지막 문서", "홈",
  "무시", "절전", "다음 쪽", "새로고침",
  "이전/다음", "다음/이전",

  "책 캐시 삭제", "블루투스 기기 잊기", "기기 저장소 삭제", "공장 초기화",

  "파일", "파일 없음",
  "앱",
  "절전 중",

  "뒤로", "확인", "취소", "삭제", "켬", "끔", "로드 중...", "오류", "예", "아니오",

  "열기", "실행", "선택", "이동", "전환", "확인", "사용", "사용 안 함",

  "버전", "가동 시간", "배터리", "여유 메모리", "SD 카드", "읽기", "책",
};

// ---------------------------------------------------------------------------
//  Arabic (UTF-8, RTL shaping is handled by the renderer's ArabicShaper)
// ---------------------------------------------------------------------------
static const char* const AR_STRINGS[] = {
  "لا يوجد كتاب مفتوح",
  "اضغط \"الملفات\" للتصفح",
  "< الملفات",
  "القائمة >",
  "الفصل %d من %d",
  "الصفحة %d من %d",
  "%d%% مكتمل",
  "الفصول",
  "الملفات",

  "الإعدادات", "التطبيقات", "خلفية الرئيسية", "النقل اللاسلكي",
  "القارئ", "الجهاز", "بلوتوث", "التنظيف", "معلومات النظام",
  "ظهور التطبيقات",

  "إعدادات القارئ",
  "السمة", "الخط", "حجم الخط", "تخطيط النص", "تباعد الأسطر",
  "كثافة النص", "الوصل", "تنعيم الحواف", "عرض الصور",
  "عرض الجداول", "شريط الحالة", "محاذاة الفقرة", "اتجاه القراءة",
  "بحث عن كلمة", "سجل البحث", "تبديل الإشارة", "عرض الإشارات", "كل الإشارات",

  "صغير جدا", "صغير", "عادي", "كبير",
  "مضغوط", "قياسي", "مريح",
  "ضبط", "يسار", "وسط", "يمين", "نمط الكتاب",
  "داكن", "داكن جدا", "أقصى",
  "إظهار", "نائب", "لا شيء",
  "عمودي", "أفقي CW", "مقلوب", "أفقي CCW",

  "مؤقت النوم", "شاشة النوم", "عند البدء", "زر الطاقة القصير",
  "صفحات لكل تحديث", "تصحيح الشمس", "الأزرار الأمامية", "الأزرار الجانبية",
  "اللغة",

  "5 د", "10 د", "15 د", "30 د", "أبدا",
  "داكن", "فاتح", "مخصص", "الغلاف", "آخر صفحة",
  "آخر مستند", "الرئيسية",
  "تجاهل", "نوم", "تقليب", "تحديث",
  "السابق/التالي", "التالي/السابق",

  "مسح ذاكرة الكتب", "نسيان أجهزة بلوتوث", "مسح التخزين", "إعادة ضبط المصنع",

  "الملفات", "لا توجد ملفات",
  "التطبيقات",
  "نائم",

  "رجوع", "موافق", "إلغاء", "حذف", "تشغيل", "إيقاف", "جار التحميل...", "خطأ", "نعم", "لا",

  "فتح", "تشغيل", "اختيار", "ذهاب", "تبديل", "تأكيد", "تفعيل", "تعطيل",

  "الإصدار", "وقت التشغيل", "البطارية", "الذاكرة الحرة", "بطاقة SD", "القراءة", "الكتب",
};

// ============================================================================
// Language table — index by Language enum
// ============================================================================

static const char* const* const TRANSLATIONS[] = {
  EN_STRINGS,    // EN
  ES_STRINGS,    // ES
  FR_STRINGS,    // FR
  DE_STRINGS,    // DE
  PT_STRINGS,    // PT
  IT_STRINGS,    // IT
  RU_STRINGS,    // RU
  PL_STRINGS,    // PL
  NL_STRINGS,    // NL
  JA_STRINGS,    // JA
  ZH_STRINGS,    // ZH
  KO_STRINGS,    // KO
  AR_STRINGS,    // AR
};

static const char* const LANGUAGE_NAMES[] = {
  "English",
  "Espanol",
  "Francais",
  "Deutsch",
  "Portugues",
  "Italiano",
  "Русский",
  "Polski",
  "Nederlands",
  "日本語",
  "中文",
  "한국어",
  "العربية",
};

// Compile-time checks — every table must be sized to StrId::COUNT.
static_assert(sizeof(EN_STRINGS) / sizeof(EN_STRINGS[0]) == static_cast<int>(StrId::COUNT),
              "EN_STRINGS size mismatch with StrId::COUNT");
static_assert(sizeof(ES_STRINGS) / sizeof(ES_STRINGS[0]) == static_cast<int>(StrId::COUNT),
              "ES_STRINGS size mismatch with StrId::COUNT");
static_assert(sizeof(FR_STRINGS) / sizeof(FR_STRINGS[0]) == static_cast<int>(StrId::COUNT),
              "FR_STRINGS size mismatch with StrId::COUNT");
static_assert(sizeof(DE_STRINGS) / sizeof(DE_STRINGS[0]) == static_cast<int>(StrId::COUNT),
              "DE_STRINGS size mismatch with StrId::COUNT");
static_assert(sizeof(PT_STRINGS) / sizeof(PT_STRINGS[0]) == static_cast<int>(StrId::COUNT),
              "PT_STRINGS size mismatch with StrId::COUNT");
static_assert(sizeof(IT_STRINGS) / sizeof(IT_STRINGS[0]) == static_cast<int>(StrId::COUNT),
              "IT_STRINGS size mismatch with StrId::COUNT");
static_assert(sizeof(RU_STRINGS) / sizeof(RU_STRINGS[0]) == static_cast<int>(StrId::COUNT),
              "RU_STRINGS size mismatch with StrId::COUNT");
static_assert(sizeof(PL_STRINGS) / sizeof(PL_STRINGS[0]) == static_cast<int>(StrId::COUNT),
              "PL_STRINGS size mismatch with StrId::COUNT");
static_assert(sizeof(NL_STRINGS) / sizeof(NL_STRINGS[0]) == static_cast<int>(StrId::COUNT),
              "NL_STRINGS size mismatch with StrId::COUNT");
static_assert(sizeof(JA_STRINGS) / sizeof(JA_STRINGS[0]) == static_cast<int>(StrId::COUNT),
              "JA_STRINGS size mismatch with StrId::COUNT");
static_assert(sizeof(ZH_STRINGS) / sizeof(ZH_STRINGS[0]) == static_cast<int>(StrId::COUNT),
              "ZH_STRINGS size mismatch with StrId::COUNT");
static_assert(sizeof(KO_STRINGS) / sizeof(KO_STRINGS[0]) == static_cast<int>(StrId::COUNT),
              "KO_STRINGS size mismatch with StrId::COUNT");
static_assert(sizeof(AR_STRINGS) / sizeof(AR_STRINGS[0]) == static_cast<int>(StrId::COUNT),
              "AR_STRINGS size mismatch with StrId::COUNT");
static_assert(sizeof(TRANSLATIONS) / sizeof(TRANSLATIONS[0]) == static_cast<int>(Language::COUNT),
              "TRANSLATIONS table size mismatch with Language::COUNT");
static_assert(sizeof(LANGUAGE_NAMES) / sizeof(LANGUAGE_NAMES[0]) == static_cast<int>(Language::COUNT),
              "LANGUAGE_NAMES table size mismatch with Language::COUNT");

// ============================================================================
// English-literal -> StrId lookup for trEn().
// Handles strings that are hardcoded in DEFS arrays, ButtonBar constructors,
// and inline Elements.cpp drawText calls. The match is whole-string exact.
// ============================================================================

namespace {

struct EnMap {
  const char* en;
  StrId id;
};

static const EnMap EN_LOOKUP[] = {
  // Button verbs
  {"Back",    StrId::COMMON_BACK},
  {"OK",      StrId::COMMON_OK},
  {"Cancel",  StrId::COMMON_CANCEL},
  {"Delete",  StrId::COMMON_DELETE},
  {"ON",      StrId::COMMON_ON},
  {"OFF",     StrId::COMMON_OFF},
  {"Yes",     StrId::COMMON_YES},
  {"No",      StrId::COMMON_NO},
  {"Open",    StrId::BTN_OPEN},
  {"Run",     StrId::BTN_RUN},
  {"Select",  StrId::BTN_SELECT},
  {"Go",      StrId::BTN_GO},
  {"Toggle",  StrId::BTN_TOGGLE},
  {"Confirm", StrId::BTN_CONFIRM},
  {"Enable",  StrId::BTN_ENABLE},
  {"Disable", StrId::BTN_DISABLE},

  // Titles / screens
  {"Reader Settings", StrId::READER_SETTINGS_TITLE},
  {"App Visibility",  StrId::APP_VISIBILITY_TITLE},
  {"Files",           StrId::HOME_FILES_TITLE},
  {"Chapters",        StrId::HOME_CHAPTERS_TITLE},
  {"File Browser",    StrId::FILES_TITLE},
  {"Settings",        StrId::SETTINGS_TITLE},
  {"Bluetooth",       StrId::SETTINGS_BLUETOOTH},

  // Reader setting labels
  {"Theme",               StrId::READER_THEME},
  {"Font",                StrId::READER_FONT},
  {"Font Size",           StrId::READER_FONT_SIZE},
  {"Text Layout",         StrId::READER_TEXT_LAYOUT},
  {"Line Spacing",        StrId::READER_LINE_SPACING},
  {"Text Darkness",       StrId::READER_TEXT_DARKNESS},
  {"Hyphenation",         StrId::READER_HYPHENATION},
  {"Anti-Aliasing",       StrId::READER_ANTI_ALIASING},
  {"Text Anti-Aliasing",  StrId::READER_ANTI_ALIASING},
  {"Show Images",         StrId::READER_SHOW_IMAGES},
  {"Show Tables",         StrId::READER_SHOW_TABLES},
  {"Status Bar",          StrId::READER_STATUS_BAR},
  {"Paragraph Alignment", StrId::READER_PARAGRAPH_ALIGNMENT},
  {"Alignment",           StrId::READER_PARAGRAPH_ALIGNMENT},
  {"Reading Orientation", StrId::READER_READING_ORIENTATION},
  {"Look up Word",        StrId::READER_LOOKUP_WORD},
  {"Lookup History",      StrId::READER_LOOKUP_HISTORY},
  {"Toggle Bookmark",     StrId::READER_TOGGLE_BOOKMARK},
  {"View Bookmarks",      StrId::READER_VIEW_BOOKMARKS},
  {"All Bookmarks",       StrId::READER_ALL_BOOKMARKS},

  // Reader value enums
  {"XSmall",        StrId::READER_XSMALL},
  {"Small",         StrId::READER_SMALL},
  {"Normal",        StrId::READER_NORMAL},
  {"Large",         StrId::READER_LARGE},
  {"Compact",       StrId::READER_COMPACT},
  {"Standard",      StrId::READER_STANDARD},
  {"Relaxed",       StrId::READER_RELAXED},
  {"Justified",     StrId::READER_JUSTIFIED},
  {"Left",          StrId::READER_ALIGN_LEFT},
  {"Center",        StrId::READER_ALIGN_CENTER},
  {"Right",         StrId::READER_ALIGN_RIGHT},
  {"Book's Style",  StrId::READER_BOOKS_STYLE},
  {"Dark",          StrId::READER_DARK},
  {"Extra Dark",    StrId::READER_EXTRA_DARK},
  {"Maximum",       StrId::READER_MAXIMUM},
  {"Show",          StrId::READER_SHOW},
  {"Placeholder",   StrId::READER_PLACEHOLDER},
  {"None",          StrId::READER_NONE},
  {"Off",           StrId::READER_NONE},
  {"Portrait",      StrId::READER_PORTRAIT},
  {"Landscape CW",  StrId::READER_LANDSCAPE_CW},
  {"Inverted",      StrId::READER_INVERTED},
  {"Landscape CCW", StrId::READER_LANDSCAPE_CCW},

  // Device setting labels
  {"Auto Sleep Timeout",  StrId::DEVICE_AUTO_SLEEP},
  {"Sleep Screen",        StrId::DEVICE_SLEEP_SCREEN},
  {"Startup Behavior",    StrId::DEVICE_STARTUP},
  {"Short Power Button",  StrId::DEVICE_SHORT_PWR},
  {"Pages Per Refresh",   StrId::DEVICE_PAGES_REFRESH},
  {"Sunlight Fading Fix", StrId::DEVICE_SUNLIGHT_FIX},
  {"Front Buttons",       StrId::DEVICE_FRONT_BTNS},
  {"Side Buttons",        StrId::DEVICE_SIDE_BTNS},
  {"Language",            StrId::DEVICE_LANGUAGE},

  // Device value enums
  {"5 min",         StrId::DEVICE_SLEEP_5},
  {"10 min",        StrId::DEVICE_SLEEP_10},
  {"15 min",        StrId::DEVICE_SLEEP_15},
  {"30 min",        StrId::DEVICE_SLEEP_30},
  {"Never",         StrId::DEVICE_NEVER},
  {"Light",         StrId::DEVICE_LIGHT},
  {"Custom",        StrId::DEVICE_CUSTOM},
  {"Cover",         StrId::DEVICE_COVER},
  {"Last Page",     StrId::DEVICE_LAST_PAGE},
  {"Last Document", StrId::DEVICE_LAST_DOC},
  {"Home",          StrId::DEVICE_HOME},
  {"Ignore",        StrId::DEVICE_IGNORE},
  {"Sleep",         StrId::DEVICE_SLEEP},
  {"Page Turn",     StrId::DEVICE_PAGE_TURN},
  {"Refresh",       StrId::DEVICE_REFRESH},
  {"Prev/Next",     StrId::DEVICE_PREV_NEXT},
  {"Next/Prev",     StrId::DEVICE_NEXT_PREV},

  // Cleanup items
  {"Clear Book Cache",         StrId::CLEANUP_CLEAR_CACHE},
  {"Forget Bluetooth Devices", StrId::CLEANUP_FORGET_BT},
  {"Clear Device Storage",     StrId::CLEANUP_CLEAR_STORAGE},
  {"Factory Reset",            StrId::CLEANUP_FACTORY_RESET},

  // System info field names
  {"Version",      StrId::INFO_VERSION},
  {"Uptime",       StrId::INFO_UPTIME},
  {"Battery",      StrId::INFO_BATTERY},
  {"Free Memory",  StrId::INFO_FREE_MEMORY},
  {"SD Card",      StrId::INFO_SD_CARD},
  {"Reading",      StrId::INFO_READING},
  {"Books",        StrId::INFO_BOOKS},
};

}  // namespace

// ============================================================================
// I18n implementation
// ============================================================================

I18n& I18n::instance() {
  static I18n inst;
  return inst;
}

const char* I18n::get(StrId id) const {
  const uint16_t idx = static_cast<uint16_t>(id);
  if (idx >= static_cast<uint16_t>(StrId::COUNT)) {
    return "???";
  }

  // Try current language first
  const uint8_t langIdx = static_cast<uint8_t>(lang_);
  if (langIdx < static_cast<uint8_t>(Language::COUNT)) {
    const char* const* table = TRANSLATIONS[langIdx];
    if (table != nullptr) {
      const char* str = table[idx];
      if (str != nullptr) {
        return str;
      }
    }
  }

  // Fall back to English
  return EN_STRINGS[idx];
}

const char* I18n::trEn(const char* en) const {
  if (en == nullptr || en[0] == '\0') return en;
  // Two-byte prefilter saves most strcmps. Still O(N), but N is small
  // (about 100), the table fits in a cache line or two, and this runs
  // only during redraw, not per frame.
  for (const auto& m : EN_LOOKUP) {
    if (m.en[0] == en[0] && std::strcmp(m.en, en) == 0) {
      return get(m.id);
    }
  }
  return en;  // unknown literal -> return as-is
}

void I18n::setLanguage(Language lang) {
  if (static_cast<uint8_t>(lang) < static_cast<uint8_t>(Language::COUNT)) {
    lang_ = lang;
  }
}

const char* I18n::languageName(Language lang) const {
  const uint8_t idx = static_cast<uint8_t>(lang);
  if (idx < static_cast<uint8_t>(Language::COUNT)) {
    return LANGUAGE_NAMES[idx];
  }
  return "???";
}

}  // namespace sumi
