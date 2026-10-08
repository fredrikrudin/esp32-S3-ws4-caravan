<!-- esp32-S3-ws4-caravan v1.0 -->
# Translations

The display texts are written in English in the code. Translations are gettext
`.po` files, one per language, edited with [Poedit](https://poedit.net/) (free).
They are compiled into the firmware: no SD card is involved.

| File | What it is |
|---|---|
| `caravan.pot` | The template: every text on the display. Generated from the code, don't edit it |
| `sv.po` | Swedish |
| `../lang_tables.h` | All `.po` files as C tables, generated. This is what gets compiled in |

`tools/i18n.py` needs Python 3 and nothing else. Run it from the sketch folder.

## Add a language

1. In Poedit: **File → New from POT/PO file…**, choose `lang/caravan.pot`, and pick the
   language.
2. Translate. Keep the placeholders (`%s`, `%d`, `%.1f`, `%%`, ...) the same and in the
   same order. Poedit warns when they differ, and `i18n.py` leaves such a text out
   (it then shows in English).
3. Save it in this folder as `<code>.po`, e.g. `de.po`. The name of the file is the code.
4. Run `python3 tools/i18n.py build`. It lists every language and how many texts are
   translated.
5. Upload the sketch. The language appears under Settings → Device → Language.

The name shown in the Language list comes from a header line `X-Language-Name:` in the
`.po` file (Poedit keeps it). Without it, a built-in list of common languages is used,
then the code.

The fonts have the Latin-1 letters, which cover Swedish, Danish, Norwegian, Finnish,
Icelandic, German, Dutch, French, Spanish, Italian and Portuguese. Other letters show as
boxes. They can be added in `tools/gen_font_latin1.py` (needs Pillow and the Montserrat
Medium TTF).

## Change a translation

Open the `.po` in Poedit, edit and save, then run `python3 tools/i18n.py build` and
upload the sketch. Texts marked "Needs work" (fuzzy) and empty ones show in English.

## After changing texts in the code

1. `python3 tools/i18n.py` writes a new `caravan.pot` and updates every `.po` from it:
   new texts are added untranslated, texts that left the code are kept at the end as
   obsolete. (Poedit's **Translation → Update from POT File…** does the same.)
2. In Poedit, open each `.po` and translate what's new.
3. `python3 tools/i18n.py build`, then upload.

Texts reach the translation table when they are given to one of the LVGL calls that
translate (`lv_label_set_text`, `make_btn`, `make_section`, `set_label`, ...), wrapped in
`TR("...")` (for `snprintf` formats and button maps), or marked `N_("...")` (in tables of
names that are translated where they are shown). Leading spaces, line breaks and LVGL
symbols are left out of what is translated.
