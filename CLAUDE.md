# firmware-ai-reviewer — Claude Code Context

## What This Project Is

A prompt-chained LLM pipeline for static analysis of embedded C firmware.
Target platform: TI CC2652R7 (ARM Cortex-M4F), FreeRTOS, C99.
Portfolio project demonstrating AI engineering applied to embedded systems.

## Current State (as of last session)

- **Eval suite:** 5 files, daily quota (20 req/day free tier) was hit after file 03 — files 01-03 ran and produced findings, 04-05 were not reached
- **Full 5/5 eval run:** pending — run at start of next session to verify (quota resets daily)
- **Rate limiter:** 13s between calls — works correctly, prevents 429 burst errors
- **Router:** fixed to use JSON mode (`response_schema` array type) — was previously falling back to all 9 domains, loading 4 experts per file instead of 2-3
- **Models:** configurable via `.env` — defaults to `gemini-2.5-flash` for both router and expert
- **Gemini 3.1 Pro:** confirmed available (`gemini-3.1-pro-preview`) but requires billing enabled on Google Cloud project (free tier limit = 0)
- **Billing:** not yet enabled — switch to `gemini-2.5-pro` as `EXPERT_MODEL` once billing is on (~$0.11/run, recommended for eval work)
- **Pricing context:** 2.5 Flash ~$0.03/run, 2.5 Pro ~$0.11/run, 3.1 Pro ~$0.16/run; $10 billing cap = ~90 runs at 2.5 Pro rates
- **Next focus:** prompt engineering improvements using L8 best practices (4 branches planned — see backlog)

## Architecture (4 phases)

```
Phase 1 — Route:   gemini-2.5-flash (or ROUTER_MODEL from .env) + prompts/router.md
                   Input:  .c file content
                   Output: JSON array of detected domains e.g. ["RTOS", "ISR"]
                   Note:   uses response_schema to force clean JSON array output

Phase 2 — Inject:  orchestrator maps domains → unique set of expert prompt files
                   DOMAIN_TO_EXPERT dict in reviewer.py controls the mapping

Phase 3 — Experts: parallel ThreadPoolExecutor + threading rate limiter (13s/call)
                   each expert: gemini-2.5-flash (or EXPERT_MODEL from .env)
                   each returns a JSON vulnerabilities array with rule IDs
                   uses EXPERT_SCHEMA + response_mime_type for API-level JSON enforcement

Phase 4 — Merge:   deduplicate by (line_number, rule), sort by line, print JSON
```

## File Map

```
reviewer.py                        — orchestrator (route → inject → parallel experts → merge)
requirements.txt                   — google-genai>=1.0.0, python-dotenv>=1.0.0
.env                               — GEMINI_API_KEY, ROUTER_MODEL, EXPERT_MODEL (gitignored)
.env.example                       — template showing all required/optional vars
prompts/
  router.md                        — domain classifier prompt → JSON array
  rtos_expert.md                   — rules ISR-001..004, RTOS-001..004
  memory_expert.md                 — rules MEM-001..008
  hardware_expert.md               — rules HW-001..008
  power_expert.md                  — rules PWR-001..005, SAF-001..002
eval_suite/
  01_isr_nonfromisr_api.c          — bugs: ISR-001, ISR-002
  02_volatile_missing.c            — bugs: MEM-001, MEM-003
  03_dma_stack_buffer.c            — bugs: HW-001, HW-003
  04_rmw_race.c                    — bugs: MEM-004, RTOS-003
  05_callback_context.c            — bugs: ISR-001, ISR-002
  expected/
    01_isr_nonfromisr_api.json     — ground-truth expected_rules
    02_volatile_missing.json
    03_dma_stack_buffer.json
    04_rmw_race.json
    05_callback_context.json
```

## Environment / Setup

```bash
# .env file (gitignored — never committed)
GEMINI_API_KEY=AIza...
ROUTER_MODEL=gemini-2.5-flash          # free tier
EXPERT_MODEL=gemini-2.5-flash          # free tier
# EXPERT_MODEL=gemini-3.1-pro-preview  # requires billing on Google Cloud project
```

Free tier limits for `gemini-2.5-flash`:
- 5 requests/minute — handled by the 13s rate limiter in `_generate()`
- 20 requests/day — a full eval run (5 files × ~3 calls) uses ~15 requests

To enable billing (removes daily limit, pay per token ~$0.004/eval run):
→ console.cloud.google.com → select project → Billing → Link billing account

## How to Run

```bash
pip install -r requirements.txt   # only needed once

# Verify main is green before starting work
python reviewer.py --eval

# Review a single file
python reviewer.py eval_suite/01_isr_nonfromisr_api.c --verbose
```

## How to Add a New Eval Test

1. Create `eval_suite/NN_descriptive_name.c` with planted bugs and inline `/* BUG [RULE-ID] */` comments
2. Create `eval_suite/expected/NN_descriptive_name.json`:
   ```json
   { "description": "...", "expected_rules": ["ISR-001", "MEM-003"] }
   ```
3. Run `python reviewer.py --eval` — picked up automatically

## How to Add a New Rule

1. Choose the correct expert prompt file based on domain
2. Add to `=== HARD RULES ===` section:
   ```
   RULE XYZ-00N: One-line summary.
     What goes wrong without this rule.
     Fix: the correct pattern.
   ```
3. Add to Rule Taxonomy table below
4. Create an eval file with a known violation
5. Run `--eval` — score must not drop on existing files

## Rule Taxonomy

| ID       | Description |
|----------|-------------|
| ISR-001  | Non-FromISR FreeRTOS API called from ISR context |
| ISR-002  | Missing portYIELD_FROM_ISR after FromISR call |
| ISR-003  | ISR at priority below SYSCALL threshold calls any FreeRTOS API |
| ISR-004  | ISR blocks, allocates heap, or calls printf |
| RTOS-001 | Shared variable between task and ISR not protected |
| RTOS-002 | Blocking API called inside taskENTER_CRITICAL |
| RTOS-003 | Binary semaphore used as mutex (no priority inheritance) |
| RTOS-004 | NVIC_SetPriorityGrouping not set to 0 |
| MEM-001  | MMIO register accessed without volatile |
| MEM-002  | Polling loop on hardware flag without volatile |
| MEM-003  | Integer promotion UB — narrow type shifted into sign bit |
| MEM-004  | Non-atomic read-modify-write on shared peripheral register |
| MEM-005  | Packed struct passed to DMA (alignment fault) |
| MEM-006  | Large lookup table not declared const (wastes SRAM) |
| MEM-007  | uint8_t* cast to uint32_t* without alignment check |
| MEM-008  | sizeof(array_param) inside function returns pointer size |
| HW-001   | DMA buffer stack-allocated (dangling pointer after return) |
| HW-002   | DMA buffer not naturally aligned for transfer width |
| HW-003   | CPU reads DMA buffer before transfer completion |
| HW-004   | Ping-pong DMA: re-arm order wrong |
| HW-005   | uDMAChannelTransferSet item count off-by-one |
| HW-006   | Peripheral register accessed before clock stable (PRCM) |
| HW-007   | Hardware polling loop without timeout |
| HW-008   | I2C/SPI transaction started without checking bus busy |
| PWR-001  | Power_setConstraint called after peripheral operation starts |
| PWR-002  | XOSC_HF used without ~300 µs stabilization after wakeup |
| PWR-003  | GPT used as Standby wakeup source (PERIPH domain off in Standby) |
| PWR-004  | Power_setConstraint without matching Power_releaseConstraint |
| PWR-005  | Tickless idle ignores XOSC_HF stabilization time |
| SAF-001  | Watchdog fed from ISR instead of representative task |
| SAF-002  | Hardware polling loop without finite timeout |

## Branching Strategy

**Model: GitHub Flow** — main is always green, every change goes through a branch + PR.

### Rules

1. **Never commit directly to main** — always branch, even for a one-line prompt tweak.
2. **main must always pass eval 5/5** — do not merge a PR if eval shows any regression.
3. **One logical change per branch** — one eval file OR one prompt tune OR one feature.
4. **PR description must include eval score** — copy the eval output into the PR body.

### Branch Naming

```
feature/add-pwr-eval-file       ← new eval test or rule
feature/add-synthesizer-phase   ← new pipeline capability
fix/router-json-fallback        ← bug in orchestrator
eval/tune-rtos-expert-prompt    ← prompt tuning (score must not drop)
docs/update-claude-md           ← documentation only
```

### Session Workflow

```bash
# 1. Start of session
git pull origin main
python reviewer.py --eval          # must be green before starting

# 2. Pick next item from backlog, create branch
git checkout -b feature/add-pwr-eval-file

# 3. Do work, test frequently
python reviewer.py --eval

# 4. When green — commit, push, PR, merge
git add . && git commit -m "feat: ..."
git push -u origin feature/add-pwr-eval-file
gh pr create --title "..." --body "## Eval\n\`\`\`\n5/5 passed\n\`\`\`"
gh pr merge --squash --delete-branch
git checkout main && git pull origin main
```

### What Not to Do

- Do not `git push origin main` directly
- Do not merge if eval shows FAIL or ERROR
- Do not put multiple backlog items in one branch

## Improvement Backlog

Priority order — pick the next unchecked item each session:

- [ ] **Verify full 5/5 eval** — run `python reviewer.py --eval` at start of next session once daily quota resets; confirm all 5 files pass before starting new work
- [ ] **Enable billing + switch to 2.5 Pro** — set `EXPERT_MODEL=gemini-2.5-pro` in `.env` after enabling billing on Google Cloud; run eval to confirm score holds; router stays on Flash

### Prompt Engineering (L8 best practices — do in order, one branch each)

- [ ] **`eval/add-expert-few-shot-examples`** — add 1 content-quality few-shot example to each of the 4 expert prompts; example should show a complete `reasoning_scratchpad` entry + one correctly identified violation for a representative rule; since format is API-enforced, examples demonstrate *reasoning quality* not schema shape (L8 §4.7)
- [ ] **`eval/false-positive-suppression`** — add negative constraint to all 4 expert prompts: "Do not report a violation unless you can identify the exact line number and cite the specific rule ID it violates. If uncertain, omit it." (L8 §2.6)
- [ ] **`eval/structured-reasoning-steps`** — convert HOW TO REASON sections in memory/hardware/power experts from prose to a numbered checklist matching the rtos_expert.md template: "I see X. I check rule Y. Conclusion: Z." (L8 §2.5)
- [ ] **`eval/router-add-third-example`** — add one 3-domain example to router.md to demonstrate that 3+ labels can be returned; covers files with RTOS + ISR + MEMORY patterns (L8 §4.4)

### New Eval Coverage

- [ ] **Add PWR eval file** — plant PWR-001 (constraint set after DMA start) and PWR-003 (GPT as Standby wakeup); no eval coverage for power rules yet
- [ ] **Add HW-002 eval file** — unaligned DMA buffer; uint8_t array passed to 32-bit DMA transfer
- [ ] **Add RTOS-001 eval file** — shared flag written from ISR, RMW from task without critical section
- [ ] **Add ISR-003 eval file** — ISR at NVIC priority 3 (above SYSCALL threshold) calling xQueueSendFromISR; illegal even though it's a FromISR variant

### Features

- [ ] **Synthesizer phase** — 5th LLM call: takes original code + findings JSON → generates corrected C code as a patch
- [ ] **`--format github` flag** — output findings as GitHub PR review comment JSON (GitHub REST API schema)
- [ ] **CI workflow** — `.github/workflows/eval.yml`: run `--eval` on every push, fail if score < 5/5
- [ ] **`--stats` flag** — table of rule IDs caught vs missed across all eval files; identifies which prompt needs tuning

## Prompt Engineering Gap Analysis (L8)

Applied against `~/ai_course/Lecture8/lecture8a-guide.md`. Use this when working on `eval/*` prompt branches.

### What the prompts already do correctly
- Role prompts with specific focus (L8 §3.1-3.4) ✓
- `reasoning_scratchpad` field in JSON schema = structured CoT inside API-enforced output (L8 §8.2) ✓
- API-level `response_schema` + `response_mime_type` — format guaranteed at decoding level, not by prompt instruction (L8 §8.2) ✓
- `temperature=0.2` for deterministic eval runs (L8 §7.6) ✓
- Negative format constraints: "No prose. No markdown." ✓
- Eval suite as regression harness before every merge (L8 §7.1-7.5) ✓

### Gaps to address (ordered by impact)

**Gap 1 — No content-quality few-shot examples in expert prompts** (L8 §4.7)
Rules are described in text only. L8 §4.7: when format is API-enforced, add examples to demonstrate *reasoning quality*, not schema shape. One example per expert file showing: what a correct `reasoning_scratchpad` walk-through looks like + one correctly identified violation with rule ID and fix.

**Gap 2 — No false-positive suppression constraint** (L8 §2.6)
Experts have "Your ONLY job is to find bugs" but no explicit threshold for confidence. Add: "Do not report a finding unless you can quote the exact line and cite the specific rule ID. If uncertain, omit it." Reduces noise in findings.

**Gap 3 — HOW TO REASON is prose, not procedure** (L8 §2.5)
`rtos_expert.md` has the right template: "I see X. I check rule Y. Conclusion: Z." The other three experts have vaguer prose. Convert all to the same numbered checklist format.

**Gap 4 — Router has only 2-domain examples** (L8 §4.4)
Representative examples should cover the distribution. 9 labels, 2 examples — add one 3-domain case (e.g., RTOS + ISR + MEMORY) to show the model it can return more than 2 labels.

## Key Design Decisions

- **Parallel experts over one large prompt** — eliminates constraint conflict; each expert's context is dense with only relevant rules
- **API-level JSON schema enforcement** — `response_schema` + `response_mime_type` on expert calls; no parse errors
- **Forced `reasoning_scratchpad`** — chain-of-thought before findings reduces false positives
- **Threading rate limiter** — 13s minimum between calls; prevents free-tier 5 RPM burst errors
- **Configurable models via `.env`** — swap router/expert model without touching code
- **Eval suite as regression test** — treat prompts like code; score must not drop on any merge
