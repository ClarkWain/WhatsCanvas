#!/usr/bin/env python3
"""Compare a classified Android font-config corpus with the pinned Skia probe."""

from __future__ import annotations

import argparse
import copy
import json
import pathlib
import sys
from typing import Any

from run_android_font_oracle import first_difference, run_probe, write_snapshot


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--whatscanvas", required=True, type=pathlib.Path)
    parser.add_argument("--skia", required=True, type=pathlib.Path)
    parser.add_argument("--manifest", required=True, type=pathlib.Path)
    parser.add_argument("--output-dir", required=True, type=pathlib.Path)
    return parser.parse_args()


def canonical_family(value: str) -> str:
    return " ".join(value.strip().lower().split())


def comparable(snapshot: Any) -> Any:
    """Remove producer identity and canonicalize case-insensitive family keys."""
    normalized = copy.deepcopy(snapshot)
    normalized.pop("engine", None)
    for face in normalized.get("faces", []):
        face["families"] = [canonical_family(name) for name in face["families"]]
        face["fallbackFor"] = canonical_family(face["fallbackFor"])
    for alias in normalized.get("aliases", []):
        alias["name"] = canonical_family(alias["name"])
        alias["target"] = canonical_family(alias["target"])
    for query in normalized.get("queries", []):
        query["family"] = canonical_family(query["family"])
    return normalized


def load_manifest(path: pathlib.Path) -> list[dict[str, Any]]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise RuntimeError(f"cannot read corpus manifest {path}: {error}") from error
    if not isinstance(value, list) or not value:
        raise RuntimeError("corpus manifest must be a non-empty array")
    return value


def main() -> int:
    arguments = parse_args()
    entries = load_manifest(arguments.manifest)
    arguments.output_dir.mkdir(parents=True, exist_ok=True)
    strict_count = 0
    extension_count = 0

    for entry in entries:
        fixture_value = entry.get("fixture")
        mode = entry.get("mode")
        font_directory = entry.get("fontDirectory", "/oracle-fonts")
        if not isinstance(fixture_value, str) or mode not in {"strict", "extension"}:
            raise RuntimeError("each corpus entry needs fixture and strict|extension mode")
        fixture = (arguments.manifest.parent / fixture_value).resolve()
        probe_arguments = ["--config", str(fixture), str(font_directory)]
        queries = entry.get("queries", [])
        if not isinstance(queries, list) or not all(
            isinstance(query, str) and query for query in queries
        ):
            raise RuntimeError(f"corpus entry {fixture.stem} has invalid queries")
        for query in queries:
            probe_arguments.extend(["--query", query])
        label = fixture.stem
        whatscanvas = run_probe(
            arguments.whatscanvas, probe_arguments, f"WhatsCanvas/{label}", "whatscanvas"
        )
        skia = run_probe(arguments.skia, probe_arguments, f"Skia/{label}", "skia")
        write_snapshot(arguments.output_dir / f"{label}.whatscanvas.json", whatscanvas)
        write_snapshot(arguments.output_dir / f"{label}.skia.json", skia)

        difference = first_difference(comparable(whatscanvas), comparable(skia))
        if mode == "strict":
            if difference:
                raise RuntimeError(f"strict corpus mismatch for {label} at {difference}")
            strict_count += 1
            continue

        expected_path = entry.get("expectedDifferencePath")
        reason = entry.get("reason")
        if not isinstance(expected_path, str) or not expected_path.startswith("$"):
            raise RuntimeError(f"extension corpus entry {label} needs expectedDifferencePath")
        if not isinstance(reason, str) or not reason.strip():
            raise RuntimeError(f"extension corpus entry {label} needs a reason")
        if not difference:
            raise RuntimeError(
                f"extension corpus entry {label} unexpectedly became equal; review: {reason}"
            )
        if not difference.startswith(expected_path + ":"):
            raise RuntimeError(
                f"extension corpus entry {label} first differed at {difference}; "
                f"expected {expected_path}: ({reason})"
            )
        extension_count += 1

    print(
        "ANDROID_FONT_ORACLE_CORPUS_RESULT=PASS "
        f"strict={strict_count} extensions={extension_count}"
    )
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except RuntimeError as error:
        print(f"ANDROID_FONT_ORACLE_CORPUS_RESULT=FAIL reason={error}", file=sys.stderr)
        sys.exit(1)
