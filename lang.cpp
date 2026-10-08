// esp32-S3-ws4-caravan v1.0
/* Language: English (the text in the code) or a built-in translation.
 *
 * Translations are gettext .po files in lang/ (lang/sv.po ...), edited with
 * Poedit. tools/i18n.py turns them into lang_tables.h, which is compiled in:
 * no SD card is involved. Fuzzy and untranslated entries are left out, so they
 * show in English.
 *
 * tr() looks the English text up and returns the translation, or the English
 * text itself. Leading spaces, line breaks and LVGL symbols are not part of the
 * text looked up: "\n" LV_SYMBOL_OK "  Save" is looked up as "Save". The table
 * is sorted once at start-up and then only read, so tr() is safe from any task.
 *
 * The language is chosen under Settings -> Device -> Language. Changing it saves
 * the choice and restarts the board, so every screen is built in one language.
 */
#include "app.h"

char ui_lang[LANG_CODE_LEN] = "en";
bool ui_translated = false;

struct TrPair {
  const char *en, *tr;
};

/* ---------- built-in languages (generated from lang/<code>.po) ---------- */
struct BuiltIn {
  const char *code, *name;
  const TrPair *table;
  int count;
};
#include "lang_tables.h"  // defines builtin[] and BUILTIN_COUNT

/* ---------- the active table ---------- */
static const TrPair *table = NULL;
static int table_count = 0;
static int *order = NULL;  // table indexes sorted by English text

static int cmp_idx(const void *a, const void *b) {
  return strcmp(table[*(const int *)a].en, table[*(const int *)b].en);
}

static void use_table(const TrPair *t, int n) {
  order = (int *)heap_caps_malloc(n * sizeof(int), MALLOC_CAP_SPIRAM);
  if (!order) order = (int *)malloc(n * sizeof(int));
  if (!order) return;
  for (int i = 0; i < n; i++) order[i] = i;
  table = t;
  qsort(order, n, sizeof(int), cmp_idx);
  table_count = n;
  ui_translated = true;
}

static const char *lookup(const char *en) {
  int lo = 0, hi = table_count - 1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    int c = strcmp(en, table[order[mid]].en);
    if (!c) return table[order[mid]].tr;
    if (c < 0) hi = mid - 1;
    else lo = mid + 1;
  }
  return NULL;
}

/* Spaces, line breaks and LVGL symbols (U+F000..U+F8FF: EF 80..A3 xx) at the start */
static size_t prefix_len(const char *s) {
  size_t i = 0;
  for (;;) {
    unsigned char c = s[i];
    if (c == ' ' || c == '\n') i++;
    else if (c == 0xEF && (unsigned char)s[i + 1] >= 0x80 && (unsigned char)s[i + 1] <= 0xA3 && s[i + 2]) i += 3;
    else return i;
  }
}

const char *tr(const char *en) {
  if (!ui_translated || !en || !en[0]) return en;
  size_t p = prefix_len(en);
  if (!en[p]) return en;
  const char *t = lookup(en + p);
  if (!t) return en;
  if (!p) return t;
  /* put the prefix back; a few rotating buffers, as the result is used at once */
  static char ring[8][200];
  static volatile uint32_t next = 0;
  char *out = ring[__atomic_fetch_add(&next, 1, __ATOMIC_RELAXED) % 8];
  size_t n = p < sizeof(ring[0]) - 1 ? p : sizeof(ring[0]) - 1;
  memcpy(out, en, n);
  strlcpy(out + n, t, sizeof(ring[0]) - n);
  return out;
}

void lang_begin() {
  if (!strcmp(ui_lang, "en")) return;
  for (int i = 0; i < BUILTIN_COUNT; i++)
    if (!strcmp(ui_lang, builtin[i].code)) {
      use_table(builtin[i].table, builtin[i].count);
      return;
    }
  log_fault("Language: %s is not built in - using English", ui_lang);
  strlcpy(ui_lang, "en", sizeof(ui_lang));
}

/* English plus the built-in languages; names are "\n"-separated for a dropdown */
int lang_list(char *codes, int max, char *names, size_t names_len) {
  int n = 0;
  strlcpy(codes, "en", LANG_CODE_LEN);
  strlcpy(names, "English", names_len);
  n++;
  for (int i = 0; i < BUILTIN_COUNT && n < max; i++, n++) {
    strlcpy(codes + n * LANG_CODE_LEN, builtin[i].code, LANG_CODE_LEN);
    strlcat(names, "\n", names_len);
    strlcat(names, builtin[i].name, names_len);
  }
  return n;
}

/* ---------- fonts ---------- */
#if LV_FONT_MONTSERRAT_14
LV_FONT_DECLARE(font_latin1_14)
#endif
#if LV_FONT_MONTSERRAT_16
LV_FONT_DECLARE(font_latin1_16)
#endif
#if LV_FONT_MONTSERRAT_20
LV_FONT_DECLARE(font_latin1_20)
#endif
#if LV_FONT_MONTSERRAT_28
LV_FONT_DECLARE(font_latin1_28)
#endif
#if LV_FONT_MONTSERRAT_32
LV_FONT_DECLARE(font_latin1_32)
#endif
#if LV_FONT_MONTSERRAT_48
LV_FONT_DECLARE(font_latin1_48)
#endif

const lv_font_t *ui_font(const lv_font_t *f) {
  if (!ui_translated) return f;  // English: the built-in fonts, exactly as before
#if LV_FONT_MONTSERRAT_14
  if (f == &lv_font_montserrat_14) return &font_latin1_14;
#endif
#if LV_FONT_MONTSERRAT_16
  if (f == &lv_font_montserrat_16) return &font_latin1_16;
#endif
#if LV_FONT_MONTSERRAT_20
  if (f == &lv_font_montserrat_20) return &font_latin1_20;
#endif
#if LV_FONT_MONTSERRAT_28
  if (f == &lv_font_montserrat_28) return &font_latin1_28;
#endif
#if LV_FONT_MONTSERRAT_32
  if (f == &lv_font_montserrat_32) return &font_latin1_32;
#endif
#if LV_FONT_MONTSERRAT_48
  if (f == &lv_font_montserrat_48) return &font_latin1_48;
#endif
  return f;  // a size without a Latin-1 version: accented letters show as boxes
}

void ui_font_apply(lv_obj_t *scr) {
  if (ui_translated && scr) lv_obj_set_style_text_font(scr, FONT_UI, 0);
}

/* ---------- dates ---------- */
static const char *const wd_en[] = { N_("Sunday"), N_("Monday"), N_("Tuesday"), N_("Wednesday"),
                                     N_("Thursday"), N_("Friday"), N_("Saturday") };
static const char *const mon_en[] = { N_("January"), N_("February"), N_("March"), N_("April"),
                                      N_("May"), N_("June"), N_("July"), N_("August"),
                                      N_("September"), N_("October"), N_("November"), N_("December") };
static const char *const day_en[] = { N_("Mo"), N_("Tu"), N_("We"), N_("Th"), N_("Fr"), N_("Sa"), N_("Su") };
/* the weather forecast shows strftime's "%a" names, translated where they are shown */
static const char *const fc_days[] __attribute__((unused)) = { N_("Mon"), N_("Tue"), N_("Wed"), N_("Thu"),
                                                               N_("Fri"), N_("Sat"), N_("Sun") };

const char *tr_weekday(int wday) {
  return wday >= 0 && wday <= 6 ? tr(wd_en[wday]) : "";
}

const char *tr_month(int mon) {
  return mon >= 0 && mon <= 11 ? tr(mon_en[mon]) : "";
}

const char *tr_day_short(int i) {
  return i >= 0 && i <= 6 ? tr(day_en[i]) : "";
}
