# firmware-ai-reviewer — Claude Code Context

## What This Project Is

A prompt-chained LLM pipeline for static analysis of embedded C firmware.
Target platform: TI CC2652R7 (ARM Cortex-M4F), FreeRTOS, C99.
Portfolio project demonstrating AI engineering applied to embedded systems.

## Session Protocol (Mandatory)

### Session Start — verify before touching any file
```bash
python reviewer.py --eval                    # CC2652R7 must be 8/8
python reviewer.py --eval --platform stm32   # STM32 must be 3/3
```
Do not begin work if either shows a regression. Fix the regression first.

### Session End — required before closing
1. `python reviewer.py --eval` → CC2652R7 N/N
2. `python reviewer.py --eval --platform stm32` → STM32 M/M
3. Update **CLAUDE.md** — current state section, next session instructions, backlog checkboxes
4. Update **README.md** — eval scores, eval table, architecture diagram if changed
5. Commit + PR + merge to main (main must always be green at close)

**Why the README step:** README is the public face of the project. It drifts from authoritative
to misleading within 2–3 sessions if not maintained. Update CLAUDE.md first (internal state),
then README (public state), commit both in the same PR so they are always in sync.

---

## Current State (as of session 11)

- **Eval suite:** CC2652R7 8/8, STM32 3/3 — deterministic at temperature=0.0
- **Eval validity:** all BUG comments and indirect hint comments removed — tests require real static analysis, not comment-reading (PRs #53, #54)
- **Billing:** enabled on Google Cloud; $10 spend cap set
- **Models:** `gemini-2.5-flash` for both router and expert (2.5 Pro returns 503 under high demand — revisit later)
- **Rate limiter:** configurable via `RATE_LIMIT_INTERVAL` in `.env` — default 1.0s (paid tier)
- **Temperature:** 0.0 (greedy decoding) — deterministic, no eval flakiness
- **Thinking tokens:** disabled (`thinking_budget=0`) — no cost, no benefit for structured JSON output
- **Platform support:** `--platform cc2652r7|stm32` CLI flag (default: cc2652r7); PR #55, #56
- **Router architecture:** template injection — `router_base.md` (hardened C-parsing, all platforms) + `router_signals_{platform}.md` (domain vocabulary); assembled in Python before API call; ensures hardening improvements propagate to all platforms automatically
- **CC2652R7 router:** 12 domains — hardened over 7 rounds of adversarial red-team (PRs #27–#44)
- **STM32 router signals:** `router_signals_stm32.md` — HAL/DMA/cache/callback vocabulary
- **DOMAIN_TO_EXPERT (CC2652R7):** ISR/BLE→rtos_expert; DMA/I2C/SPI→hardware_expert; MEMORY/POINTER→memory_expert; POWER/SAFETY→power_expert; UART→uart_expert; SECURITY→security_expert
- **DOMAIN_TO_EXPERT (STM32):** STM32/DMA→stm32_expert; ISR/RTOS/UART/SPI/I2C→stm32_expert+stm32_rtos_expert; MEMORY/POINTER→memory_expert
- **STM32 experts:** `stm32_expert.md` (STM-001..003, STM-005..006 — D-Cache, HAL locking, priority grouping); `stm32_rtos_expert.md` (ISR-001..004, RTOS-001..004 with STM32 HAL callback ISR recognition)
- **STM-004 retired:** FreeRTOS API misuse in HAL callbacks now reported as ISR-001/ISR-002 by stm32_rtos_expert (correct rule IDs, no duplicate)
- **stm32_expert.md prompt fix (session 11):** reporting threshold condition 7 corrected — was "task AND ISR/callback", now "two task functions OR task + non-ISR callback"; matches HARD RULE EVIDENCE REQUIRED text
- **Known FP pattern (session 11):** stm32/03_hal_locking.c shows RTOS-003 (spurious) + RTOS-004 (duplicate of STM-006) — both from stm32_rtos_expert; test passes 3/3; RTOS-004 duplicate requires Gemini scope consult before fixing (backlog)
- **Expert fork threshold (Gemini-validated, pre-session 12):** Fork a generic expert into a platform-specific variant when ANY of: (A) ISR context recognition differs, (B) domain has platform-specific API violations the generic expert cannot name, (C) generic expert produces confirmed FPs. `memory_expert.md` stays shared (pure C99/GCC). `security_expert.md` and `power_expert.md` are CC2652R7-only — TI APIs; STM32 routing for SECURITY/POWER/SAFETY domains removed from STM32_DOMAIN_TO_EXPERT (explicit gap, pending `stm32_security_expert.md` and `stm32_power_expert.md`)
- **Expert coverage:** all CC2652R7 domains have experts — zero silent gaps
- **Prompt engineering (L8):** all gaps addressed — few-shot + near-miss examples in all experts, structured CoT, negative constraints, verification instructions
- **Header context injection:** `_build_context()` prepends local `#include "..."` headers; proven by eval file 06
- **Model profiles:** `APP_ENV=dev` (flash-lite router + flash expert) / `APP_ENV=demo` (flash router + 2.5-pro expert) / `APP_ENV=perf` (flash router + 3.1-pro-preview expert — maximum accuracy; preview, may 503 under load)
- **Robustness fixes (PRs #21, #22):** path traversal guard, safety block crash fix, MAX_TOKENS truncation warning, block comment include stripping
- **Gemini consultation protocol:** mandatory for all non-trivial architectural decisions across all projects — defined globally in `~/.claude/CLAUDE.md`; draft with L8 prompt engineering best practices, 4-step audit on response, implement only where both agree
- **Challenge protocol:** mandatory 4-step audit before implementing any LLM challenge response (see section below)
- **Taxonomy:** SAF-002 canonicalized to HW-007; STM-004 retired (covered by ISR-001/002)

## Architecture (4 phases)

```
Phase 1 — Route:   gemini-2.5-flash (or ROUTER_MODEL from .env)
                   Prompt: router_base.md + router_signals_{platform}.md (assembled in Python)
                   Input:  .c file content wrapped in <source_code> tags
                   Output: JSON array of detected domains e.g. ["RTOS", "ISR"]
                   Note:   uses response_schema to force clean JSON array output

Phase 2 — Inject:  orchestrator maps domains → unique set of expert prompt files
                   DOMAIN_TO_EXPERT (cc2652r7) or STM32_DOMAIN_TO_EXPERT (stm32) in reviewer.py

Phase 3 — Experts: parallel ThreadPoolExecutor + threading rate limiter
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
  router_base.md                   — shared C-parsing rules (all platforms) — DO NOT split
  router_signals_cc2652r7.md       — CC2652R7 domain signal vocabulary (12 domains)
  router_signals_stm32.md          — STM32 HAL domain signal vocabulary
  rtos_expert.md                   — CC2652R7: ISR-001..004, RTOS-001..004
  memory_expert.md                 — platform-agnostic: MEM-001..008
  hardware_expert.md               — CC2652R7: HW-001..008
  power_expert.md                  — CC2652R7: PWR-001..005, SAF-001, HW-007
  security_expert.md               — platform-agnostic: SEC-001..005
  uart_expert.md                   — CC2652R7: UART-001..004
  stm32_expert.md                  — STM32: STM-001..003, STM-005..006
  stm32_rtos_expert.md             — STM32: ISR-001..004, RTOS-001..004 (STM32 ISR context)
eval_suite/
  01_isr_nonfromisr_api.c          — bugs: ISR-001, ISR-002
  02_volatile_missing.c            — bugs: MEM-001, MEM-002, MEM-003
  03_dma_stack_buffer.c            — bugs: HW-001, HW-003
  04_rmw_race.c                    — bugs: MEM-004, RTOS-003
  05_callback_context.c            — bugs: ISR-001, ISR-002
  06_packed_struct_dma.c           — bugs: MEM-005, HW-002 (requires sensor_types.h)
  07_crypto_key_leak.c             — bugs: SEC-001, SEC-003
  08_uart_bugs.c                   — bugs: UART-001, UART-004
  sensor_types.h                   — header with packed struct definition for file 06
  expected/                        — ground-truth expected_rules JSON files
  stm32/
    01_dcache_dma_coherency.c      — bugs: STM-001, STM-002, STM-003 (STM32H7)
    02_hal_callback_isr_misuse.c   — bugs: ISR-001, ISR-002 (STM32F4 HAL callbacks)
    expected/
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
python reviewer.py --eval                              # CC2652R7, 8/8
python reviewer.py --eval --platform stm32             # STM32, 2/2

# Run only specific eval files (by numeric prefix) — faster during iteration
python reviewer.py --eval 01,05          # runs 01_* and 05_* only
python reviewer.py --eval 03             # runs 03_* only

# Review a single file
python reviewer.py eval_suite/01_isr_nonfromisr_api.c --verbose
python reviewer.py stm32_firmware.c --platform stm32 --verbose
```

## When to Run Eval

A full eval run costs ~$0.007 and takes ~2 min. Use targeted runs during iteration.

| What changed | Command | Rationale |
|---|---|---|
| `CLAUDE.md`, `README.md`, docs only | skip | No code or prompt changed |
| Single expert prompt tweaked | `--eval <affected files>` | Only those files exercise that expert |
| `router.md` changed | `--eval` (full) | Router affects all file routing |
| `reviewer.py` changed | `--eval` (full) | Orchestration affects all files |
| Before any PR merge | `--eval` (full) | Required — regression gate |
| New eval file added | `--eval` (full) | Verify new file + no regressions |

**Expert → eval file mapping** (use for targeted runs):

| Expert file | Eval files to run |
|---|---|
| `rtos_expert.md` | `01,04,05` |
| `memory_expert.md` | `02,04,06` |
| `hardware_expert.md` | `03,06` |
| `power_expert.md` | *(no eval files yet)* |
| `security_expert.md` | `07` |
| `uart_expert.md` | `08` |

## How to Add a New Eval Test

1. Create `eval_suite/NN_descriptive_name.c` with planted bugs. DO NOT add BUG comments or
   any comments that hint at the rule ID or violation — the eval must test static analysis,
   not comment-reading. Comments should describe hardware behavior only (register names,
   offsets, peripheral descriptions). No "RMW required", "ISR context", "packed struct" etc.
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
| SEC-001  | Key material not zeroized after use (CryptoUtils_memset missing) |
| SEC-002  | TRNG not opened/seeded before first generateEntropy call |
| SEC-003  | Hardcoded key or IV byte-array literal in firmware image |
| SEC-004  | CryptoKey object reused across operations without reinit |
| SEC-005  | AES output buffer not zeroized after operation completes |
| UART-001 | FIFO not enabled — UARTFIFOEnable() missing (interrupt storm at high baud) |
| UART-002 | Baud rate divisor miscalculated in UARTConfigSetExpClk() |
| UART-003 | DMA-UART TX buffer reused before transfer completes |
| UART-004 | Blocking UART write called from ISR or SWI context |

| STM-001  | Cortex-M7: SCB_CleanDCache_by_Addr missing before DMA TX (write-back cache sends stale SRAM) |
| STM-002  | Cortex-M7: SCB_InvalidateDCache_by_Addr missing before CPU reads DMA RX buffer |
| STM-003  | DMA buffer not __attribute__((aligned(32))) — cache maintenance covers wrong cache lines |
| STM-004  | RETIRED — covered by ISR-001/ISR-002 in stm32_rtos_expert |
| STM-005  | STM32 HAL __HAL_LOCK is not FreeRTOS-safe; shared handles between tasks need a mutex |
| STM-006  | NVIC priority grouping not NVIC_PRIORITYGROUP_4 before vTaskStartScheduler |

**Known taxonomy issues (to resolve in future sessions):**
- `RTOS-005` (xQueueSend return unchecked), `RTOS-006` (no stack overflow detection), `MEM-009` (pvPortMalloc NULL dereference), `MEM-010` (use-after-free), `HW-009` (SPI CS not deasserted) — identified gaps, need Gemini sign-off before implementing

## Next Session Start Instructions

**Rule: each session has ONE focus.** Pick one goal from the backlog, do it completely
(eval passing, PR merged, CLAUDE.md + README.md updated), then stop or pick a second goal
only if time remains. Do not context-switch mid-session.

**Session format (paste this to start, then fill in the SESSION GOAL line):**

```
I'm continuing work on the firmware-ai-reviewer portfolio project at
/Users/razyosef/firmware-ai-reviewer. Read CLAUDE.md first for full context.

SESSION GOAL: [one sentence — copied from the backlog below]

Step 1 — verify green baseline before touching anything:
  python reviewer.py --eval                    # CC2652R7, must be 8/8
  python reviewer.py --eval --platform stm32   # STM32, must be 3/3
  Do not start if either fails — fix the regression first.

Step 2 — if the goal requires architectural decisions: follow the Gemini consultation
  protocol in ~/.claude/CLAUDE.md before writing any code.

Step 3 — implement the session goal. One branch, one PR.

Step 4 — session end (mandatory):
  python reviewer.py --eval && python reviewer.py --eval --platform stm32
  Update CLAUDE.md (current state, next session goal, backlog checkboxes).
  Update README.md (eval scores, eval table if changed).
  Commit + PR + merge. main must be green at close.
```

**Session 12 goal (next) — complete STM32 RTOS expert eval coverage:**
```
SESSION GOAL: Add stm32/04_isr_shared_variable.c planting RTOS-001 (HAL callback
writes a shared counter, task RMW without taskENTER_CRITICAL) and RTOS-003 (binary
semaphore used to protect a resource instead of a mutex). No BUG comments or indirect
hint comments. STM32 score must reach 4/4 before closing.
```

**Backlog — ordered, one per session:**

| # | Goal | Requires Gemini? | Notes |
|---|------|-----------------|-------|
| ~~11~~ | ~~`stm32/03_hal_locking.c` — STM-005 + STM-006~~ | ~~No~~ | **DONE** — STM32 suite: 3/3 ✓ |
| 12 | `stm32/04_isr_shared_variable.c` — RTOS-001 + RTOS-003 | No | shared var HAL callback↔task; semaphore-as-mutex |
| 13 | `stm32/05_isr_priority_heap.c` — ISR-003 + ISR-004 | No | IRQ at priority 2 calling FreeRTOS; pvPortMalloc in callback |
| 14 | RTOS-004/STM-006 duplicate — scope fix for stm32_rtos_expert | **Yes** | Gemini sign-off on which expert owns NVIC grouping check |
| 15 | `stm32_security_expert.md` — SEC rules for HAL_CRYP_*, HAL_RNG_* | No | security_expert is CC2652R7-only (TI TRNG/CryptoKey APIs); Gemini-validated |
| 16 | `stm32_power_expert.md` — PWR rules for STM32 Stop/Standby modes | No | power_expert is CC2652R7-only (Power_setConstraint); Gemini-validated |
| 17 | `09_power_bugs.c` — PWR-001 + PWR-003 (CC2652R7) | No | power_expert has zero eval coverage |
| 18 | `10_dma_alignment.c` — HW-002 (CC2652R7) | No | unaligned DMA buffer, hardware_expert |
| 19 | Taxonomy gaps — RTOS-005, MEM-009, HW-009 | **Yes** | Gemini sign-off before any expert edits |
| 20 | Synthesizer phase (5th LLM call → corrected C patch) | **Yes** | Gemini sign-off on prompt structure + output format |

## Branching Strategy

**Model: GitHub Flow** — main is always green, every change goes through a branch + PR.

### Rules

1. **Never commit directly to main** — always branch, even for a one-line prompt tweak.
2. **main must always pass eval** — CC2652R7 8/8 and STM32 3/3; do not merge if either shows regression.
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

See **Session Protocol** section at the top of this file for start/end checklists.

```bash
# During a session — pick next backlog item, create branch
git pull origin main
git checkout -b feature/add-pwr-eval-file

# Do work, test frequently
python reviewer.py --eval

# When green — commit, push, PR, merge
git add . && git commit -m "feat: ..."
git push -u origin feature/add-pwr-eval-file
gh pr create --title "..." --body "## Eval\n\`\`\`\n8/8 passed\n\`\`\`"
gh pr merge --squash --delete-branch
git checkout main && git pull origin main
```

### What Not to Do

- Do not `git push origin main` directly
- Do not merge if eval shows FAIL or ERROR
- Do not put multiple backlog items in one branch

## Improvement Backlog

Priority order — pick the next unchecked item each session:

- [x] **Verify full 5/5 eval** — confirmed 5/5 with billing enabled
- [x] **Enable billing** — done; $10 cap set
- [x] **Prompt engineering (L8)** — all gaps addressed: few-shot examples, near-miss examples (§4.4), false-positive suppression, structured reasoning, RTOS-001 evidence requirement
- [x] **Router over-classification fix** — requires evidence before including domain
- [x] **Thinking tokens disabled** — `thinking_budget=0` on all calls
- [x] **Temperature → 0.0** — deterministic eval, eliminated MEM-003 flakiness
- [x] **Configurable rate limit** — `RATE_LIMIT_INTERVAL` env var, default 1.0s
- [x] **Header context injection** — `_build_context()` in reviewer.py; eval file 06 proves it
- [x] **Model profiles** — `APP_ENV=dev/demo` in `.env`; README updated
- [x] **Robustness fixes** — path traversal, safety block crash, MAX_TOKENS truncation, block comment includes, redundant I/O (PRs #21, #22)
- [x] **False positive elimination** — near-miss examples in all 6 experts; 0 FP warnings on 8/8 eval
- [ ] **Switch to `gemini-2.5-pro`** — returns 503 under high demand currently; retry in a future session

### Router Expansion

- [x] **Add UART domain** — router label done; `uart_expert.md` added session 8 (PR #49)
- [x] **Add BLE/RF domain** — router label done; routes to `rtos_expert.md` for ISR/callback rules
- [x] **Add SECURITY domain** — router label done; `security_expert.md` added session 8 (PR #48)
- [x] **Router template injection** — `router_base.md` + `router_signals_{platform}.md`; hardening propagates to all platforms (PR #56)
- [x] **STM32 platform** — `--platform stm32` flag; `router_signals_stm32.md`; `stm32_expert.md`; `stm32_rtos_expert.md` (PRs #55, #56)

### Expert Coverage Gaps

- [x] **`security_expert.md`** — SEC-001..005; eval 07_crypto_key_leak.c ✓
- [x] **`uart_expert.md`** — UART-001..004; eval 08_uart_bugs.c ✓
- [x] **`stm32_expert.md`** — STM-001..003, STM-005..006; eval stm32/01 ✓
- [x] **`stm32_rtos_expert.md`** — ISR-001..004, RTOS-001..004 (STM32 ISR context); eval stm32/02 ✓
- [ ] **`stm32_security_expert.md`** — SEC rules for HAL_CRYP_*, HAL_RNG_* (Gemini-validated fork; security_expert.md is CC2652R7-only)
- [ ] **`stm32_power_expert.md`** — PWR rules for STM32 Stop/Standby/HAL_PWR_* (Gemini-validated fork; power_expert.md is CC2652R7-only)

### Taxonomy Cleanup

- [x] **HW-007 / SAF-002 duplicate** — canonicalized to HW-007 (PR #50)
- [x] **STM-004 retired** — ISR-001/ISR-002 in stm32_rtos_expert cover it with correct IDs (PR #56)
- [ ] **RTOS-004 / STM-006 duplicate** — both stm32_rtos_expert and stm32_expert check NVIC priority grouping; stm32_rtos_expert should defer to stm32_expert for this check; needs Gemini scope sign-off
- [ ] **RTOS-005** — xQueueSend return value not checked (silent queue full)
- [ ] **RTOS-006** — no stack overflow detection configured (deprioritised — hard to plant without FP)
- [ ] **MEM-009** — pvPortMalloc() NULL dereference (return unchecked)
- [ ] **MEM-010** — use-after-free after vPortFree()
- [ ] **HW-009** — SPI CS not deasserted between transactions

### New Eval Coverage (CC2652R7 — existing domains, no expert work needed)

- [ ] **Add PWR eval file** — plant PWR-001 + PWR-003; no eval coverage for power rules yet
- [ ] **Add HW-002 eval file** — unaligned DMA buffer; uint8_t array passed to 32-bit DMA
- [ ] **Add RTOS-001 eval file** — shared flag written from ISR, RMW from task without critical section
- [ ] **Add ISR-003 eval file** — ISR at NVIC priority 3 calling xQueueSendFromISR (above SYSCALL threshold)

### New Eval Coverage (STM32)

- [x] `01_dcache_dma_coherency.c` — STM-001, STM-002, STM-003 (STM32H7) ✓
- [x] `02_hal_callback_isr_misuse.c` — ISR-001, ISR-002 via HAL callbacks (STM32F4) ✓
- [x] `03_hal_locking.c` — STM-005 (HAL handle shared between tasks), STM-006 (priority grouping) ✓
- [ ] `04_isr_shared_variable.c` — RTOS-001 (HAL callback writes shared var, task RMW unprotected) + RTOS-003 (semaphore as mutex)
- [ ] `05_isr_priority_heap.c` — ISR-003 (IRQ at priority 2 calling FreeRTOS API) + ISR-004 (pvPortMalloc in HAL callback)

### Features

- [ ] **Synthesizer phase** — 5th LLM call: original code + findings JSON → corrected C patch (requires Gemini sign-off on prompt structure first)
- [ ] **`--format github` flag** — output findings as GitHub PR review comment JSON
- [ ] **CI workflow** — `.github/workflows/eval.yml`: run `--eval` on every push, fail if score drops
- [ ] **`--stats` flag** — table of rule IDs caught vs missed across all eval files

## Challenge Response Audit Protocol (Mandatory)

When the user pastes a red-team / adversarial challenge response from another LLM,
do NOT implement the findings immediately. Follow this 4-step protocol first:

1. **Audit** — For each finding, verify the failure path against the actual prompt text.
   Confirm the prompt does not already address it via a rule the other LLM overlooked.

2. **Run the challenge yourself** — Independently work through every attack surface
   using the current prompt text. State your own conclusion (fires / does not fire + reason).

3. **Compare** — "Other LLM says X, I find Y — agree/disagree because Z."
   Where you disagree, your own analysis takes precedence unless the other LLM reveals
   something genuinely missed.

4. **Implement only confirmed findings** — Findings that survive both audits get
   implemented. Disputed findings are noted as "not implemented — reason" in the PR.

**Why:** Implementing challenge responses wholesale without independent verification has
caused over-correction regressions (e.g., the `void*` removal that broke FreeRTOS task
routing). Catching this at audit time is cheaper than reverting after a failing eval.

## Prompt Engineering Standards (Mandatory)

Every prompt or expert file created in this project — and any other project — MUST apply
all of the following concepts. When generating a prompt, list which concepts were applied
in your response to the user — NOT as a comment block inside the prompt file itself.
(Comment blocks in prompt files inject noise into the model's context window.)

### Required Concepts Checklist

| Concept | L8 Section | What it means |
|---|---|---|
| **Role prompting** | §3.1 | Specific expert persona with named domain knowledge, not generic "you are an AI" |
| **Structured CoT** | §2.5 | `reasoning_scratchpad` field required before every verdict/output |
| **Few-shot examples** | §4.7 | At least one full worked example showing correct reasoning quality |
| **Near-miss examples** | §4.4 | Contrast: what a shallow/wrong output looks like vs. the correct one |
| **Negative constraints** | §2.6 | Explicit "do not report unless..." threshold; confidence scoring |
| **Output schema** | §7.6 | JSON schema with required fields enforced at API or prompt level |
| **Prioritization** | §3.4 | Impact ordering stated upfront; highest-impact items first |
| **Verification instruction** | §2.4 | "Before reporting, confirm X is not already addressed" |
| **Temperature** | §7.6 | 0.0 for deterministic eval/classification; stated explicitly |

### When Writing Any Prompt — Required Response Format

After generating a prompt, always state:

```
PROMPT ENGINEERING CONCEPTS APPLIED:
  § 3.1  Role prompting — [what persona, what knowledge]
  § 4.7  Few-shot examples — [how many, what they demonstrate]
  § 4.4  Near-miss examples — [what contrast is shown]
  § 2.5  Structured CoT — [field name used for chain-of-thought]
  § 2.6  Negative constraints — [what the threshold is]
  § 7.6  Output schema — [how format is enforced]
  § 3.4  Prioritization — [how ordering is expressed]
  § 2.4  Verification — [what instruction is given before reporting]
```

This applies to: expert prompts, router prompts, challenge/red-team prompts,
synthesizer prompts, and any LLM prompt in any project.

## Prompt Engineering Gap Analysis (L8)

Applied against `~/ai_course/Lecture8/lecture8a-guide.md`. Use this when working on `eval/*` prompt branches.

### What the prompts already do correctly
- Role prompts with specific focus (L8 §3.1-3.4) ✓
- `reasoning_scratchpad` field in JSON schema = structured CoT inside API-enforced output (L8 §8.2) ✓
- API-level `response_schema` + `response_mime_type` — format guaranteed at decoding level, not by prompt instruction (L8 §8.2) ✓
- `temperature=0.2` for deterministic eval runs (L8 §7.6) ✓
- Negative format constraints: "No prose. No markdown." ✓
- Eval suite as regression harness before every merge (L8 §7.1-7.5) ✓

### Gaps — all closed as of session 7

- **Gap 1 — Few-shot examples** (L8 §4.7): Closed — all 4 experts have a full worked EXAMPLE section showing correct `reasoning_scratchpad` walk-through.
- **Gap 2 — False-positive suppression** (L8 §2.6): Closed — all experts have a REPORTING THRESHOLD section with explicit confidence requirement.
- **Gap 3 — HOW TO REASON format** (L8 §2.5): Closed — all experts use "I see X. I check rule Y. Conclusion: Z." template.
- **Gap 4 — Router multi-domain examples** (L8 §4.4): Closed — router now has 4 examples covering 2-, 2-, 3-, and 4-domain cases.

## Key Design Decisions

- **Parallel experts over one large prompt** — eliminates constraint conflict; each expert's context is dense with only relevant rules
- **API-level JSON schema enforcement** — `response_schema` + `response_mime_type` on expert calls; no parse errors
- **Forced `reasoning_scratchpad`** — chain-of-thought before findings reduces false positives
- **Threading rate limiter** — 13s minimum between calls; prevents free-tier 5 RPM burst errors
- **Configurable models via `.env`** — swap router/expert model without touching code
- **Eval suite as regression test** — treat prompts like code; score must not drop on any merge
