# SPDX-License-Identifier: LGPL-3.0-or-later
"""Synthetic fixtures only; no proprietary LawnStrings content.

Run: python3 -m unittest discover -s tools -p 'test_import_zh_locale.py' -v
"""
import contextlib
import io
from pathlib import Path
import tempfile
import unittest

from import_zh_locale import ImportFailure, load_table, main, parse_table, serialize, validate


class ParserTests(unittest.TestCase):
    def test_runtime_normalization_multiline_and_bom(self):
        text = "\ufeff \r\n[ mixed_key ]\r\n 中文\r\n  第二行 \r\n[EMPTY]\n"
        table = parse_table(text)
        self.assertEqual(table, {"MIXED_KEY": "中文\n  第二行", "EMPTY": ""})
        self.assertEqual(parse_table(serialize(table)), table)

    def test_runtime_preserves_non_ascii_outer_whitespace(self):
        self.assertEqual(parse_table("[A]\n\u3000中文\u3000\n")["A"], "\u3000中文\u3000")

    def test_duplicate_case_normalized_keys_rejected(self):
        with self.assertRaisesRegex(ImportFailure, "duplicate"):
            parse_table("[key]\nfirst\n[KEY]\nsecond")

    def test_malformed_tables_rejected(self):
        for text in ("", "\n", "not a table", "preamble[A]x", "[]x", "[BAD[NEXT]x", "[A]x\0", "[A]x[UNCLOSED", "[A-B]x"):
            with self.subTest(text=text), self.assertRaises(ImportFailure):
                parse_table(text)

    def test_fake_ini_options_rejected(self):
        with self.assertRaisesRegex(ImportFailure, "INI"):
            parse_table("[OPTIONS]\nMUSIC: 音乐\nSOUND: 音效")

    def test_delimiter_anywhere_not_only_lines(self):
        self.assertEqual(parse_table("[A]first[B]second"), {"A": "first", "B": "second"})


class ValidationTests(unittest.TestCase):
    def test_missing_unknown_and_unchanged_are_explicit(self):
        overlay, report = validate({"A": "old", "B": "same", "C": "lost"},
                                   {"A": "新", "B": "same", "FOREIGN": "外"})
        self.assertEqual(overlay, {"A": "新"})
        self.assertEqual(report["missing_keys"], ["C"])
        self.assertEqual(report["unknown_keys_dropped"], ["FOREIGN"])
        self.assertEqual(report["untranslated_or_missing_keys"], ["B", "C"])
        self.assertTrue(report["errors"])
        self.assertFalse(validate({"A": "old"}, {"A": "新", "X": "外"}, True)[1]["errors"])

    def test_placeholders_move_but_counts_are_preserved(self):
        self.assertFalse(validate({"A": "{SYNTHETIC} hello"}, {"A": "你好 {SYNTHETIC}"})[1]["errors"])
        for value in ("你好", "你好 {OTHER}", "{SYNTHETIC}{SYNTHETIC}"):
            self.assertEqual(validate({"A": "{SYNTHETIC} hello"}, {"A": value})[1]["format_mismatch_keys"], ["A"])

    def test_control_order_and_printf_types_preserved(self):
        english = {"A": "{KEYWORD}x{STAT}%s %d %%"}
        self.assertFalse(validate(english, {"A": "{KEYWORD}字{STAT}%s %d %%"})[1]["errors"])
        for value in ("{STAT}字{KEYWORD}%s %d %%", "{KEYWORD}字{STAT}%d %s %%", "{KEYWORD}字{STAT}%s %s %%"):
            self.assertEqual(validate(english, {"A": value})[1]["format_mismatch_keys"], ["A"])

    def test_empty_translation_does_not_hide_english(self):
        self.assertEqual(validate({"A": "text"}, {"A": ""})[1]["empty_translation_keys"], ["A"])


class CliTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.en = self.root / "english.txt"
        self.zh = self.root / "translation.txt"
        self.en.write_text("[A]\nsynthetic {ARG}\n[B]\nsame\n", encoding="utf-8")
        self.zh.write_text("[A]\n合成 {ARG}\n", encoding="utf-8")

    def run_cli(self, *extra):
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            return main([str(self.zh), "--english", str(self.en), *map(str, extra)])

    def rights(self):
        return ["--acknowledge-rights", "--source", "Synthetic test author", "--permission", "Original test fixture"]

    def test_dry_run_and_complete_mode(self):
        self.assertEqual(self.run_cli(), 0)
        self.assertEqual(self.run_cli("--require-complete"), 1)

    def test_output_requires_provenance_and_never_overwrites(self):
        output = self.root / "overlay.txt"
        with self.assertRaises(SystemExit):
            self.run_cli("--output", output)
        self.assertEqual(self.run_cli("--output", output, *self.rights()), 0)
        self.assertEqual(parse_table(output.read_text(encoding="utf-8")), {"A": "合成 {ARG}"})
        with self.assertRaises(SystemExit):
            self.run_cli("--output", output, *self.rights())
        with self.assertRaises(SystemExit):
            self.run_cli("--output", self.en, *self.rights())

    def test_invalid_translation_never_creates_output(self):
        self.zh.write_text("[A]\nmissing token", encoding="utf-8")
        output = self.root / "overlay.txt"
        report = self.root / "audit.json"
        self.assertEqual(self.run_cli("--output", output, "--report", report, *self.rights()), 1)
        self.assertFalse(output.exists())
        self.assertIn('"format_mismatch_keys": [', report.read_text(encoding="utf-8"))

    def test_explicit_encodings_and_executable_rejection(self):
        for encoding in ("utf-8-sig", "utf-16", "gb18030"):
            self.zh.write_bytes("[A]\n中文 {ARG}".encode(encoding))
            self.assertEqual(load_table(self.zh, encoding)[0], {"A": "中文 {ARG}"})
        binary = self.root / "patch.exe"
        binary.write_bytes(b"MZnot a real executable")
        with self.assertRaisesRegex(ImportFailure, "standalone"):
            load_table(binary, "utf-8-sig")


if __name__ == "__main__":
    unittest.main()
