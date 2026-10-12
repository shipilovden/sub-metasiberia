# Text editor and font checks

Code and offline check snapshot: 2026-10-12.

## Content editing

`ObjectEditor::setFromObject()` previously recreated the content document for
every authoritative object refresh, including the echo of each typed character.
`EditorTextSync.h` now leaves an unchanged document alone. Cursor, selection,
scroll and local undo therefore survive these echoes. A changed value for the
same object retains numeric cursor/selection positions, clamped to the new text;
selecting a different object starts a fresh document. The same helper covers the
inline script field and specialised editor content refreshes.

`scripts/test_editor_text_sync.cpp` is a standalone Qt widget check. It passed
with Qt 5.15 offscreen for QTextEdit and QPlainTextEdit, covering insertion in the
middle, unchanged echo, undo, backward selection, actual remote replacement,
shortening and switching objects. It does not launch the world client.

## Font coverage

`scripts/test_world_text_fonts.cpp` audits every packaged font using FreeType.
On 2026-10-12 it was linked against the existing FreeType object files from the
Qt client build, rather than another system FreeType implementation. Both
`resources/fonts` and the canonical RelWithDebInfo `data/resources/fonts`
contained 326 files (325 WOFF, one TTF):

- 326 loaded and rendered their supported sample glyphs successfully;
- source and packaged files matched byte for byte;
- 59 fonts lacked at least one character in the combined Latin alphabet,
  Russian alphabet including Ё/ё and decimal-digit sample.

Missing characters are a font's coverage limitation, not a failed font file.
ObjectEditor now displays the missing characters from the current text below
the font selector. It keeps the selected font; it does not silently substitute
another design. A font referenced by an object but absent from this client is
also identified, and editing content preserves that font name.

These checks establish file loading, sample rasterisation and editor behaviour.
They do not claim every Unicode character exists in every decorative font, or
verify a live multiplayer editing session.

## Compatible font selection (2026-10-12)

The Text editor now defaults to **Supports my text / Поддерживает мой текст**.
It filters on the actual codepoints in Content, including punctuation and
spaces, using the same FreeType library as rendering. CR/LF are layout controls.
The current selection always survives filtering and remote echoes. An
incompatible selected row is explicitly marked **selected; incomplete**; its
canonical font name is unchanged, and filtering does not emit an object edit.

**Choose a compatible font / Выбрать совместимый шрифт** is an explicit repair
action: it chooses Default if that exact native face supports the text, or a
packaged font with complete coverage otherwise. The normal font-change signal
applies the choice through the editor's existing update/undo path. On failure it
leaves the object unchanged and reports that no installed font covers the text.
It is disabled for objects without edit permission or unsupported server font
capabilities. The filter and repair controls belong only to Text objects.

Coverage is cached by font file, with FreeType faces closed after discovery.
Filtering is debounced by 200 ms; initial catalogue loading yields to the event
loop after an 8 ms budget rather than loading all fonts on every keystroke.
This is a selection repair, not manufactured missing glyphs or an implicit
mixed-font rendering fallback. A font containing a character still might only
offer a bitmap glyph, which cannot be extruded as an outline mesh.

`get_text_object_info` also reports `font_coverage` with availability,
`supports_all_characters`, `missing_characters`, and an optional
`compatible_font`. The AI can diagnose a missing-letter problem before trying
to convert the text to a mesh; it must explain a font/style change. Conversion
continues to reject missing or non-outline glyphs rather than silently dropping
letters.

Offline `scripts/test_font_coverage.cpp`, linked against the client FreeType and
zlib objects, passed: all 326 packaged fonts loaded; 284 supported the tested
Russian phrase including Ё and digits, and 310 the tested Latin phrase. The
different totals from the full alphabet audit above reflect different samples.
The check also covered cache reuse, missing files, CR/LF, and a deliberately
unsupported Unicode scalar. A cold full catalogue scan took 328 ms before UI
chunking. Full widget interaction and world/server round trips remain separate
verification steps.
