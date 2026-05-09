# firmware-ai-reviewer

Deterministic, prompt-chained static analysis for embedded C firmware.
Target platform: TI CC2652R7 (ARM Cortex-M4F), FreeRTOS, C99.

---

## The Problem

Generic LLM code review fails on embedded firmware because:

- **Context dilution** — dumping 10,000 words of rules into one prompt loses critical constraints in the middle of the context window
- **Constraint conflict** — RTOS safety rules and hardware timing rules have opposing implications; a single-prompt reviewer can't handle both without missing violations
- **No verifiability** — "looks correct" is not an engineering metric; there is no way to measure whether a prompt change improved or regressed detection accuracy

---

## Architecture

```
Target .c file
      │
      ▼
┌──────────────────┐
│  Phase 1: Route  │  claude-haiku — classifies which embedded domains
│  (Triage)        │  are present in the file → JSON domain list
└────────┬─────────┘
         │  e.g. ["RTOS", "ISR", "DMA"]
         ▼
┌─────────────────────────────────────────────┐
│  Phase 2: Dynamic Context Assembly          │
│  Orchestrator loads only the expert         │
│  prompt files matching detected domains     │
└──────────────────────┬──────────────────────┘
                       │
         ┌─────────────┼──────────────┐
         ▼             ▼              ▼
   ┌──────────┐  ┌──────────┐  ┌──────────┐
   │  RTOS &  │  │ Memory & │  │Hardware &│   Phase 3: Parallel
   │   ISR    │  │ Pointer  │  │   DMA    │   Expert Reviews
   │  Expert  │  │  Expert  │  │  Expert  │   (claude-sonnet)
   └────┬─────┘  └────┬─────┘  └────┬─────┘
        │              │              │
        └──────────────┼──────────────┘
                       ▼
              ┌─────────────────┐
              │  Phase 4: Merge │  Deduplicate, sort by line,
              │                 │  output unified JSON report
              └─────────────────┘
```

Each expert:
- Receives **only** the rules for its domain — no constraint conflict, no dilution
- Is forced to write a `reasoning_scratchpad` before producing findings (chain-of-thought conditioning reduces false positives)
- Outputs typed JSON: `line_number`, `severity`, `rule`, `description`, `fix`

---

## Eval Suite

`eval_suite/` contains C files with **known bugs planted at specific lines**, calibrated against the reviewer.
`eval_suite/expected/` contains the ground-truth rule IDs each file must catch.

| File | Bug Class | Rules to Catch |
|------|-----------|----------------|
| `01_isr_nonfromisr_api.c` | ISR calls blocking `xQueueSend` + missing `portYIELD_FROM_ISR` | ISR-001, ISR-002 |
| `02_volatile_missing.c` | MMIO polling without `volatile` + integer promotion UB on shift | MEM-001, MEM-003 |
| `03_dma_stack_buffer.c` | DMA buffer on stack + CPU reads buffer before transfer completes | HW-001, HW-003 |
| `04_rmw_race.c` | Non-atomic GPIO RMW + binary semaphore as mutex (priority inversion) | MEM-004, RTOS-003 |
| `05_callback_context.c` | ISR-context callback calls blocking `xSemaphoreGive` | ISR-001, ISR-002 |

Run the full eval suite to measure detection accuracy:

```bash
python reviewer.py --eval
```

Expected output:
```
====================================================
  Eval Results: 5/5 passed
====================================================
  [PASS] 01_isr_nonfromisr_api.c
  [PASS] 02_volatile_missing.c
  [PASS] 03_dma_stack_buffer.c
  [PASS] 04_rmw_race.c
  [PASS] 05_callback_context.c
====================================================
```

---

## Usage

```bash
pip install -r requirements.txt
export ANTHROPIC_API_KEY=sk-ant-...

# Review a single file
python reviewer.py path/to/firmware.c

# Verbose mode — shows domain routing decisions
python reviewer.py path/to/firmware.c --verbose

# Run the full eval suite
python reviewer.py --eval
```

---

## Example Output

```json
{
  "file": "eval_suite/01_isr_nonfromisr_api.c",
  "domains": ["RTOS", "ISR"],
  "findings": [
    {
      "line_number": 49,
      "severity": "Critical",
      "rule": "ISR-001",
      "description": "xQueueSend called from ISR context — will corrupt the FreeRTOS scheduler's ready-list structures.",
      "fix": "Replace with xQueueSendFromISR(g_rxQueue, &byte, &xHigherPriorityTaskWoken)."
    },
    {
      "line_number": 49,
      "severity": "Warning",
      "rule": "ISR-002",
      "description": "portYIELD_FROM_ISR not called — a higher-priority task unblocked by this send waits up to 1 tick.",
      "fix": "Declare BaseType_t xWoken = pdFALSE; pass &xWoken to xQueueSendFromISR; call portYIELD_FROM_ISR(xWoken) at ISR exit."
    }
  ]
}
```

---

## Engineering Rationale

**Why prompt chaining instead of one large prompt?**
A single prompt enforcing RTOS rules, DMA timing constraints, and pointer alignment simultaneously suffers from constraint conflict — rules that are correct in one domain imply incorrect things in another. Isolating each domain to a dedicated expert with a minimal, non-conflicting rule set eliminates this.

**Why a reasoning scratchpad?**
LLM output is autoregressive — each token is conditioned on prior tokens. Forcing the model to write its chain-of-thought analysis *before* producing the findings array means the vulnerability judgments are conditioned on explicit reasoning, not just surface-level pattern matching. This measurably reduces false positives.

**Why an eval suite before tuning prompts?**
Without a ground-truth test set, prompt tuning is guesswork. The eval suite converts the problem into a measurable regression: if a prompt change drops detection from 5/5 to 4/5, you know immediately and can revert. Treating prompts like code — with tests.

---

## Rule Taxonomy

| Prefix | Domain |
|--------|--------|
| ISR-   | Interrupt handler safety (FreeRTOS API context, yield) |
| RTOS-  | Scheduler primitives (mutexes vs semaphores, critical sections) |
| MEM-   | Memory access (volatile, alignment, promotion, RMW atomicity) |
| HW-    | Hardware timing (DMA ownership, clock stability, peripheral sequencing) |
| PWR-   | Power management (constraints, wakeup sources, oscillator stabilization) |
| SAF-   | Safety (watchdog, fault handlers, hardware timeouts) |

---

## Platform Context

- **MCU:** TI CC2652R7 — ARM Cortex-M4F, 48 MHz, 256 KB SRAM, 704 KB Flash
- **RTOS:** FreeRTOS 10.x, 3 NVIC priority bits (8 levels), `configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY = 5`
- **DMA:** TI µDMA controller, ping-pong mode, 32-channel
- **Standards:** C99, MISRA-C:2012 (advisory), IEC 62304 (informing safety rules)
