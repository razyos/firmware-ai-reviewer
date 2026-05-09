#!/usr/bin/env python3
"""
firmware-ai-reviewer — Prompt-Chained Static Analysis for Embedded C
Target: ARM Cortex-M4F (TI CC2652R7), FreeRTOS, C99

Architecture:
  Phase 1 (Route)   — gemini-2.0-flash classifies which embedded domains are present
  Phase 2 (Inject)  — orchestrator loads only the relevant expert prompts
  Phase 3 (Experts) — parallel gemini-2.5-flash calls, each a single-focus domain expert
  Phase 4 (Merge)   — deduplicate findings, sort by line number, output JSON
"""

import os
import re
import sys
import json
import time
import threading
import argparse
import concurrent.futures
from pathlib import Path

from dotenv import load_dotenv
from google import genai
from google.genai import types

load_dotenv()  # loads .env from the project root if it exists

# Rate limiter — free tier cap is 5 RPM; paid tier is ~1000 RPM.
# RATE_LIMIT_INTERVAL in .env: use 13.0 for free tier, 1.0 for paid tier.
_rate_lock = threading.Lock()
_last_call_time: float = 0.0
_MIN_CALL_INTERVAL = float(os.getenv("RATE_LIMIT_INTERVAL", "1.0"))

SCRIPT_DIR  = Path(__file__).parent
PROMPTS_DIR = SCRIPT_DIR / "prompts"
EVAL_DIR    = SCRIPT_DIR / "eval_suite"

# APP_ENV=demo uses stronger models for final demos/interviews.
# APP_ENV=dev (default) uses cheaper models for prompt iteration.
_APP_ENV = os.getenv("APP_ENV", "dev")
_DEMO_MODE = _APP_ENV == "demo"

ROUTER_MODEL = os.getenv("ROUTER_MODEL", "gemini-2.5-flash"      if _DEMO_MODE else "gemini-2.5-flash-lite")
EXPERT_MODEL = os.getenv("EXPERT_MODEL", "gemini-2.5-pro"        if _DEMO_MODE else "gemini-2.5-flash")

# JSON schema enforced at the API level for expert calls.
# Eliminates JSONDecodeError — the model is constrained to output this structure.
EXPERT_SCHEMA = {
    "type": "object",
    "properties": {
        "reasoning_scratchpad": {"type": "string"},
        "vulnerabilities": {
            "type": "array",
            "items": {
                "type": "object",
                "properties": {
                    "line_number":   {"type": "integer"},
                    "severity":      {"type": "string", "enum": ["Critical", "Warning", "Style"]},
                    "rule":          {"type": "string"},
                    "description":   {"type": "string"},
                    "fix":           {"type": "string"},
                },
                "required": ["line_number", "severity", "rule", "description", "fix"],
            },
        },
    },
    "required": ["reasoning_scratchpad", "vulnerabilities"],
}

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


def _generate(
    client: genai.Client,
    model: str,
    system: str,
    user: str,
    max_tokens: int,
    response_schema: dict | None = None,
) -> str:
    """Single Gemini API call with rate limiting.

    Enforces _MIN_CALL_INTERVAL between calls globally so parallel
    ThreadPoolExecutor threads don't burst past the free-tier RPM cap.

    When response_schema is provided the API enforces JSON mode at the model
    level — the response is guaranteed to be valid JSON matching the schema.
    """
    global _last_call_time
    with _rate_lock:
        wait = _MIN_CALL_INTERVAL - (time.monotonic() - _last_call_time)
        if wait > 0:
            time.sleep(wait)
        _last_call_time = time.monotonic()

    config_kwargs: dict = dict(
        system_instruction=system,
        max_output_tokens=max_tokens,
        temperature=0.0,
        thinking_config=types.ThinkingConfig(thinking_budget=0),
    )
    if response_schema is not None:
        config_kwargs["response_mime_type"] = "application/json"
        config_kwargs["response_schema"] = response_schema

    response = client.models.generate_content(
        model=model,
        config=types.GenerateContentConfig(**config_kwargs),
        contents=user,
    )

    # Warn if the model stopped because of the token budget — the JSON may be
    # truncated and callers will see fewer findings than actually exist.
    candidate = response.candidates[0] if response.candidates else None
    if candidate and candidate.finish_reason.name == "MAX_TOKENS":
        print(
            f"  [warn] response truncated at max_tokens={max_tokens}; findings may be incomplete",
            file=sys.stderr,
        )

    # response.text raises ValueError when the response was blocked by safety
    # filters and contains no text payload.  Return None so callers can fall back.
    try:
        return response.text
    except ValueError:
        reason = candidate.finish_reason.name if candidate else "UNKNOWN"
        print(f"  [warn] generation blocked (finish_reason={reason})", file=sys.stderr)
        return None


def route(client: genai.Client, code: str) -> list[str]:
    """Phase 1: Classify which embedded domains are present in the file."""
    text = _generate(
        client,
        ROUTER_MODEL,
        _load("router.md"),
        f"<source_code>\n{code}\n</source_code>",
        max_tokens=128,
        response_schema={"type": "array", "items": {"type": "string"}},
    )
    try:
        return [d.upper() for d in json.loads(text or "[]")]
    except (json.JSONDecodeError, TypeError):
        return list(DOMAIN_TO_EXPERT.keys())  # fallback: all domains


def expert_review(client: genai.Client, expert_file: str, code: str) -> list[dict]:
    """Phase 3: Run one expert and return its vulnerability list."""
    text = _generate(
        client,
        EXPERT_MODEL,
        _load(expert_file),
        f"Review this firmware:\n\n```c\n{code}\n```",
        max_tokens=2048,
        response_schema=EXPERT_SCHEMA,  # API-level JSON enforcement — no parse errors
    )
    # With JSON mode the response is always valid JSON — json.loads is safe.
    # KeyError guard retained in case the model omits the vulnerabilities key.
    try:
        return json.loads(text).get("vulnerabilities", [])
    except (json.JSONDecodeError, KeyError, AttributeError, TypeError):
        return []


def _build_context(path: Path) -> tuple[str, list[str]]:
    """Assemble source file plus any local headers it includes.

    Returns (context_string, resolved_header_names) so callers don't need to
    re-read the file or re-run the regex.

    Security: header paths are validated to remain within the source file's
    directory — prevents path traversal via crafted #include directives.

    Block comments are stripped before scanning for #include directives so that
    commented-out headers (e.g. inside /* ... */ blocks) are not injected.

    Line numbers in findings must be relative to the SOURCE FILE (line 1 = first
    line of the .c file).  A sentinel comment tells the model exactly where the
    source starts so it can report correct line numbers even when headers are
    prepended.
    """
    source = path.read_text(encoding="utf-8")
    header_dir = path.parent.resolve()

    # Strip C block comments before scanning includes to avoid picking up
    # commented-out headers.  Line comments (//) are already handled by the
    # anchored regex below.
    source_no_block_comments = re.sub(r'/\*.*?\*/', '', source, flags=re.DOTALL)
    local_includes = re.findall(r'(?m)^[ \t]*#include\s+"([^"]+)"', source_no_block_comments)

    header_blocks: list[str] = []
    resolved_names: list[str] = []
    for name in local_includes:
        header_path = (path.parent / name).resolve()
        # Security: reject any path that escapes the source file's directory.
        if not header_path.is_relative_to(header_dir):
            print(f"  [warn] skipping out-of-bounds include: {name}", file=sys.stderr)
            continue
        if header_path.exists():
            header_blocks.append(
                f"// ===== {name} =====\n{header_path.read_text(encoding='utf-8')}"
            )
            resolved_names.append(name)

    if header_blocks:
        header_text = "\n\n".join(header_blocks)
        sentinel = (
            f"// ===== {path.name} — REPORT ALL line_number VALUES RELATIVE TO THIS FILE (line 1 = first line below) =====\n"
        )
        return header_text + f"\n\n{sentinel}{source}", resolved_names
    return source, resolved_names


def review_file(client: genai.Client, path: Path, verbose: bool = False) -> dict:
    """Full review pipeline for one C file."""
    context, headers = _build_context(path)

    # Phase 1: Route
    domains = route(client, context)
    if verbose:
        print(f"  [router] domains: {domains}", file=sys.stderr)

    # Phase 2: Select unique expert files based on detected domains
    expert_files = list({DOMAIN_TO_EXPERT[d] for d in domains if d in DOMAIN_TO_EXPERT})
    if not expert_files:
        expert_files = ["rtos_expert.md", "memory_expert.md", "hardware_expert.md", "power_expert.md"]

    # Phase 3: Parallel expert reviews
    all_findings: list[dict] = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=len(expert_files)) as pool:
        futures = {pool.submit(expert_review, client, ef, context): ef for ef in expert_files}
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

    return {"file": str(path), "headers": headers, "domains": domains, "findings": unique}


def run_eval(client: genai.Client, verbose: bool = False) -> int:
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

        try:
            report = review_file(client, c_file, verbose=verbose)
            found_rules = {f["rule"] for f in report["findings"]}
            caught = expected_rules & found_rules
            missed = expected_rules - found_rules
            false_positives = found_rules - expected_rules
            results.append({
                "file":          c_file.name,
                "passed":        len(missed) == 0,
                "expected":      sorted(expected_rules),
                "caught":        sorted(caught),
                "missed":        sorted(missed),
                "false_positives": sorted(false_positives),
                "error":         None,
            })
        except Exception as e:
            print(f"  [error] {c_file.name}: {e}", file=sys.stderr)
            results.append({
                "file":          c_file.name,
                "passed":        False,
                "expected":      sorted(expected_rules),
                "caught":        [],
                "missed":        sorted(expected_rules),
                "false_positives": [],
                "error":         str(e)[:120],
            })

    total  = len(results)
    passed = sum(1 for r in results if r["passed"])

    print(f"\n{'=' * 52}")
    print(f"  Eval Results: {passed}/{total} passed")
    print(f"{'=' * 52}")
    for r in results:
        if r["error"]:
            status = "\033[33mERROR\033[0m"
        elif r["passed"]:
            status = "\033[32mPASS\033[0m"
        else:
            status = "\033[31mFAIL\033[0m"
        print(f"  [{status}] {r['file']}")
        if r["missed"] and not r["error"]:
            print(f"         Missed: {r['missed']}")
        if r.get("false_positives") and not r["error"]:
            print(f"         \033[33mFP warn\033[0m: {r['false_positives']}")
        if r["error"]:
            print(f"         {r['error'][:80]}")
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

    api_key = os.environ.get("GEMINI_API_KEY")
    if not api_key:
        print("Error: GEMINI_API_KEY environment variable not set", file=sys.stderr)
        print("Get a free key at https://aistudio.google.com/apikey", file=sys.stderr)
        return 1

    client = genai.Client(api_key=api_key)

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
