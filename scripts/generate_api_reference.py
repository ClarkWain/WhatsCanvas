#!/usr/bin/env python3
"""Generate a lightweight public API reference from include/wsc headers."""

from __future__ import annotations

import argparse
import difflib
import re
import sys
from pathlib import Path


PUBLIC_HEADERS = [
    "wsc.h",
    "base.h",
    "Canvas.h",
    "Color.h",
    "Font.h",
    "Image.h",
    "ImageFilter.h",
    "Log.h",
    "Matrix.h",
    "Paint.h",
    "Path.h",
    "Surface.h",
    "TextureSource.h",
    "Version.h",
]

DECL_RE = re.compile(r"^\s*(?:class|struct)\s+(?:WSC_API\s+)?([A-Za-z_]\w*)\b")
ENUM_RE = re.compile(r"^\s*enum\s+(?:class\s+)?([A-Za-z_]\w*)\b")
ACCESS_RE = re.compile(r"^\s*(public|private|protected)\s*:\s*$")


def strip_line_comment(line: str) -> str:
    in_string = False
    escaped = False
    for i, ch in enumerate(line):
        if escaped:
            escaped = False
            continue
        if ch == "\\":
            escaped = True
            continue
        if ch == '"':
            in_string = not in_string
            continue
        if not in_string and line[i : i + 2] == "//":
            return line[:i]
    return line


def normalize_decl(lines: list[str]) -> str:
    text = " ".join(line.strip() for line in lines)
    text = re.sub(r"\s+", " ", text)
    text = text.replace(" ;", ";").replace(" )", ")").replace("( ", "(")
    text = text.replace(" = ", " = ")
    return text.strip()


def is_public_member_decl(line: str) -> bool:
    if not line.endswith(";"):
        return False
    if line.startswith(("using ", "typedef ", "friend ")):
        return False
    if line in ("public:;", "private:;", "protected:;"):
        return False
    if "(" not in line and ")" not in line:
        return False
    if line.startswith(("if ", "for ", "while ", "switch ")):
        return False
    return True


def inline_signature(joined: str) -> str | None:
    if "{" not in joined:
        return None
    prefix = joined.split("{", 1)[0].strip()
    if "(" not in prefix or ")" not in prefix:
        return None
    if prefix.startswith(("if ", "for ", "while ", "switch ")):
        return None
    if prefix.endswith(("=", ":", ",")):
        return None
    initializer_match = re.match(r"^(.*\))\s*:.*$", prefix)
    if initializer_match:
        prefix = initializer_match.group(1).strip()
    return prefix.rstrip() + ";"


def parse_header(path: Path) -> dict[str, object]:
    text = path.read_text(encoding="utf-8")
    lines = text.splitlines()
    types: list[dict[str, object]] = []
    enums: list[str] = []

    i = 0
    while i < len(lines):
        line = strip_line_comment(lines[i])
        enum_match = ENUM_RE.match(line)
        if enum_match:
            enums.append(enum_match.group(1))

        decl_match = DECL_RE.match(line)
        if not decl_match:
            i += 1
            continue

        kind = "struct" if line.lstrip().startswith("struct") else "class"
        name = decl_match.group(1)
        declaration_lines = [line]
        while "{" not in line and ";" not in line and i + 1 < len(lines):
            i += 1
            line = strip_line_comment(lines[i])
            declaration_lines.append(line)

        if ";" in line and "{" not in line:
            i += 1
            continue

        brace_depth = line.count("{") - line.count("}")
        access = "public" if kind == "struct" else "private"
        members: list[str] = []
        pending: list[str] = []
        method_depth = 0
        skip_member_body_depth = 0
        i += 1

        while i < len(lines) and brace_depth > 0:
            raw = strip_line_comment(lines[i]).strip()
            brace_delta = raw.count("{") - raw.count("}")

            if skip_member_body_depth > 0:
                skip_member_body_depth += brace_delta
                brace_depth += brace_delta
                i += 1
                continue

            access_match = ACCESS_RE.match(raw)

            if access_match and brace_depth == 1:
                access = access_match.group(1)
                pending = []
                method_depth = 0
                i += 1
                continue

            if access == "public" and brace_depth == 1 and raw and not raw.startswith("#"):
                pending.append(raw)
                joined = normalize_decl(pending)
                method_depth += raw.count("(") - raw.count(")")
                inline = inline_signature(joined)
                if inline is not None and is_public_member_decl(inline):
                    members.append(inline)
                    pending = []
                    method_depth = 0
                    skip_member_body_depth = max(0, brace_delta)
                elif method_depth <= 0 and joined.endswith(";"):
                    if is_public_member_decl(joined):
                        members.append(joined)
                    pending = []
                    method_depth = 0
                elif "{" in raw:
                    pending = []
                    method_depth = 0
                    skip_member_body_depth = max(0, brace_delta)

            brace_depth += brace_delta
            i += 1

        types.append({"kind": kind, "name": name, "members": members})

    return {"types": types, "enums": sorted(set(enums))}


def generate(include_dir: Path) -> str:
    lines: list[str] = [
        "# WhatsCanvas Public API Reference",
        "",
        "<!-- Generated by scripts/generate_api_reference.py. Do not edit by hand. -->",
        "",
        "This document is a lightweight index of the public headers under `include/wsc/`.",
        "It is meant to stay close to the current C++ API; behavioral contracts remain documented in README and focused docs.",
        "",
        "## Headers",
        "",
    ]

    for header in PUBLIC_HEADERS:
        if (include_dir / header).exists():
            lines.append(f"- `wsc/{header}`")
    lines.append("")

    for header in PUBLIC_HEADERS:
        path = include_dir / header
        if not path.exists():
            continue
        parsed = parse_header(path)
        types = parsed["types"]
        enums = parsed["enums"]
        if not types and not enums:
            continue

        lines.extend([f"## `wsc/{header}`", ""])
        if enums:
            lines.append("Enums:")
            for enum in enums:
                lines.append(f"- `{enum}`")
            lines.append("")

        for item in types:
            name = item["name"]
            kind = item["kind"]
            members = item["members"]
            lines.extend([f"### `{kind} {name}`", ""])
            if members:
                lines.append("Public members:")
                lines.append("")
                for member in members:
                    lines.append(f"- `{member}`")
                lines.append("")
            else:
                lines.append("_No public methods detected by the lightweight generator._")
                lines.append("")

    return "\n".join(lines).rstrip() + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--include-dir", default="include/wsc", type=Path)
    parser.add_argument("--output", default="doc/API_REFERENCE.md", type=Path)
    parser.add_argument("--check", action="store_true", help="Fail if the output file is stale.")
    args = parser.parse_args()

    generated = generate(args.include_dir)
    if args.check:
        existing = args.output.read_text(encoding="utf-8") if args.output.exists() else ""
        if existing != generated:
            diff = difflib.unified_diff(
                existing.splitlines(),
                generated.splitlines(),
                fromfile=str(args.output),
                tofile="generated",
                lineterm="",
            )
            print("\n".join(diff), file=sys.stderr)
            return 1
        return 0

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(generated, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
