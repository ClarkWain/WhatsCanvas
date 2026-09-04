#!/usr/bin/env python3
"""Generate the public API reference from declarations and Doxygen comments."""

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
    "CanvasStats.h",
    "Color.h",
    "Font.h",
    "FontSystem.h",
    "FontResolver.h",
    "Image.h",
    "ImageFilter.h",
    "Log.h",
    "Matrix.h",
    "Paint.h",
    "Path.h",
    "Picture.h",
    "Surface.h",
    "TextureSource.h",
    "Version.h",
]

DECL_RE = re.compile(
    r"^\s*(?:class|struct)\s+(?:WSC_API\s+)?"
    r"([A-Za-z_]\w*(?:::[A-Za-z_]\w*)*)\b"
)
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


def sanitize_code_lines(lines: list[str]) -> list[str]:
    """Remove C/C++ comments while preserving line numbers."""
    sanitized: list[str] = []
    in_block = False
    for line in lines:
        output: list[str] = []
        i = 0
        in_string: str | None = None
        escaped = False
        while i < len(line):
            if in_block:
                end = line.find("*/", i)
                if end < 0:
                    i = len(line)
                    continue
                in_block = False
                i = end + 2
                continue

            ch = line[i]
            if in_string is not None:
                output.append(ch)
                if escaped:
                    escaped = False
                elif ch == "\\":
                    escaped = True
                elif ch == in_string:
                    in_string = None
                i += 1
                continue

            if ch in ('"', "'"):
                in_string = ch
                output.append(ch)
                i += 1
                continue
            if line[i : i + 2] == "//":
                break
            if line[i : i + 2] == "/*":
                in_block = True
                i += 2
                continue
            output.append(ch)
            i += 1
        sanitized.append("".join(output))
    return sanitized


def clean_doc_comment(raw_lines: list[str]) -> tuple[str, bool]:
    """Convert a Doxygen comment into readable Markdown."""
    cleaned: list[str] = []
    is_file_doc = False
    for raw in raw_lines:
        line = raw.strip()
        if line.startswith(("/**", "/*!")):
            line = line[3:]
        elif line.startswith(("///", "//!")):
            line = line[3:]
        if line.endswith("*/"):
            line = line[:-2]
        line = line.lstrip()
        if line.startswith("*"):
            line = line[1:].lstrip()
        if line.startswith("@file") or line.startswith("\\file"):
            is_file_doc = True
            line = line[5:].strip()
            if line.endswith(".h"):
                line = ""
        if line.startswith(("@brief ", "\\brief ")):
            line = line[7:].strip()
        cleaned.append(line.rstrip())

    while cleaned and not cleaned[0]:
        cleaned.pop(0)
    while cleaned and not cleaned[-1]:
        cleaned.pop()

    rendered: list[str] = []
    in_parameters = False
    for line in cleaned:
        code_match = re.match(r"[@\\]code(?:\{\.?(\w+)\})?", line)
        if code_match:
            rendered.append(f"```{code_match.group(1) or ''}")
            in_parameters = False
            continue
        if re.match(r"[@\\]endcode", line):
            rendered.append("```")
            in_parameters = False
            continue

        param_match = re.match(r"[@\\]param(?:\[[^]]+\])?\s+(\w+)\s*(.*)", line)
        if param_match:
            if not in_parameters:
                if rendered and rendered[-1]:
                    rendered.append("")
                rendered.append("**Parameters**")
                rendered.append("")
                in_parameters = True
            rendered.append(f"- `{param_match.group(1)}`: {param_match.group(2)}".rstrip())
            continue

        tag_match = re.match(
            r"[@\\](return|returns|retval|throws|throw|note|warning|deprecated|see)\s*(.*)",
            line,
        )
        if tag_match:
            labels = {
                "return": "Returns",
                "returns": "Returns",
                "retval": "Returns",
                "throws": "Throws",
                "throw": "Throws",
                "note": "Note",
                "warning": "Warning",
                "deprecated": "Deprecated",
                "see": "See also",
            }
            if rendered and rendered[-1]:
                rendered.append("")
            rendered.append(f"**{labels[tag_match.group(1)]}:** {tag_match.group(2)}".rstrip())
            in_parameters = False
            continue

        rendered.append(line)
        if line:
            in_parameters = False

    while rendered and not rendered[0]:
        rendered.pop(0)
    while rendered and not rendered[-1]:
        rendered.pop()
    return "\n".join(rendered), is_file_doc


def extract_doc_comments(lines: list[str]) -> tuple[dict[int, str], str]:
    """Map each documented declaration line to its preceding Doxygen text."""
    docs: dict[int, str] = {}
    file_docs: list[str] = []
    i = 0
    while i < len(lines):
        stripped = lines[i].lstrip()
        raw: list[str] = []
        if stripped.startswith(("///", "//!")):
            while i < len(lines) and lines[i].lstrip().startswith(("///", "//!")):
                raw.append(lines[i])
                i += 1
        elif stripped.startswith(("/**", "/*!")):
            while i < len(lines):
                raw.append(lines[i])
                complete = "*/" in lines[i]
                i += 1
                if complete:
                    break
        else:
            i += 1
            continue

        doc, is_file_doc = clean_doc_comment(raw)
        if not doc:
            continue
        if is_file_doc:
            file_docs.append(doc)
            continue
        declaration = i
        while declaration < len(lines):
            candidate = lines[declaration].strip()
            if candidate and not candidate.startswith(("//", "/*", "*")):
                break
            declaration += 1
        if declaration < len(lines):
            docs[declaration] = doc
    return docs, "\n\n".join(file_docs)


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
    docs, file_doc = extract_doc_comments(lines)
    code_lines = sanitize_code_lines(lines)
    types: list[dict[str, object]] = []
    enums: list[dict[str, str]] = []

    i = 0
    while i < len(lines):
        line = code_lines[i]
        enum_match = ENUM_RE.match(line)
        if enum_match:
            enum_name = enum_match.group(1)
            if not any(item["name"] == enum_name for item in enums):
                enums.append({"name": enum_name, "doc": docs.get(i, "")})

        decl_match = DECL_RE.match(line)
        if not decl_match:
            i += 1
            continue

        decl_index = i
        kind = "struct" if line.lstrip().startswith("struct") else "class"
        name = decl_match.group(1)
        declaration_lines = [line]
        while "{" not in line and ";" not in line and i + 1 < len(lines):
            i += 1
            line = code_lines[i]
            declaration_lines.append(line)

        if ";" in line and "{" not in line:
            i += 1
            continue

        brace_depth = line.count("{") - line.count("}")
        access = "public" if kind == "struct" else "private"
        members: list[dict[str, str]] = []
        pending: list[str] = []
        pending_start = -1
        method_depth = 0
        skip_member_body_depth = 0
        i += 1

        while i < len(lines) and brace_depth > 0:
            raw = code_lines[i].strip()
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
                pending_start = -1
                method_depth = 0
                i += 1
                continue

            if access == "public" and brace_depth == 1 and raw and not raw.startswith("#"):
                if not pending:
                    pending_start = i
                pending.append(raw)
                joined = normalize_decl(pending)
                method_depth += raw.count("(") - raw.count(")")
                inline = inline_signature(joined)
                if inline is not None and is_public_member_decl(inline):
                    members.append({"signature": inline, "doc": docs.get(pending_start, "")})
                    pending = []
                    pending_start = -1
                    method_depth = 0
                    skip_member_body_depth = max(0, brace_delta)
                elif method_depth <= 0 and joined.endswith(";"):
                    if is_public_member_decl(joined):
                        members.append({"signature": joined, "doc": docs.get(pending_start, "")})
                    pending = []
                    pending_start = -1
                    method_depth = 0
                elif "{" in raw:
                    pending = []
                    pending_start = -1
                    method_depth = 0
                    skip_member_body_depth = max(0, brace_delta)

            brace_depth += brace_delta
            i += 1

        types.append({
            "kind": kind,
            "name": name,
            "doc": docs.get(decl_index, ""),
            "members": members,
        })

    return {"types": types, "enums": enums, "file_doc": file_doc}


def member_name(signature: str) -> str:
    prefix = signature.split("(", 1)[0].strip()
    operator = prefix.rfind("operator")
    if operator >= 0:
        return prefix[operator:].strip()
    match = re.search(r"(~?[A-Za-z_]\w*)$", prefix)
    return match.group(1) if match else prefix


def append_doc(lines: list[str], doc: str) -> None:
    if not doc:
        return
    lines.extend([doc, ""])


def generate(include_dir: Path) -> str:
    lines: list[str] = [
        "# WhatsCanvas Public API Reference",
        "",
        "<!-- Generated by scripts/generate_api_reference.py. Do not edit by hand. -->",
        "",
        "This document is generated from the declarations and Doxygen comments in",
        "`include/wsc/`. Update the public headers first; this reference intentionally",
        "has no independent behavioral-contract source.",
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
        file_doc = parsed["file_doc"]
        if not types and not enums and not file_doc:
            continue

        lines.extend([f"## `wsc/{header}`", ""])
        append_doc(lines, file_doc)
        if enums:
            lines.append("Enums:")
            for enum in enums:
                lines.append(f"- `{enum['name']}`")
                if enum["doc"]:
                    summary = enum["doc"].split("\n\n", 1)[0].replace("\n", " ")
                    lines.append(f"  {summary}")
            lines.append("")

        for item in types:
            name = item["name"]
            kind = item["kind"]
            members = item["members"]
            lines.extend([f"### `{kind} {name}`", ""])
            append_doc(lines, item["doc"])
            if members:
                groups: dict[str, list[dict[str, str]]] = {}
                for member in members:
                    groups.setdefault(member_name(member["signature"]), []).append(member)
                undocumented: list[str] = []
                for group_name, overloads in groups.items():
                    shared_doc = next((overload["doc"] for overload in overloads if overload["doc"]), "")
                    if not shared_doc:
                        undocumented.extend(overload["signature"] for overload in overloads)
                        continue
                    lines.extend([f"#### `{group_name}`", ""])
                    for overload in overloads:
                        lines.append(f"- `{overload['signature']}`")
                    lines.append("")
                    append_doc(lines, shared_doc)
                if undocumented:
                    lines.extend([
                        "#### Additional public members",
                        "",
                        "Straightforward accessors, value operations, and overloads:",
                        "",
                    ])
                    for signature in undocumented:
                        lines.append(f"- `{signature}`")
                    lines.append("")
            else:
                lines.append("_No public methods detected by the lightweight generator._")
                lines.append("")

    return "\n".join(lines).rstrip() + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--include-dir", default="include/wsc", type=Path)
    parser.add_argument("--output", default="doc/public/reference/API_REFERENCE.md", type=Path)
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
