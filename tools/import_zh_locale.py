#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-3.0-or-later
"""Validate a legally supplied PvZ1 text table and emit a local UTF-8 overlay.

No network, executables, archives, PAK parsing, translation, or asset bundling.
The English reference must be extracted locally from the user's legal assets.
"""
from __future__ import annotations

import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import sys

WS = " \r\n\t\v\f"
KEY = re.compile(r"[A-Za-z0-9_]+\Z")
BRACE = re.compile(r"\{[^{}\n]*\}")
PRINTF = re.compile(r"%(?:\d+\$)?[-+#0 ']*(?:\d+|\*)?(?:\.(?:\d+|\*))?(?:hh|ll|[hljztL])?[diuoxXfFeEgGaAcspn%]")
CONTROLS = {
    "NORMAL", "FLAVOR", "KEYWORD", "NOCTURNAL", "AQUATIC", "STAT",
    "METAL", "KEYMETAL", "SHORTLINE", "EXTRASHORTLINE", "CREDITS1", "CREDITS2",
}
MAX_BYTES = 16 * 1024 * 1024


class ImportFailure(ValueError):
    pass


def parse_table(text: str) -> dict[str, str]:
    """Safe subset of PvzpStringListParseItems: reject ambiguous/duplicate input.

    Match its ASCII uppercase/trim, CR removal and ANY-'[' delimiter behavior.
    Unlike the runtime, do not ignore preambles or silently accept duplicate keys.
    Header names are restricted to the ordinary resource-key ASCII alphabet.
    """
    if "\0" in text:
        raise ImportFailure("NUL byte in text (binary or wrong encoding)")
    text = text.removeprefix("\ufeff")
    result: dict[str, str] = {}
    pos = 0
    while True:
        begin = text.find("[", pos)
        if begin < 0:
            if text[pos:].strip(WS):
                raise ImportFailure("text without a [KEY] header")
            break
        if text[pos:begin].strip(WS):
            raise ImportFailure("non-whitespace before a [KEY] header")
        end = text.find("]", begin + 1)
        if end < 0:
            raise ImportFailure("unclosed [KEY] header")
        name = text[begin + 1:end].strip(WS)
        if not KEY.fullmatch(name):
            raise ImportFailure("invalid resource key; expected ASCII letters/digits/underscore")
        key = name.upper()
        if key in result:
            raise ImportFailure(f"duplicate resource key: {key}")
        next_header = text.find("[", end + 1)
        value = text[end + 1:next_header if next_header >= 0 else len(text)]
        value = value.strip(WS).replace("\r", "")
        # Do not mistake an INI options section for a whole LawnStrings table.
        if re.search(r"(?m)^\s*[A-Z_][A-Z0-9_]+\s*[:=]", value):
            raise ImportFailure(f"{key}: INI/key:value-looking content; supply [KEY] / value records")
        result[key] = value
        pos = next_header if next_header >= 0 else len(text)
    if not result:
        raise ImportFailure("empty string table")
    return result


def load_table(path: Path, encoding: str) -> tuple[dict[str, str], str]:
    if path.suffix.lower() != ".txt":
        raise ImportFailure("only standalone .txt inputs are accepted; never run patch installers")
    with path.open("rb") as handle:
        data = handle.read(MAX_BYTES + 1)
    if len(data) > MAX_BYTES:
        raise ImportFailure("text input exceeds 16 MiB limit")
    try:
        text = data.decode(encoding, errors="strict")
    except UnicodeError as exc:
        raise ImportFailure(f"cannot decode text as {encoding}; select the correct encoding explicitly") from exc
    return parse_table(text), hashlib.sha256(data).hexdigest()


def signature(value: str) -> tuple[Counter, list[str], list[str]]:
    braces = BRACE.findall(value)
    controls = [token for token in braces if token[1:-1] in CONTROLS]
    # Printf argument order remains strict: ordinary %s/%d arguments cannot move.
    return Counter(braces), controls, PRINTF.findall(value)


def validate(english: dict[str, str], chinese: dict[str, str],
             allow_unknown: bool = False) -> tuple[dict[str, str], dict]:
    common = english.keys() & chinese.keys()
    unknown = sorted(chinese.keys() - english.keys())
    missing = sorted(english.keys() - chinese.keys())
    unchanged = sorted(key for key in common if chinese[key] == english[key])
    empty = sorted(key for key in common if english[key] and not chinese[key])
    mismatches = sorted(key for key in common if signature(english[key]) != signature(chinese[key]))
    output = {key: value for key, value in chinese.items()
              if key in english and value != english[key]}
    report = {
        "english_key_count": len(english), "input_key_count": len(chinese),
        "overlay_key_count": len(output), "missing_keys": missing,
        "unknown_keys_dropped": unknown, "unchanged_keys_omitted": unchanged,
        "empty_translation_keys": empty, "format_mismatch_keys": mismatches,
        "untranslated_or_missing_keys": sorted(set(missing) | set(unchanged)),
        "errors": [],
    }
    if unknown and not allow_unknown:
        report["errors"].append("unknown keys: wrong game/version or mapping required; --allow-unknown drops them")
    if empty:
        report["errors"].append("empty translations would hide nonempty English strings")
    if mismatches:
        report["errors"].append("brace placeholders/control order or printf formatting differs")
    return output, report


def serialize(table: dict[str, str]) -> str:
    return "\n\n".join(f"[{key}]\n{value}" for key, value in table.items()) + "\n"


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("translation", type=Path, help="legally supplied standalone LawnStrings .txt")
    parser.add_argument("--english", required=True, type=Path, help="local legal English LawnStrings .txt reference")
    parser.add_argument("--encoding", choices=("utf-8-sig", "utf-16", "gb18030"), default="utf-8-sig")
    parser.add_argument("--english-encoding", choices=("utf-8-sig", "utf-16", "gb18030"), default="utf-8-sig")
    parser.add_argument("--output", type=Path, help="new overlay; absent means validate only")
    parser.add_argument("--report", type=Path, help="new JSON report (keys and hashes, no game text)")
    parser.add_argument("--allow-unknown", action="store_true", help="explicitly drop unmapped keys")
    parser.add_argument("--require-complete", action="store_true", help="fail if any English key absent or unchanged")
    parser.add_argument("--acknowledge-rights", action="store_true", help="confirm lawful local use; not redistribution permission")
    parser.add_argument("--source", default="", help="nonsecret source/author/version description, required for output")
    parser.add_argument("--permission", default="", help="basis for lawful LOCAL use, required for output")
    args = parser.parse_args(argv)
    if args.output and not (args.acknowledge_rights and args.source.strip() and args.permission.strip()):
        parser.error("output requires --acknowledge-rights, --source and --permission; no license is inferred")
    # Never overwrite inputs, existing overlays, or reports, even through symlinks.
    inputs = {args.translation.resolve(), args.english.resolve()}
    outputs = [path for path in (args.output, args.report) if path is not None]
    if len({path.resolve() for path in outputs}) != len(outputs):
        parser.error("output and report must be different files")
    for path in outputs:
        if path.resolve() in inputs or path.exists() or path.is_symlink():
            parser.error("outputs must be new files distinct from inputs; existing files are never overwritten")
    try:
        english, en_hash = load_table(args.english, args.english_encoding)
        chinese, zh_hash = load_table(args.translation, args.encoding)
        overlay, report = validate(english, chinese, args.allow_unknown)
        if args.require_complete and report["untranslated_or_missing_keys"]:
            report["errors"].append("complete coverage requested but English keys remain untranslated/missing")
        report.update({"english_sha256": en_hash, "translation_sha256": zh_hash,
                       "source": args.source, "permission": args.permission,
                       "redistribution_permission_verified": False})
        report["valid"] = not report["errors"]
        rendered = json.dumps(report, ensure_ascii=False, indent=2) + "\n"
        if args.report:
            with args.report.open("x", encoding="utf-8", newline="\n") as handle:
                handle.write(rendered)
        print(rendered, end="")
        if report["errors"]:
            return 1
        if args.output:
            payload = serialize(overlay)
            # Empty overlay is valid when everything is unchanged; nothing to install.
            if not overlay:
                raise ImportFailure("no changed, known translation entries to export")
            if parse_table(payload) != overlay:
                raise ImportFailure("output did not round-trip through runtime-compatible parser")
            with args.output.open("x", encoding="utf-8", newline="\n") as handle:
                handle.write(payload)
        return 0
    except (OSError, ImportFailure) as exc:
        print(f"import failed: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
