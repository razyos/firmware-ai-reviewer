# Challenge: Score the firmware-ai-reviewer Against L8 Prompt Engineering Principles

## Your Role

You are a senior AI systems architect and prompt engineer. You have deep knowledge of
prompt engineering techniques: instruction prompts, role prompts, few-shot prompting,
chain-of-thought, prompt chaining, evaluation methodology, and structured output patterns.

Your task is to audit the `firmware-ai-reviewer` project — a prompt-chained LLM pipeline
for static analysis of embedded C firmware — and score it against the prompt engineering
principles from the L8 curriculum. Then identify gaps: concepts from the curriculum that
are not yet applied, and would concretely improve this system if implemented.

---

## Project Context

`firmware-ai-reviewer` is a 4-phase prompt-chaining pipeline:

```
Phase 1 (Route)   — LLM classifies which embedded domains are present → JSON array
Phase 2 (Inject)  — Orchestrator maps domains → unique expert prompt files
Phase 3 (Experts) — Parallel LLM calls, one per domain, each a focused expert
Phase 4 (Merge)   — Deduplicate findings by (line, rule), sort by line, output JSON
```

Target platform: TI CC2652R7 (ARM Cortex-M4F), FreeRTOS, C99.
Models: Gemini 2.5 Flash (router + experts), temperature=0.0, thinking_budget=0.
Eval suite: 6 C files with planted bugs; expected rule IDs per file; binary pass/fail on recall.

---

## Artifacts

### reviewer.py (orchestrator)

```python
#!/usr/bin/env python3
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

load_dotenv()

_rate_lock = threading.Lock()
_last_call_time: float = 0.0
_MIN_CALL_INTERVAL = float(os.getenv("RATE_LIMIT_INTERVAL", "1.0"))

SCRIPT_DIR  = Path(__file__).parent
PROMPTS_DIR = SCRIPT_DIR / "prompts"
EVAL_DIR    = SCRIPT_DIR / "eval_suite"

_APP_ENV = os.getenv("APP_ENV", "dev")
_DEMO_MODE = _APP_ENV == "demo"

ROUTER_MODEL = os.getenv("ROUTER_MODEL", "gemini-2.5-flash"   if _DEMO_MODE else "gemini-2.5-flash-lite")
EXPERT_MODEL = os.getenv("EXPERT_MODEL", "gemini-2.5-pro"     if _DEMO_MODE else "gemini-2.5-flash")

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


def _generate(client, model, system, user, max_tokens, response_schema=None):
    """Single Gemini API call with rate limiting.
    Checks finish_reason for MAX_TOKENS and safety blocks before returning.
    Returns None on safety block; caller handles gracefully.
    """
    global _last_call_time
    with _rate_lock:
        wait = _MIN_CALL_INTERVAL - (time.monotonic() - _last_call_time)
        if wait > 0:
            time.sleep(wait)
        _last_call_time = time.monotonic()

    config_kwargs = dict(
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

    candidate = response.candidates[0] if response.candidates else None
    if candidate and candidate.finish_reason.name == "MAX_TOKENS":
        print(f"  [warn] truncated at max_tokens={max_tokens}", file=sys.stderr)

    try:
        return response.text
    except ValueError:
        reason = candidate.finish_reason.name if candidate else "UNKNOWN"
        print(f"  [warn] generation blocked (finish_reason={reason})", file=sys.stderr)
        return None


def route(client, code):
    text = _generate(
        client, ROUTER_MODEL, _load("router.md"),
        f"```c\n{code}\n```", max_tokens=128,
        response_schema={"type": "array", "items": {"type": "string"}},
    )
    try:
        return [d.upper() for d in json.loads(text or "[]")]
    except (json.JSONDecodeError, TypeError):
        return list(DOMAIN_TO_EXPERT.keys())


def expert_review(client, expert_file, code):
    text = _generate(
        client, EXPERT_MODEL, _load(expert_file),
        f"Review this firmware:\n\n```c\n{code}\n```",
        max_tokens=2048, response_schema=EXPERT_SCHEMA,
    )
    try:
        return json.loads(text).get("vulnerabilities", [])
    except (json.JSONDecodeError, KeyError, AttributeError, TypeError):
        return []


def _build_context(path):
    """Returns (context_string, resolved_header_names).

    Security: validates resolved header path stays within source directory.
    Block comments stripped before scanning includes to avoid injecting
    commented-out headers.
    Sentinel comment instructs model to report line numbers relative to .c file.
    """
    source = path.read_text(encoding="utf-8")
    header_dir = path.parent.resolve()
    source_no_block_comments = re.sub(r'/\*.*?\*/', '', source, flags=re.DOTALL)
    local_includes = re.findall(r'(?m)^[ \t]*#include\s+"([^"]+)"', source_no_block_comments)

    header_blocks = []
    resolved_names = []
    for name in local_includes:
        header_path = (path.parent / name).resolve()
        if not header_path.is_relative_to(header_dir):
            print(f"  [warn] skipping out-of-bounds include: {name}", file=sys.stderr)
            continue
        if header_path.exists():
            header_blocks.append(f"// ===== {name} =====\n{header_path.read_text(encoding='utf-8')}")
            resolved_names.append(name)

    if header_blocks:
        header_text = "\n\n".join(header_blocks)
        sentinel = (
            f"// ===== {path.name} — REPORT ALL line_number VALUES RELATIVE TO THIS FILE "
            f"(line 1 = first line below) =====\n"
        )
        return header_text + f"\n\n{sentinel}{source}", resolved_names
    return source, resolved_names


def review_file(client, path, verbose=False):
    context, headers = _build_context(path)
    domains = route(client, context)
    if verbose:
        print(f"  [router] domains: {domains}", file=sys.stderr)

    expert_files = list({DOMAIN_TO_EXPERT[d] for d in domains if d in DOMAIN_TO_EXPERT})
    if not expert_files:
        expert_files = ["rtos_expert.md", "memory_expert.md", "hardware_expert.md"]

    all_findings = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=len(expert_files)) as pool:
        futures = {pool.submit(expert_review, client, ef, context): ef for ef in expert_files}
        for future in concurrent.futures.as_completed(futures):
            all_findings.extend(future.result())

    seen = set()
    unique = []
    for finding in sorted(all_findings, key=lambda x: x.get("line_number", 0)):
        key = (finding.get("line_number"), finding.get("rule"))
        if key not in seen:
            seen.add(key)
            unique.append(finding)

    return {"file": str(path), "headers": headers, "domains": domains, "findings": unique}


def run_eval(client, verbose=False):
    expected_dir = EVAL_DIR / "expected"
    c_files = sorted(EVAL_DIR.glob("*.c"))
    results = []
    for c_file in c_files:
        expected_file = expected_dir / (c_file.stem + ".json")
        if not expected_file.exists():
            continue
        expected_rules = set(json.loads(expected_file.read_text()).get("expected_rules", []))
        try:
            report = review_file(client, c_file, verbose=verbose)
            found_rules = {f["rule"] for f in report["findings"]}
            caught = expected_rules & found_rules
            missed = expected_rules - found_rules
            false_positives = found_rules - expected_rules
            results.append({
                "file": c_file.name, "passed": len(missed) == 0,
                "expected": sorted(expected_rules), "caught": sorted(caught),
                "missed": sorted(missed), "false_positives": sorted(false_positives),
                "error": None,
            })
        except Exception as e:
            results.append({
                "file": c_file.name, "passed": False,
                "expected": sorted(expected_rules), "caught": [], "missed": sorted(expected_rules),
                "false_positives": [], "error": str(e)[:120],
            })

    passed = sum(1 for r in results if r["passed"])
    print(f"\n{'='*52}\n  Eval Results: {passed}/{len(results)} passed\n{'='*52}")
    for r in results:
        status = "PASS" if r["passed"] else ("ERROR" if r["error"] else "FAIL")
        print(f"  [{status}] {r['file']}")
        if r["missed"]:    print(f"         Missed: {r['missed']}")
        if r.get("false_positives"): print(f"         FP warn: {r['false_positives']}")
    print(f"{'='*52}\n")
    return 0 if passed == len(results) else 1
```

---

### prompts/router.md (full)

```
You are an embedded firmware domain classifier.

Read the C source file provided and output ONLY a valid JSON array of the domains it touches.
No prose. No explanation. No markdown fences. Raw JSON array only.

Include a domain ONLY if you can point to at least one specific function call, macro, or
variable declaration in the file that matches that domain's description.
If you are not certain a domain is present, omit it. Fewer correct labels is better than
many uncertain ones.

Available domain labels:
  "RTOS"    — FreeRTOS tasks, queues, semaphores, mutexes, task notifications, vTaskDelay
  "ISR"     — Interrupt handlers, NVIC, portYIELD_FROM_ISR, FromISR API variants, IRQHandler
  "DMA"     — uDMA transfers, DMA descriptors, ping-pong, uDMAChannelTransferSet
  "MEMORY"  — volatile qualifiers, pointer casts, alignment, sizeof, stack vs heap allocation
  "POINTER" — Pointer arithmetic, function pointers, double pointers, void* casts
  "I2C"     — I2C bus transactions, I2CXfer, I2CSend, I2CReceive
  "SPI"     — SPI bus transactions, SPITransfer
  "POWER"   — Power_setConstraint, Power_releaseConstraint, sleep modes, standby, ClockP
  "SAFETY"  — Watchdog, HardFault handlers, MPU, assertions, fault status registers

Example — file with a FreeRTOS queue and an ISR:
["RTOS", "ISR"]

Example — file with DMA and volatile register access:
["DMA", "MEMORY"]

Example — file with a FreeRTOS task, an ISR handler, and a shared volatile variable:
["RTOS", "ISR", "MEMORY"]
```

---

### prompts/rtos_expert.md (structure — representative of all 4 expert prompts)

```
You are an RTOS and interrupt safety auditor specializing in FreeRTOS on ARM Cortex-M4F
(TI CC2652R7: 3 priority bits implemented, 8 levels, lower number = higher urgency).

Your ONLY job is to find bugs. Output strict JSON. No prose. No markdown.

=== REPORTING THRESHOLD ===
Only include a finding in the vulnerabilities array if ALL of the following are true:
1. You can point to a specific line number where the violation occurs.
2. You can name the exact rule ID (e.g., ISR-001) it violates.
3. You are confident — not just suspicious — based on code you can see.
If any condition is not met, omit the finding. A short clean report is better than a
long report full of guesses.

=== HARD RULES YOU MUST ENFORCE ===
[8 rules with description, consequence, and fix for each]

=== EXAMPLE ===
Input snippet:
```c
void UART0_IRQHandler(void) {          // line 10
    char rxByte = HWREG(UART0_BASE);   // line 11
    xQueueSend(uartQueue, &rxByte, 0); // line 12
}
```

Correct reasoning_scratchpad:
"Line 10: UART0_IRQHandler — IRQHandler suffix confirms ISR context.
Line 11: HWREG register read — no FreeRTOS API. Check MEM-001: HWREG is volatile-correct. Clean.
Line 12: xQueueSend — non-FromISR FreeRTOS API. Check ISR-001: VIOLATION.
Check ISR-002: portYIELD_FROM_ISR missing. VIOLATION.
No blocking calls, no heap allocation. ISR-004 clean.
No NVIC priority visible — ISR-003 cannot be evaluated from this snippet."

Correct vulnerabilities: [ISR-001 at line 12, ISR-002 at line 12]

=== HOW TO REASON ===
Before listing vulnerabilities, write your reasoning_scratchpad.
Walk through the code top to bottom. For each ISR or FreeRTOS API call, state:
  "I see [function call]. I check rule [ISR-00X]. Conclusion: [violation or clean]."
Then populate the vulnerabilities array.

=== OUTPUT SCHEMA ===
[JSON schema for {reasoning_scratchpad, vulnerabilities[]}]
```

Note: `hardware_expert.md`, `memory_expert.md`, and `power_expert.md` follow identical
structure (role + reporting threshold + hard rules + one worked example + HOW TO REASON
+ output schema). Each expert covers its own domain-specific rules.

---

### Eval suite structure

```
eval_suite/
  01_isr_nonfromisr_api.c       → expected: [ISR-001, ISR-002]
  02_volatile_missing.c         → expected: [MEM-001, MEM-003]
  03_dma_stack_buffer.c         → expected: [HW-001, HW-003]
  04_rmw_race.c                 → expected: [MEM-004, RTOS-003]
  05_callback_context.c         → expected: [ISR-001, ISR-002]
  06_packed_struct_dma.c        → expected: [MEM-005]  (requires header injection)
  expected/
    *.json                      → { "expected_rules": ["RULE-ID", ...] }
```

Current eval score: **6/6 passed** at temperature=0.0 (deterministic).

Known false positives (rules fired but not in expected set):
- File 02: MEM-002 fires (not expected)
- File 03: MEM-004, MEM-005 fire (not expected)
- File 06: HW-002 fires (not expected)

---

## Scoring Task

Score the implementation against each L8 section below. For each item, indicate:
- **Status**: `IMPLEMENTED`, `PARTIAL`, or `MISSING`
- **Evidence**: what in the code/prompts supports your verdict (1-2 sentences)
- **If PARTIAL or MISSING**: what concrete change would address the gap

### L8 Scoring Rubric

**§2 — Instruction Prompts**
- §2.2 Specific verbs (not "analyze", but "find bugs", "classify domains")
- §2.3 Delimiters for input content
- §2.4 Explicit output format specification
- §2.5 Task decomposition (numbered reasoning steps)
- §2.6 Negative constraints (what not to report, confidence threshold)
- §2.7 Escape hatch for edge cases (empty input, non-C input, no findings)
- §2.8 Prompt injection defense (code as untrusted content, trust labeling)

**§3 — Role Prompts**
- §3.1-3.4 Specific, focused role (not generic "helpful assistant")
- §3.5 Role in system message, task in user message (separation)

**§4 — Few-Shot Prompting**
- §4.4 Representative examples (coverage of edge cases, not just easy cases)
- §4.5 Consistent format across examples
- §4.6 Example ordering (most representative last)
- §4.7 Examples demonstrate reasoning quality, not just output format
- §4.5 beyond-slides: Message-level few-shot format (user/model turn pairs vs inline text)
- §4.9 Negative example: clean file with no findings → returns []

**§5 — Chain-of-Thought**
- §5.3 Structured CoT (reasoning field before answer field in schema)
- §5.4 Self-consistency (multiple chains, majority vote) — at temperature=0 this is moot,
       but note whether the architecture would support it at temperature>0

**§6 — Prompt Chaining**
- §6.1-6.3 Route → Specialize pattern (does it match the chaining patterns?)
- §6.3 Parallel independent steps (are independent calls parallelized?)
- §6.4 Cascading error handling (is intermediate output validated?)
- §6.5 beyond-slides: Semantic routing alternative (embedding-based, no LLM call for routing)

**§7 — Evaluation & Iteration**
- §7.1-7.2 Eval set built before tuning; covers input distribution
- §7.2 beyond-slides: Holdout split (never tuned against)
- §7.3 Metric choice (recall-only vs precision/F1/quality)
- §7.4 LLM-as-judge for quality dimensions beyond rule ID recall
- §7.5 One change at a time; no regression allowed to merge

**§8 — Prompt Versioning and CI**
- §8.1 Prompts in version control
- §8.1 beyond-slides: Batch API for eval cost reduction
- §8.1 Prompt version tagging in individual files
- §8.2 API-level structured output (not prompt-only JSON instructions)
- §8.2 beyond-slides: Token truncation check before parsing (finish_reason)
- §8.2 beyond-slides: Prompt caching for large system prompts

---

## Gap Analysis Task

After scoring, answer:

### Q1: Which three gaps would have the highest impact on detection accuracy or reliability if fixed?
Rank them 1-3. For each: what specifically would you add or change, and which eval file or
failure mode it would address.

### Q2: Which L8 technique from §11 (Real-World Status) is most applicable here but not yet used?
Options include: many-shot ICL, Chain-of-Verification (CoVe), Step-Back Prompting,
prompt compression, meta-prompting (DSPy-style). For your choice: describe exactly how
it would fit into this pipeline and what it would improve.

### Q3: The eval suite measures recall (did we catch the expected rules?).
Design a complementary LLM-as-judge eval that measures at least two additional
quality dimensions. Specify: the judge model, the rubric, the scoring schema, and how
the judge output would integrate with the existing `run_eval()` function.

### Q4: The router uses a full LLM call to classify domains.
L8 §6.5 describes semantic routing as an alternative. Would embedding-based semantic
routing work here? State your reasoning. If yes: sketch the implementation. If no:
explain what makes LLM-based routing necessary for this use case.

### Q5: Identify any prompt engineering anti-patterns in the current prompts.
An anti-pattern is a technique that looks like good PE practice but actively hurts
performance in this context. Cite the specific prompt section and explain why it
is harmful here.

---

## Output Format

Return a JSON object with this structure:

```json
{
  "scores": {
    "section_2_instruction": {
      "items": [
        {
          "criterion": "§2.2 Specific verbs",
          "status": "IMPLEMENTED|PARTIAL|MISSING",
          "evidence": "...",
          "gap_fix": "... (omit if IMPLEMENTED)"
        }
      ]
    },
    "section_3_role": { "items": [...] },
    "section_4_few_shot": { "items": [...] },
    "section_5_cot": { "items": [...] },
    "section_6_chaining": { "items": [...] },
    "section_7_eval": { "items": [...] },
    "section_8_versioning": { "items": [...] }
  },
  "top_3_gaps": [
    {
      "rank": 1,
      "gap": "...",
      "concrete_fix": "...",
      "addresses": "..."
    }
  ],
  "q2_l8_section11_technique": {
    "technique": "...",
    "how_it_fits": "...",
    "what_it_improves": "..."
  },
  "q3_llm_judge_design": {
    "judge_model": "...",
    "rubric_dimensions": ["...", "..."],
    "scoring_schema": {},
    "integration_note": "..."
  },
  "q4_semantic_routing": {
    "verdict": "viable|not_viable",
    "reasoning": "...",
    "sketch_or_objection": "..."
  },
  "q5_anti_patterns": [
    {
      "prompt_file": "...",
      "section": "...",
      "anti_pattern": "...",
      "why_harmful": "..."
    }
  ]
}
```

Be direct. Cite specific line numbers or prompt sections. Avoid generic praise.
If something is well done, say so once and move on. Spend your analysis budget on gaps.
