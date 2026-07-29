#!/usr/bin/env python3
"""Verify that public performance claims match checked-in benchmark evidence."""

from __future__ import annotations

import json
import re
import sys
from collections import Counter
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
BASELINE = ROOT / "benchmarks/baselines/nanovg-win-i7-8700-gtx1060"
SUMMARY = BASELINE / "matrix-summary.json"


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def main() -> int:
    errors: list[str] = []
    try:
        summary = json.loads(read(SUMMARY))
        cases = summary["cases"]
    except (OSError, KeyError, json.JSONDecodeError) as error:
        print("PERFORMANCE_CLAIMS_RESULT=FAIL")
        print(f"PERFORMANCE_CLAIMS_ERROR=unable to read canonical baseline: {error}")
        return 1

    verdicts = Counter(case.get("verdict") for case in cases)
    expected = {
        "cells": len(cases),
        "wins": verdicts["whatscanvas faster"],
        "losses": verdicts["nanovg faster"],
        "inconclusive": verdicts["inconclusive"],
        "quality_passed": sum(
            bool(case.get("quality", {}).get("passed")) for case in cases
        ),
    }

    unknown_verdicts = sorted(
        verdict for verdict in verdicts
        if verdict not in {"whatscanvas faster", "nanovg faster", "inconclusive"}
    )
    if unknown_verdicts:
        errors.append(f"unknown verdicts in baseline: {', '.join(unknown_verdicts)}")

    readme = read(ROOT / "README.md")
    marker = re.search(
        r"<!--\s*PERFORMANCE_CLAIM\s+"
        r"baseline=(\S+)\s+wins=(\d+)\s+losses=(\d+)\s+"
        r"inconclusive=(\d+)\s+quality=(\d+)/(\d+)\s*-->",
        readme,
    )
    if not marker:
        errors.append("README.md is missing the PERFORMANCE_CLAIM marker")
    else:
        actual_marker = {
            "baseline": marker.group(1),
            "wins": int(marker.group(2)),
            "losses": int(marker.group(3)),
            "inconclusive": int(marker.group(4)),
            "quality_passed": int(marker.group(5)),
            "cells": int(marker.group(6)),
        }
        expected_baseline = SUMMARY.relative_to(ROOT).as_posix()
        if actual_marker["baseline"] != expected_baseline:
            errors.append(
                "README baseline marker points to "
                f"{actual_marker['baseline']}, expected {expected_baseline}"
            )
        for key in ("wins", "losses", "inconclusive", "quality_passed", "cells"):
            if actual_marker[key] != expected[key]:
                errors.append(
                    f"README marker {key}: expected {expected[key]}, "
                    f"got {actual_marker[key]}"
                )

    public_claim = re.search(
        r"\*\*(\d+) 项领先、(\d+) 项落后、(\d+) 项持平\*\*.*?"
        r"\*\*(\d+) 项像素质量验证通过\*\*",
        readme,
        re.DOTALL,
    )
    if not public_claim:
        errors.append("README.md is missing the public performance summary")
    else:
        actual_public = tuple(int(value) for value in public_claim.groups())
        expected_public = (
            expected["wins"],
            expected["losses"],
            expected["inconclusive"],
            expected["quality_passed"],
        )
        if actual_public != expected_public:
            errors.append(
                f"README public summary: expected {expected_public}, got {actual_public}"
            )

    baseline_readme = read(BASELINE / "README.md")
    total_row = re.search(
        r"\|\s*\*\*Total\*\*\s*\|\s*\*\*(\d+)\*\*\s*\|\s*"
        r"\*\*(\d+)\*\*\s*\|\s*\*\*(\d+)\*\*\s*\|\s*\*\*(\d+)\*\*",
        baseline_readme,
    )
    if not total_row:
        errors.append("baseline README is missing its Total row")
    else:
        actual_total = tuple(int(value) for value in total_row.groups())
        expected_total = (
            expected["cells"],
            expected["wins"],
            expected["losses"],
            expected["inconclusive"],
        )
        if actual_total != expected_total:
            errors.append(
                f"baseline README Total row: expected {expected_total}, got {actual_total}"
            )

    if errors:
        print("PERFORMANCE_CLAIMS_RESULT=FAIL")
        for error in errors:
            print(f"PERFORMANCE_CLAIMS_ERROR={error}")
        return 1

    print(f"PERFORMANCE_CLAIMS_CELLS={expected['cells']}")
    print(f"PERFORMANCE_CLAIMS_WINS={expected['wins']}")
    print(f"PERFORMANCE_CLAIMS_LOSSES={expected['losses']}")
    print(f"PERFORMANCE_CLAIMS_INCONCLUSIVE={expected['inconclusive']}")
    print(f"PERFORMANCE_CLAIMS_QUALITY={expected['quality_passed']}/{expected['cells']}")
    print("PERFORMANCE_CLAIMS_RESULT=PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
