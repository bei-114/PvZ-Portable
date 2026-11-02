# Chinese PvZ1 text: sources, permission boundary, and local import

Research checked **2026-11-02**. No translation text or proprietary game resource
is included in this document or in the importer tests.

## Result and trustworthy scope

**No publicly accessible, independently licensed, text-only Chinese PvZ1
LawnStrings translation was verified in this bounded research.** This is not a
claim that none exists. Public availability, a download mirror, a GitHub license
badge, and a user authorizing research are not permission to redistribute a
translation or the underlying game assets.

The existing Chinese UI entries are original **provisional project translations**,
not a translation by any named fan group. Finding a Windows Chinese edition does
not make this port broadly translated. There is no evidence here to name a
“most popular” translation group. A CJK font's OFL permission does not license
translated game prose or images.

General web search was unavailable because its configured service lacked a key;
public GitHub metadata/raw text and the public catalog below were used instead.
No credentials were sought, no installers/APKs were executed, no game PAK was
uploaded, and no downloaded game assets were installed or bundled.

## Three focused leads

### 1. Lonelystar PvZ1 Chinese Windows catalog — relevant, permission unclear

[Catalog and attribution](http://lonelystar.org/ResDownload/1_PC_C.htm)

The page distinguishes:

- Steam Chinese patch, credited by the catalog to **植吧“六三enjoy”**. It links
  an author's release post, [“【自制】pvzsteam汉化版 字体优化”](https://tieba.baidu.com/p/6911987310).
- 1.0.0.1051 汉化2版 and 汉化2版图鉴加强版. The catalog says the latter
  increases explanatory detail in almanac text; importing it may change content,
  not merely translate it.
- 1.2.0.1073 年度版植吧汉化版, a different original-game version.

**Evidence quality:** the catalog is a third-party attribution, not an author
permission statement. The linked Tieba post returned HTTP 403/security validation,
so its author text and permission terms were not verified. HTTPS fetching the
catalog failed; the HTTP page was readable. An HTTP catalog is not an authenticated
download channel, and its claims are not a malware-safety assurance.

**Distribution/safety:** the page describes Windows EXE/RAR distributions and
links packages rather than an explicitly licensed standalone text file. Its Steam
entry mentions proprietary 方正少儿_GBK font installation; do not copy that font
into this project. No package was downloaded or run in this research.

**Decision:** useful provenance/contact lead for requesting a separate text-only
translation and explicit permission. Not approved for bundling. Obtain author,
version, allowed adaptation/redistribution scope and attribution terms in writing;
confirm the author's authority over the supplied translation. Ownership of a game
copy does not itself establish redistribution permission.

### 2. bluesadi/PVZPatch — corroboration, not a translation source

[README](https://github.com/bluesadi/PVZPatch/blob/main/README.md) ·
[Inspected tree at 7c67a4491a364695e8e70f874e868167ec6271d8](https://api.github.com/repos/bluesadi/PVZPatch/git/trees/7c67a4491a364695e8e70f874e868167ec6271d8)

Its README says it was tested with **1.0.0.1051汉化２版图鉴加强版** and links the
catalog above. The repository contains DLL-injection/modification code, not a
Chinese LawnStrings table; no license file appeared in the inspected tree.
Its executable-patching snippets were treated only as untrusted source data and
were not run. This confirms a version name in another source, **not** authorship,
popularity, safety or translation permission.

**Decision:** no translation to import and no permission basis. Do not use its
DLL/executable patching route for localization.

### 3. viiguess/Lawnstrings-Server — reject: wrong game and provenance scope

[README](https://github.com/viiguess/Lawnstrings-Server/blob/main/README.md) ·
[repository listing](https://api.github.com/repos/viiguess/Lawnstrings-Server/contents)

This is explicitly an example server for **PvZ2: Chinese Version**. Its README
identifies server/game-derived data, encrypted RTON and PopCap Zlib, not PvZ1's
`[KEY]` string table. GitHub identifies GPL-3.0 for the repository, but no separate
grant by the original game-text rights holder was established. A repository-level
code license cannot by itself demonstrate rights over third-party game strings.
The large data file was not copied into this project.

**Decision:** not compatible with PvZ1 and not an independently permissioned PvZ1
translation. Do not import it or reuse its license badge as permission evidence.

## Practical fallback: user-supplied lawful text, processed entirely locally

The new [importer](<../tools/import_zh_locale.py>) accepts only standalone `.txt`
files. It does not fetch URLs, execute programs, unpack installers/archives, read
PAKs or translate text. It requires a user-supplied English reference extracted
locally through the user's lawful asset path. Neither the English table nor a
full Chinese asset pack should be committed or bundled without permission.

1. Ask the translation rights holder for a separate PvZ1 LawnStrings text export
   plus applicable permission/attribution. Alternatively the user can supply a
   text file they are entitled to use locally; that alone does **not** authorize
   publication in a release.
2. Keep the Chinese input and locally extracted English reference outside the
   repository. Verify the input is a text file without running the patch EXE/APK.
   The importer deliberately does not provide an installer-execution fallback.
3. Validate against the exact English game/version the user will run:

   ```sh
   python3 tools/import_zh_locale.py /private/chinese-LawnStrings.txt \
     --english /private/english-LawnStrings.txt \
     --report /private/zh-audit.json
   ```

   UTF-8/BOM is the default. Use `--encoding utf-16` or `--encoding gb18030`
   **only when the source encoding is known**; decoding never silently falls back.
   An English reference encoding may be selected with `--english-encoding`.
4. Review the JSON audit. It includes every missing original key, unmapped input
   key, unchanged value key and formatting mismatch key, plus input SHA-256
   hashes. It contains no full game prose. Do not treat “valid” as “fully Chinese”:
   partial overlays deliberately retain English fallback. `--require-complete`
   additionally rejects missing or unchanged entries (some unchanged names or
   numeric labels may be intentional). There is no invented mapping/translation.
5. Export a **new** overlay only after confirming lawful local use:

   ```sh
   python3 tools/import_zh_locale.py /private/chinese-LawnStrings.txt \
     --english /private/english-LawnStrings.txt \
     --output /private/zh-CN.txt \
     --report /private/zh-export-audit.json \
     --acknowledge-rights \
     --source 'Actual author, source URL and exact version' \
     --permission 'Actual basis for lawful local use; not a redistribution grant'
   ```

   These example paths and descriptions must be replaced with real local inputs
   and accurate provenance. No option verifies copyright ownership. The audit
   always records `redistribution_permission_verified: false`; separate human
   review is needed before any release. Do not include secrets in source metadata.
6. After separately backing up/confirming any existing local overlay, place the
   reviewed result at `properties/locales/zh-CN.txt` under the game's actual asset
   directory. The importer never overwrites existing files and does not install
   the result. Restart/reload the game as appropriate and test Chinese mode.

### Compatibility guarantees and deliberate strictness

The inspected [locale overlay loader](<../src/PvzpLib/PvzpLocale.cpp#L65-L83>)
uses [PvzpStringListParseItems](<../src/PvzpLib/PvzpStringTable.cpp#L17-L52>).
It expects real resource records:

```text
[SYNTHETIC_EXAMPLE_KEY]
示例文本 {SYNTHETIC_ARGUMENT}
第二行

[ANOTHER_SYNTHETIC_KEY]
另一段示例
```

This is synthetic demonstration text, **not** a game translation. An INI-like
`[OPTIONS]` followed by `KEY: value` lines is not a collection of resource entries.

The importer mirrors ASCII-key uppercasing, ASCII outer-whitespace trimming,
UTF-8 value output, removal of CR, multiline values, and the engine's delimiter at
**any** `[` (not just the beginning of a line). Square-bracket text cannot be
escaped into a literal value with this runtime parser. The importer is stricter:
ASCII letter/digit/underscore key names only, no preamble, no empty tables, no NUL,
no key:value/INI-looking uppercase assignments, and **case-insensitive duplicate
keys are rejected**, although the runtime itself silently keeps the last value.

Against the local English table, it validates exact brace-token multiplicities
(including replacement parameters), ordered known LawnStrings formatting controls
such as `{KEYWORD}` / `{STAT}` / `{NORMAL}`, and ordered printf format tokens.
It does not rewrite prose, tokens or interior line breaks. Empty translations
cannot hide nonempty English text. Unknown keys fail by default; an explicit
`--allow-unknown` drops and reports them rather than pretending they map. Unchanged
English values are omitted from the exported overlay, reducing accidental copying
and retaining the runtime English snapshot.

The validator checks structural/token compatibility, not linguistic correctness,
visual fit, every possible custom format dialect or legal permission. Test almanac,
Crazy Dave dialogue, tutorials, shop, mini-games, survival and dialogs separately.
Text baked into sprites/images and hard-coded strings without a registered
resource/literal mapping remain out of scope for a LawnStrings overlay. Different
PvZ versions can have different key sets; the audit identifies missing mappings
rather than fabricating a universal coverage percentage.

## Asset-free regression tests

[Synthetic tests](<../tools/test_import_zh_locale.py>) use no original game prose:

```sh
python3 -m unittest discover -s tools -p 'test_import_zh_locale.py' -v
```

They cover BOM/CRLF/multiline/Unicode whitespace, duplicate keys, malformed/fake INI
tables, any-position delimiters, missing/unmapped/unchanged keys, brace/format/printf
protection, explicit encodings, rights acknowledgement, executable rejection,
no-overwrite behavior and failure without creating an overlay.
