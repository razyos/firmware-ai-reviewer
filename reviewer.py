#!/usr/bin/env python3
"""
firmware-ai-reviewer — Prompt-Chained Static Analysis for Embedded C
Target: ARM Cortex-M4F (TI CC2652R7), FreeRTOS, C99

Architecture:
  Phase 1 (Route)   — haiku classifies which embedded domains are present
  Phase 2 (Inject)  — orchestrator loads only the relevant expert prompts
  Phase 3 (Experts) — parallel sonnet calls, each a single-focus domain expert
  Phase 4 (Merge)   — deduplicate findings, sort by line number, output JSON
"""

import sys
import json
import argparse
import concurrent.futures
from pathlib import Path

import anthropic

SCRIPT_DIR  = Path(__file__).parent
PROMPTS_DIR = SCRIPT_DIR / "prompts"
EVAL_DIR    = SCRIPT_DIR / "eval_suite"

ROUTER_MODEL = "claude-haiku-4-5-20251001"
EXPERT_MODEL = "claude-sonnet-4-6"

DOMAIN_TO_EXPERT = {
    "RTOS":    "rtos_expert.md",
    "ISR":     "rtos_expert.md",
    "DMA":     "hardware_expert.md",
    "I2C":     "hardware_expert.md",
    "SPI":     "hardware_expert.md",
    "MEMORY":  "memory_expert.md",
    "POINTER": "memory_expert.md",
    "POWER":   "power_expert.md",
    "SAFETY":  "power_expert.md",
}


def _load(filename: str) -> str:
    return (PROMPTS_DIR / filename).read_text(encoding="utf-8")


def _parse_json_response(text: str) -> dict:
    """Strip markdown fences if present, then parse JSON."""
    text = text.strip()
    if text.startswith("```"):
        lines = text.split("\n")
        text = "\n".join(lines[1:-1])
    return json.loads(text)


def route(client: anthropic.Anthropic, code: str) -> list[str]:
    """Phase 1: Classify which embedded domains are present in the file."""
    response = client.messages.create(
        model=ROUTER_MODEL,
        max_tokens=128,
        system=_load("router.md"),
        messages=[{"role": "user", "content": f"```c\n{code}\n```"}],
    )
    text = response.content[0].text.strip()
    try:
        return [d.upper() for d in json.loads(text)]
    except json.JSONDecodeError:
        return list(DOMAIN_TO_EXPERT.keys())  # fallback: all domains


def expert_review(client: anthropic.Anthropic, expert_file: str, code: str) -> list[dict]:
    """Phase 3: Run one expert and return its vulnerability list."""
    response = client.messages.create(
        model=EXPERT_MODEL,
        max_tokens=2048,
        system=_load(expert_file),
        messages=[{"role": "user", "content": f"Review this firmware:\n\n```c\n{code}\n```"}],
    )
    try:
        result = _parse_json_response(response.content[0].text)
        return result.get("vulnerabilities", [])
    except (json.JSONDecodeError, KeyError):
        return []


def review_file(client: anthropic.Anthropic, path: Path, verbose: bool = False) -> dict:
    """Full review pipeline for one C file."""
    code = path.read_text(encoding="utf-8")

    # Phase 1: Route
    domains = route(client, code)
    if verbose:
        print(f"  [router] domains: {domains}", file=sys.stderr)

    # Phase 2: Select unique expert files based on detected domains
    expert_files = list({DOMAIN_TO_EXPERT[d] for d in domains if d in DOMAIN_TO_EXPERT})
    if not expert_files:
        expert_files = ["rtos_expert.md", "memory_expert.md", "hardware_expert.md"]

    # Phase 3: Parallel expert reviews
    all_findings: list[dict] = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=len(expert_files)) as pool:
        futures = {pool.submit(expert_review, client, ef, code): ef for ef in expert_files}
        for future in concurrent.futures.as_completed(futures):
            if verbose:
                print(f"  [expert] {futures[future]} complete", file=sys.stderr)
            all_findings.extend(future.result())

    # Phase 4: Deduplicate by (line, rule) and sort by line number
    seen: set[tuple] = set()
    unique: list[dict] = []
    for finding in sorted(all_findings, key=lambda x: x.get("line_number", 0)):
        key = (finding.get("line_number"), finding.get("rule"))
        if key not in seen:
            seen.add(key)
            unique.append(finding)

    return {"file": str(path), "domains": domains, "findings": unique}


def run_eval(client: anthropic.Anthropic, verbose: bool = False) -> int:
    """Run reviewer against all eval_suite/*.c files and check against expected rules."""
    expected_dir = EVAL_DIR / "expected"
    c_files = sorted(EVAL_DIR.glob("*.c"))

    if not c_files:
        print("No .c files found in eval_suite/", file=sys.stderr)
        return 1

    results = []
    for c_file in c_files:
        expected_file = expected_dir / (c_file.stem + ".json")
        if not expected_file.exists():
            print(f"  [skip] {c_file.name} — no expected file", file=sys.stderr)
            continue

        expected_rules = set(json.loads(expected_file.read_text()).get("expected_rules", []))
        print(f"  [eval] {c_file.name}...", file=sys.stderr)

        report = review_file(client, c_file, verbose=verbose)
        found_rules = {f["rule"] for f in report["findings"]}

        caught = expected_rules & found_rules
        missed = expected_rules - found_rules

        results.append({
            "file":     c_file.name,
            "passed":   len(missed) == 0,
            "expected": sorted(expected_rules),
            "caught":   sorted(caught),
            "missed":   sorted(missed),
        })

    total  = len(results)
    passed = sum(1 for r in results if r["passed"])

    print(f"\n{'=' * 52}")
    print(f"  Eval Results: {passed}/{total} passed")
    print(f"{'=' * 52}")
    for r in results:
        color = "\033[32m" if r["passed"] else "\033[31m"
        status = f"{color}{'PASS' if r['passed'] else 'FAIL'}\033[0m"
        print(f"  [{status}] {r['file']}")
        if r["missed"]:
            print(f"         Missed: {r['missed']}")
    print(f"{'=' * 52}\n")

    return 0 if passed == total else 1


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Firmware AI Reviewer — Prompt-chained embedded C static analysis",
    )
    parser.add_argument("file", nargs="?", help="C source file to review")
    parser.add_argument("--eval", action="store_true", help="Run full eval suite")
    parser.add_argument("--verbose", "-v", action="store_true", help="Show routing details")
    args = parser.parse_args()

    if not args.file and not args.eval:
        parser.print_help()
        return 1

    client = anthropic.Anthropic()  # reads ANTHROPIC_API_KEY from environment

    if args.eval:
        return run_eval(client, verbose=args.verbose)

    path = Path(args.file)
    if not path.exists():
        print(f"Error: file not found: {path}", file=sys.stderr)
        return 1

    print(f"Reviewing {path.name}...", file=sys.stderr)
    report = review_file(client, path, verbose=args.verbose)
    print(json.dumps(report, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
