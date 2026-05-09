# firmware-ai-reviewer — Claude Code Context

## What This Project Is

A prompt-chained LLM pipeline for static analysis of embedded C firmware.
Target platform: TI CC2652R7 (ARM Cortex-M4F), FreeRTOS, C99.
Portfolio project demonstrating AI engineering applied to embedded systems.

## Architecture (4 phases)

```
Phase 1 — Route:   reviewer.py calls claude-haiku with prompts/router.md
                   Input:  .c file content
                   Output: JSON array of detected domains e.g. ["RTOS", "ISR"]

Phase 2 — Inject:  orchestrator maps domains → unique set of expert prompt files
                   DOMAIN_TO_EXPERT dict in reviewer.py controls the mapping

Phase 3 — Experts: parallel ThreadPoolExecutor calls to claude-sonnet
                   each expert gets its prompt + the .c file
                   each returns a JSON vulnerabilities array with rule IDs

Phase 4 — Merge:   deduplicate by (line_number, rule), sort by line, print JSON
```

## File Map

```
reviewer.py                   — orchestrator (route → inject → parallel experts → merge)
requirements.txt              — anthropic>=0.40.0
prompts/
  router.md                   — haiku domain classifier prompt
  rtos_expert.md              — rules ISR-001..004, RTOS-001..004
  memory_expert.md            — rules MEM-001..008
  hardware_expert.md          — rules HW-001..008
  power_expert.md             — rules PWR-001..005, SAF-001..002
eval_suite/
  01_isr_nonfromisr_api.c     — bugs: ISR-001, ISR-002
  02_volatile_missing.c       — bugs: MEM-001, MEM-003
  03_dma_stack_buffer.c       — bugs: HW-001, HW-003
  04_rmw_race.c               — bugs: MEM-004, RTOS-003
  05_callback_context.c       — bugs: ISR-001, ISR-002
  expected/
    01.json .. 05.json        — ground-truth expected_rules for eval harness
```

## Rule Taxonomy

| ID       | Description |
|----------|-------------|
| ISR-001  | Non-FromISR FreeRTOS API called from ISR context |
| ISR-002  | Missing portYIELD_FROM_ISR after FromISR call |
| ISR-003  | ISR at priority numerically below SYSCALL threshold calls any FreeRTOS API |
| ISR-004  | ISR blocks, allocates heap, or calls printf |
| RTOS-001 | Shared variable between task and ISR not protected by critical section or volatile |
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
| MEM-008  | sizeof(array_param) inside function returns pointer size, not array size |
| HW-001   | DMA buffer stack-allocated (dangling pointer after return) |
| HW-002   | DMA buffer not naturally aligned for transfer width |
| HW-003   | CPU reads DMA buffer before transfer completion (ownership race) |
| HW-004   | Ping-pong DMA: re-arm order wrong (process before re-arm) |
| HW-005   | uDMAChannelTransferSet item count off-by-one |
| HW-006   | Peripheral register accessed before clock is stable (PRCM) |
| HW-007   | Hardware polling loop without timeout |
| HW-008   | I2C/SPI transaction started without checking bus busy |
| PWR-001  | Power_setConstraint called after peripheral operation starts (race window) |
| PWR-002  | XOSC_HF used without ~300 µs stabilization after wakeup |
| PWR-003  | GPT used as Standby wakeup source (PERIPH domain powered off in Standby) |
| PWR-004  | Power_setConstraint without matching Power_releaseConstraint |
| PWR-005  | Tickless idle hook ignores XOSC_HF stabilization time |
| SAF-001  | Watchdog fed from ISR instead of representative task |
| SAF-002  | Hardware polling loop without finite timeout |

## How to Run

```bash
export ANTHROPIC_API_KEY=sk-ant-...
pip install -r requirements.txt

# Review a file
python reviewer.py eval_suite/01_isr_nonfromisr_api.c --verbose

# Run full eval suite (measures detection accuracy)
python reviewer.py --eval
```

## How to Add a New Eval Test

1. Create `eval_suite/NN_descriptive_name.c` with planted bugs and inline comments explaining each bug
2. Create `eval_suite/expected/NN.json`:
   ```json
   { "description": "...", "expected_rules": ["ISR-001", "MEM-003"] }
   ```
3. Run `python reviewer.py --eval` — the new file is picked up automatically

## How to Add a New Rule

1. Choose the correct expert prompt file (or create a new one for a new domain)
2. Add the rule in the `=== HARD RULES ===` section with the format:
   ```
   RULE XYZ-00N: One-line summary.
     Detail explaining what the compiler/hardware does wrong.
     Fix: what the correct pattern is.
   ```
3. Add an entry to the Rule Taxonomy in this file
4. Create an eval file that contains a known violation of the new rule
5. Run `--eval` to verify detection

## Improvement Backlog

Priority order — pick the next unchecked item in a new session:

- [ ] **Add PWR eval file** — plant PWR-001 (constraint after DMA start) and PWR-003 (GPT wakeup from Standby); currently no eval coverage for power rules
- [ ] **Add HW-002 eval file** — unaligned DMA buffer (uint8_t array passed to 32-bit DMA transfer); stack arrays have no alignment guarantee
- [ ] **Add RTOS-001 eval file** — shared uint32_t flag written from ISR, read-modify-write from task without critical section
- [ ] **Synthesizer phase** — after experts produce findings, add a 5th LLM call that takes the original code + findings JSON and generates the corrected C code as a patch; present as a unified diff
- [ ] **GitHub PR comment output** — add `--format github` flag that formats findings as GitHub PR review comments (JSON matching the GitHub REST API schema for pull request review comments)
- [ ] **CI workflow** — add `.github/workflows/eval.yml` that runs `python reviewer.py --eval` on every push; fails the build if score drops below 5/5
- [ ] **Per-rule detection stats** — `--stats` flag: after `--eval`, print a table showing which rule IDs were caught vs missed across all files, to identify which prompt needs tuning
- [ ] **ISR-003 eval file** — ISR at NVIC priority 3 (above FreeRTOS threshold) calling xQueueSendFromISR; should be flagged as illegal even though it's a FromISR variant

## Models Used

- Router: `claude-haiku-4-5-20251001` (fast, cheap classification)
- Experts: `claude-sonnet-4-6` (strong reasoning for bug detection)

## Key Design Decisions

- **Parallel experts over one large prompt**: eliminates constraint conflict between RTOS and hardware timing rules; each expert's context is dense with high-signal rules only
- **Forced reasoning_scratchpad**: conditions the JSON output on explicit chain-of-thought; reduces false positives
- **Eval suite before prompt tuning**: treats prompt changes like code changes — must not regress detection score
- **Dynamic context injection**: router output determines which expert files load; a file with no RTOS calls never loads rtos_expert.md
