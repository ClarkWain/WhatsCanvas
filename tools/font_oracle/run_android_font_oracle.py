#!/usr/bin/env python3
"""Run and compare Android font discovery probes using the v1 JSON protocol."""

from __future__ import annotations

import argparse
import json
import pathlib
import subprocess
import sys
from typing import Any


SCHEMA = "whatscanvas.android-font-oracle.v1"


def parse_args() -> tuple[argparse.Namespace, list[str]]:
    parser = argparse.ArgumentParser()
    parser.add_argument("--whatscanvas", required=True, type=pathlib.Path)
    parser.add_argument("--golden", required=True, type=pathlib.Path)
    parser.add_argument("--skia", type=pathlib.Path)
    parser.add_argument("--require-skia", action="store_true")
    parser.add_argument("--output-dir", required=True, type=pathlib.Path)
    arguments, probe_arguments = parser.parse_known_args()
    if probe_arguments and probe_arguments[0] == "--":
        probe_arguments = probe_arguments[1:]
    if not probe_arguments:
        parser.error("probe arguments must follow --")
    return arguments, probe_arguments


def run_probe(
    executable: pathlib.Path,
    arguments: list[str],
    label: str,
    expected_engine: str,
) -> Any:
    if not executable.is_file():
        raise RuntimeError(f"{label} probe does not exist: {executable}")
    try:
        result = subprocess.run(
            [str(executable), *arguments],
            check=False,
            capture_output=True,
            encoding="utf-8",
            errors="replace",
        )
    except OSError as error:
        raise RuntimeError(f"cannot start {label} probe: {error}") from error
    if result.returncode != 0:
        raise RuntimeError(
            f"{label} probe exited with {result.returncode}\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )
    try:
        snapshot = json.loads(result.stdout)
    except json.JSONDecodeError as error:
        raise RuntimeError(f"{label} emitted invalid JSON: {error}") from error
    if not isinstance(snapshot, dict) or snapshot.get("schema") != SCHEMA:
        raise RuntimeError(f"{label} emitted an unsupported oracle schema")
    if snapshot.get("engine") != expected_engine:
        raise RuntimeError(
            f"{label} declared engine {snapshot.get('engine')!r}; "
            f"expected {expected_engine!r}"
        )
    return snapshot


def write_snapshot(path: pathlib.Path, snapshot: Any) -> None:
    path.write_text(
        json.dumps(snapshot, indent=2, ensure_ascii=False, sort_keys=True) + "\n",
        encoding="utf-8",
    )


def first_difference(expected: Any, actual: Any, path: str = "$") -> str | None:
    expected_number = isinstance(expected, (int, float)) and not isinstance(expected, bool)
    actual_number = isinstance(actual, (int, float)) and not isinstance(actual, bool)
    if expected_number and actual_number:
        if expected != actual:
            return f"{path}: {expected!r} != {actual!r}"
        return None
    if type(expected) is not type(actual):
        return f"{path}: type {type(expected).__name__} != {type(actual).__name__}"
    if isinstance(expected, dict):
        expected_keys = set(expected)
        actual_keys = set(actual)
        if expected_keys != actual_keys:
            missing = sorted(expected_keys - actual_keys)
            extra = sorted(actual_keys - expected_keys)
            return f"{path}: missing keys={missing}, extra keys={extra}"
        for key in sorted(expected):
            difference = first_difference(expected[key], actual[key], f"{path}.{key}")
            if difference:
                return difference
        return None
    if isinstance(expected, list):
        if len(expected) != len(actual):
            return f"{path}: length {len(expected)} != {len(actual)}"
        for index, (expected_item, actual_item) in enumerate(zip(expected, actual)):
            difference = first_difference(
                expected_item, actual_item, f"{path}[{index}]"
            )
            if difference:
                return difference
        return None
    if expected != actual:
        return f"{path}: {expected!r} != {actual!r}"
    return None


def compare(expected: Any, actual: Any, label: str) -> None:
    difference = first_difference(expected, actual)
    if difference:
        raise RuntimeError(f"{label} snapshot mismatch at {difference}")


def comparable(snapshot: Any) -> Any:
    normalized = dict(snapshot)
    normalized.pop("engine", None)
    return normalized


def main() -> int:
    arguments, probe_arguments = parse_args()
    arguments.output_dir.mkdir(parents=True, exist_ok=True)

    whatscanvas = run_probe(
        arguments.whatscanvas, probe_arguments, "WhatsCanvas", "whatscanvas"
    )
    write_snapshot(arguments.output_dir / "whatscanvas.json", whatscanvas)

    try:
        golden = json.loads(arguments.golden.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise RuntimeError(f"cannot read golden snapshot {arguments.golden}: {error}") from error
    compare(golden, whatscanvas, "WhatsCanvas/golden")

    if arguments.skia:
        skia = run_probe(arguments.skia, probe_arguments, "Skia", "skia")
        write_snapshot(arguments.output_dir / "skia.json", skia)
        compare(comparable(skia), comparable(whatscanvas), "Skia/WhatsCanvas")
        print("ANDROID_FONT_ORACLE_RESULT=PASS mode=dual")
    elif arguments.require_skia:
        raise RuntimeError("--require-skia was set without --skia")
    else:
        print("ANDROID_FONT_ORACLE_RESULT=PASS mode=golden")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except RuntimeError as error:
        print(f"ANDROID_FONT_ORACLE_RESULT=FAIL reason={error}", file=sys.stderr)
        sys.exit(1)
