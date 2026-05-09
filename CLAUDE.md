# firmware-ai-reviewer — Claude Code Context

## What This Project Is

A prompt-chained LLM pipeline for static analysis of embedded C firmware.
Target platform: TI CC2652R7 (ARM Cortex-M4F), FreeRTOS, C99.
Portfolio project demonstrating AI engineering applied to embedded systems.

## Current State (as of last session)

- **Eval suite:** 6/6 passing on main — deterministic at temperature=0.0, 0 FP warnings
- **Billing:** enabled on Google Cloud; $10 spend cap set
- **Models:** `gemini-2.5-flash` for both router and expert (2.5 Pro returns 503 under high demand — revisit later)
- **Rate limiter:** configurable via `RATE_LIMIT_INTERVAL` in `.env` — default 1.0s (paid tier)
- **Temperature:** 0.0 (greedy decoding) — deterministic, no eval flakiness
- **Thinking tokens:** disabled (`thinking_budget=0`) — no cost, no benefit for structured JSON output
- **Router:** 12 domains (RTOS, ISR, DMA, MEMORY, POINTER, I2C, SPI, POWER, SAFETY, UART, BLE, SECURITY) — hardened over 7 rounds of adversarial red-team challenge prompts (PRs #27–#44)
- **DOMAIN_TO_EXPERT:** `dict[str, list[str]]` (1-to-many) — ISR/BLE→rtos_expert; DMA/I2C/SPI→hardware_expert; MEMORY/POINTER→memory_expert; POWER/SAFETY→power_expert; UART/SECURITY→unmapped (experts TBD)
- **Fallback fix:** `if not expert_files and not domains` — only fires on classification failure, NOT on unmapped domains (prevents all-expert false positives for SECURITY/UART files)
- **Prompt engineering (L8):** all gaps addressed — few-shot + near-miss examples (§4.4) in all 4 experts, 4 router examples covering 2/2/3/4-domain cases, invocation-based `#define` rules, sizeof() qualification
- **Header context injection:** implemented — `_build_context()` prepends local `#include "..."` headers; line numbers preserved; proven by eval file 06
- **Model profiles:** `APP_ENV=dev` (flash-lite router + flash expert) / `APP_ENV=demo` (flash router + 2.5-pro expert)
- **Robustness fixes (PRs #21, #22):** path traversal guard, safety block crash fix, MAX_TOKENS truncation warning, block comment include stripping
- **Challenge protocol:** mandatory 4-step audit before implementing any LLM challenge response (see Challenge Response Audit Protocol section)
- **Next priority: `security_expert.md`** — SECURITY domain fires in router but has zero expert coverage and no fallback; crypto files get no analysis

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
  02_volatile_missing.c            — bugs: MEM-001, MEM-002, MEM-003
  03_dma_stack_buffer.c            — bugs: HW-001, HW-003
  04_rmw_race.c                    — bugs: MEM-004, RTOS-003
  05_callback_context.c            — bugs: ISR-001, ISR-002
  06_packed_struct_dma.c           — bugs: MEM-005, HW-002 (requires sensor_types.h)
  sensor_types.h                   — header with packed struct definition for file 06
  expected/
    01_isr_nonfromisr_api.json     — ground-truth expected_rules
    02_volatile_missing.json
    03_dma_stack_buffer.json
    04_rmw_race.json
    05_callback_context.json
    06_packed_struct_dma.json
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

# Run only specific eval files (by numeric prefix) — faster during iteration
python reviewer.py --eval 01,05          # runs 01_* and 05_* only
python reviewer.py --eval 03             # runs 03_* only

# Review a single file
python reviewer.py eval_suite/01_isr_nonfromisr_api.c --verbose
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
| SEC-001  | Key material not zeroized after use (CryptoUtils_memset missing) |
| SEC-002  | TRNG not opened/seeded before first generateEntropy call |
| SEC-003  | Hardcoded key or IV byte-array literal in firmware image |
| SEC-004  | CryptoKey object reused across operations without reinit |
| SEC-005  | AES output buffer not zeroized after operation completes |
| UART-001 | FIFO not enabled — UARTFIFOEnable() missing (interrupt storm at high baud) |
| UART-002 | Baud rate divisor miscalculated in UARTConfigSetExpClk() |
| UART-003 | DMA-UART TX buffer reused before transfer completes |
| UART-004 | Blocking UART write called from ISR or SWI context |

**Known taxonomy issues (to resolve in future sessions):**
- `RTOS-005` (xQueueSend return unchecked), `RTOS-006` (no stack overflow detection), `MEM-009` (pvPortMalloc NULL dereference), `MEM-010` (use-after-free), `HW-009` (SPI CS not deasserted) — identified gaps, not yet implemented

## Next Session Start Instructions

```
I'm continuing work on the firmware-ai-reviewer portfolio project at
/Users/razyosef/firmware-ai-reviewer. Read CLAUDE.md first for full context.

Step 0 — merge pending PR (if not already done):
  PR #46 fix/router-hwreg-unclosed-parens is open — HWREG( prefix signals
  were unclosed in router.md; fixed to HWREG(PREFIX...) notation.
  This PR requires a full eval before merge (router changed).

  git checkout fix/router-hwreg-unclosed-parens   # or just:
  python reviewer.py --eval                        # must be 6/6
  gh pr merge 46 --squash --delete-branch
  git checkout main && git pull origin main

Step 1 — verify green baseline:
  python reviewer.py --eval   # must be 6/6 before starting new work

Step 2 — create security_expert.md (highest priority — silent gap):
  SECURITY domain fires in the router but has ZERO expert coverage.
  A crypto file today routes to SECURITY, no expert runs, output is
  "findings": [] with no warning. Silent false negative.

  Full chain:
    a) Create prompts/security_expert.md — apply ALL L8 concepts:
         § 3.1  Role: senior embedded security engineer, TI CC2652R7 CryptoCell,
                AES-CCM, SHA-2, PKA, TRNG, CryptoKey driver
         § 2.6  Threshold: "do not report unless you can cite the exact line
                and the specific SEC-00N rule ID"
         § 3.4  Prioritization: Critical first (key material exposure), then Warning
         § 2.5  Structured CoT: reasoning_scratchpad field
         § 4.7  Few-shot example: full worked snippet with correct scratchpad
         § 4.4  Near-miss example: shallow vs deep finding contrast
         § 7.6  Output schema: same EXPERT_SCHEMA as other experts
         § 2.4  Verification: "before reporting, confirm the key is not immediately
                overwritten or that CryptoUtils_memset is not called later"

       Rules to implement:
         SEC-001: Key material not zeroized after use — memset/CryptoUtils_memset
                  missing after AESECB_open / AESCCM_open operation completes
         SEC-002: TRNG_open() not called before first TRNG_generateEntropy() —
                  RNG used uninitialized
         SEC-003: Hardcoded key or IV as a byte-array literal in firmware image —
                  const uint8_t key[] = {0xAA, ...} visible in binary
         SEC-004: CryptoKey object reused across operations without
                  CryptoKey_initKey() reinit — stale key state
         SEC-005: AES operation result buffer left in SRAM after use without
                  CryptoUtils_memset zeroization

    b) Add "SECURITY": ["security_expert.md"] to DOMAIN_TO_EXPERT in reviewer.py
    c) Create eval_suite/07_crypto_key_leak.c — plant SEC-001 + SEC-003:
         - AES-CCM encrypt, key buffer not zeroized after operation
         - Hardcoded IV literal as a const byte array
    d) Create eval_suite/expected/07_crypto_key_leak.json:
         { "description": "...", "expected_rules": ["SEC-001", "SEC-003"] }
    e) Run --eval; score must be 7/7

Step 3 — create uart_expert.md (second silent gap):
  UART domain fires in the router but has ZERO expert coverage.
  Same full chain:
    a) Create prompts/uart_expert.md — apply ALL L8 concepts.
       Rules to implement:
         UART-001: UARTFIFOEnable() not called — FIFO disabled, single-byte
                   interrupt storm at high baud rates
         UART-002: Baud rate divisor miscalculated — UARTConfigSetExpClk()
                   called with wrong clock source or wrong divisor formula
         UART-003: DMA-UART TX buffer reused before DMA transfer completes —
                   CPU writes new data before uDMAChannelModeGet() confirms STOP
         UART-004: Blocking UART write (UART_write / UARTCharPut) called from
                   ISR or ClockP SWI callback context
    b) Add "UART": ["uart_expert.md"] to DOMAIN_TO_EXPERT in reviewer.py
    c) Create eval_suite/08_uart_bugs.c — plant UART-001 + UART-004
    d) Create eval_suite/expected/08_uart_bugs.json
    e) Run --eval; score must be 8/8

Step 4 — rule taxonomy cleanup (after new experts are green):
  Two issues identified in existing taxonomy:
    DUPLICATE: HW-007 ("Hardware polling loop without timeout") and
               SAF-002 ("Hardware polling loop without finite timeout")
               are the same rule split across two experts. Decide:
               keep in both experts (same rule, two domains) or
               canonicalize to one ID and reference from the other.
    GAPS to consider adding:
      RTOS-005: xQueueSend() / xQueueSendFromISR() return value not checked —
                queue full silently drops data; return pdFAIL never handled
      RTOS-006: No stack overflow detection — configCHECK_FOR_STACK_OVERFLOW=0
                or uxTaskGetStackHighWaterMark never called in production build
      MEM-009:  pvPortMalloc() return value not checked before dereference
      MEM-010:  Use-after-free — pointer used after vPortFree()
      HW-009:   SPI CS line not deasserted between transactions (missing
                GPIO_write(CS_PIN, 1) after SPI_transfer())
    Each gap requires: rule added to expert, eval file planted, score N+1/N+1.
```

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
- [x] **False positive elimination** — near-miss examples in all 4 experts; 0 FP warnings on 6/6 eval
- [ ] **Switch to `gemini-2.5-pro`** — returns 503 under high demand currently; retry in a future session

### Router Expansion

- [x] **Add UART domain** — router label done (PR #27–#44 adversarial hardening); `uart_expert.md` still needed
- [x] **Add BLE/RF domain** — router label done; routes to `rtos_expert.md` for ISR/callback rules (RF callbacks run at ISR priority); `ble_expert.md` not yet created
- [x] **Add SECURITY domain** — router label done; `security_expert.md` not yet created — **NEXT PRIORITY** (silent gap: crypto files get zero expert analysis)

### Pending PR

- [ ] **PR #46** `fix/router-hwreg-unclosed-parens` — HWREG( prefix signals closed to HWREG(PREFIX...) notation; needs `python reviewer.py --eval` (6/6) before merge

### Expert Coverage Gaps (highest priority)

- [ ] **`security_expert.md`** — SEC-001..005; eval 07_crypto_key_leak.c; target 7/7
- [ ] **`uart_expert.md`** — UART-001..004; eval 08_uart_bugs.c; target 8/8

### Taxonomy Cleanup (after new experts)

- [ ] **HW-007 / SAF-002 duplicate** — same rule in two experts; canonicalize
- [ ] **RTOS-005** — xQueueSend return value not checked (silent queue full)
- [ ] **RTOS-006** — no stack overflow detection configured
- [ ] **MEM-009** — pvPortMalloc() NULL dereference (return unchecked)
- [ ] **MEM-010** — use-after-free after vPortFree()
- [ ] **HW-009** — SPI CS not deasserted between transactions

### New Eval Coverage (existing domains)

- [ ] **Add PWR eval file** — plant PWR-001 (constraint set after DMA start) and PWR-003 (GPT as Standby wakeup); no eval coverage for power rules yet
- [ ] **Add HW-002 eval file** — unaligned DMA buffer; uint8_t array passed to 32-bit DMA transfer
- [ ] **Add RTOS-001 eval file** — shared flag written from ISR, RMW from task without critical section
- [ ] **Add ISR-003 eval file** — ISR at NVIC priority 3 (above SYSCALL threshold) calling xQueueSendFromISR; illegal even though it's a FromISR variant

### Features

- [ ] **Synthesizer phase** — 5th LLM call: takes original code + findings JSON → generates corrected C code as a patch
- [ ] **`--format github` flag** — output findings as GitHub PR review comment JSON (GitHub REST API schema)
- [ ] **CI workflow** — `.github/workflows/eval.yml`: run `--eval` on every push, fail if score < 5/5
- [ ] **`--stats` flag** — table of rule IDs caught vs missed across all eval files; identifies which prompt needs tuning

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
