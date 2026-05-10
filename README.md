# firmware-ai-reviewer

Deterministic, prompt-chained static analysis for embedded C firmware.
Supported platforms: **TI CC2652R7** (ARM Cortex-M4F, FreeRTOS) · **STM32F4/F7/H7** (Cortex-M4/M7, FreeRTOS HAL).

---

## The Problem

Generic LLM code review fails on embedded firmware because:

- **Context dilution** — dumping all rules into one prompt loses critical constraints in the middle of the context window
- **Constraint conflict** — RTOS safety rules and hardware timing rules have opposing implications; a single-prompt reviewer can't handle both without missing violations
- **No verifiability** — "looks correct" is not an engineering metric; there is no way to measure whether a prompt change improved or regressed detection accuracy

---

## Architecture

```
Target .c file + local headers
      │
      ▼
┌──────────────────────────┐
│  Phase 0: Context Build  │  Scans #include "..." directives, prepends
│                          │  local headers as labelled blocks.
│                          │  Line numbers in source are preserved.
└────────────┬─────────────┘
             │
             ▼
┌──────────────────────────────────────────────────┐
│  Phase 1: Route                                  │  gemini-2.5-flash classifies which
│                                                  │  embedded domains are present → JSON list
│  router_base.md (C-parsing rules, shared)        │
│  + router_signals_{platform}.md (domain vocab)   │  Template injection — base rules
│  ──────────────────────────────────────────────  │  shared across platforms; signal
│  Platform: --platform cc2652r7 (default)         │  vocabulary is platform-specific.
│         or --platform stm32                      │
└────────────┬─────────────────────────────────────┘
             │  e.g. ["RTOS", "ISR", "DMA"]
             ▼
┌─────────────────────────────────────────────┐
│  Phase 2: Dynamic Context Assembly          │
│  Orchestrator maps domains → unique set of  │
│  expert prompt files (platform-aware)       │
└──────────────────────┬──────────────────────┘
                       │
         ┌─────────────┼────────────────────────────────────┐
         ▼             ▼              ▼           ▼          ▼
   ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────┐  ┌──────────┐
   │  RTOS &  │  │ Memory & │  │Hardware &│  │UART  │  │Security  │   Phase 3: Parallel
   │   ISR    │  │ Pointer  │  │   DMA    │  │Expert│  │ Expert   │   Expert Reviews
   │  Expert  │  │  Expert  │  │  Expert  │  │      │  │          │   (gemini-2.5-flash)
   └────┬─────┘  └────┬─────┘  └────┬─────┘  └──┬───┘  └────┬─────┘
        │              │              │            │            │
        │    STM32 platform adds:                             │
        │    ┌──────────────┐  ┌────────────────────┐        │
        │    │ stm32_expert │  │ stm32_rtos_expert  │        │
        │    │STM-001..003, │  │ ISR/RTOS for STM32 │        │
        │    │STM-005..006  │  │                    │        │
        │    └──────┬───────┘  └────────┬───────────┘        │
        │           │                   │                      │
        └───────────┴───────────────────┴──────────────────────┘
                                │
                       ┌─────────────────┐
                       │  Phase 4: Merge │  Deduplicate by (line, rule),
                       │                 │  sort by line, output JSON report
                       └─────────────────┘
```

Each expert:
- Receives **only** the rules for its domain — no constraint conflict, no dilution
- Is forced to write a `reasoning_scratchpad` before producing findings (chain-of-thought reduces false positives)
- Outputs API-enforced typed JSON: `line_number`, `severity`, `rule`, `description`, `fix`
- Runs at `temperature=0.0` — deterministic output, reproducible eval results

---

## Setup

```bash
pip install -r requirements.txt

# Copy and fill in your API key
cp .env.example .env
# Get a free key at https://aistudio.google.com/apikey
```

**.env** (gitignored):
```
GEMINI_API_KEY=AIza...
APP_ENV=dev          # dev = flash models for iteration; demo = flash router + 2.5-pro expert
RATE_LIMIT_INTERVAL=1.0
```

---

## Usage

```bash
# Review a single file (CC2652R7 platform, default)
python reviewer.py path/to/firmware.c

# Review with verbose routing output
python reviewer.py path/to/firmware.c --verbose

# Review STM32 firmware
python reviewer.py path/to/stm32_firmware.c --platform stm32

# Run the full CC2652R7 eval suite
python reviewer.py --eval

# Run the full STM32 eval suite
python reviewer.py --eval --platform stm32

# Run specific eval files only (faster during iteration)
python reviewer.py --eval 01,05
```

---

## Model Profiles

| `APP_ENV` | Router | Expert | Use case |
|-----------|--------|--------|----------|
| `dev` (default) | gemini-2.5-flash-lite | gemini-2.5-flash | Prompt iteration |
| `demo` | gemini-2.5-flash | gemini-2.5-pro | Interview / production |
| `perf` | gemini-2.5-pro | gemini-2.5-pro | Maximum accuracy, both stages |

Switch with one line in `.env`: `APP_ENV=demo`

---

## Eval Suite

`eval_suite/` contains C files with **known bugs planted at specific lines**.
`eval_suite/expected/` holds the ground-truth rule IDs each file must catch.

### CC2652R7 (default)

| File | Bug Class | Rules |
|------|-----------|-------|
| `01_isr_nonfromisr_api.c` | ISR calls blocking `xQueueSend`, missing `portYIELD_FROM_ISR` | ISR-001, ISR-002 |
| `02_volatile_missing.c` | MMIO polling without `volatile`, integer promotion UB on shift | MEM-001, MEM-002, MEM-003 |
| `03_dma_stack_buffer.c` | DMA buffer on stack, CPU reads buffer before transfer completes | HW-001, HW-003 |
| `04_rmw_race.c` | Non-atomic GPIO RMW, binary semaphore as mutex | MEM-004, RTOS-003 |
| `05_callback_context.c` | ISR-context driver callback calls blocking `xSemaphoreGive` | ISR-001, ISR-002 |
| `06_packed_struct_dma.c` | Packed struct passed to DMA — bug defined in `sensor_types.h` | MEM-005, HW-002 |
| `07_crypto_key_leak.c` | Hardcoded AES key, key material not zeroized after use | SEC-001, SEC-003 |
| `08_uart_bugs.c` | UART FIFO not enabled, blocking UART write in SWI context | UART-001, UART-004 |

File 06 validates **header context injection**: the packed struct definition lives in a header; the tool must read it to catch the violation.

### STM32 (--platform stm32)

| File | Bug Class | Rules |
|------|-----------|-------|
| `stm32/01_dcache_dma_coherency.c` | Cortex-M7 D-Cache: missing SCB_Clean before DMA TX, missing SCB_Invalidate before CPU reads RX buffer, buffers not 32-byte aligned | STM-001, STM-002, STM-003 |
| `stm32/02_hal_callback_isr_misuse.c` | HAL TX/RX completion callbacks call non-FromISR FreeRTOS APIs, missing portYIELD_FROM_ISR | ISR-001, ISR-002 |
| `stm32/03_hal_locking.c` | Same UART handle shared between two FreeRTOS tasks without a mutex; NVIC priority grouping overridden to Group 2 after HAL_Init | STM-005, STM-006 |

```bash
python reviewer.py --eval
```

```
====================================================
  Eval Results: 8/8 passed
====================================================
  [PASS] 01_isr_nonfromisr_api.c
  [PASS] 02_volatile_missing.c
  [PASS] 03_dma_stack_buffer.c
  [PASS] 04_rmw_race.c
  [PASS] 05_callback_context.c
  [PASS] 06_packed_struct_dma.c
  [PASS] 07_crypto_key_leak.c
  [PASS] 08_uart_bugs.c
====================================================
```

```bash
python reviewer.py --eval --platform stm32
```

```
====================================================
  Eval Results: 3/3 passed
====================================================
  [PASS] 01_dcache_dma_coherency.c
  [PASS] 02_hal_callback_isr_misuse.c
  [PASS] 03_hal_locking.c
====================================================
```

---

## Example Output

```json
{
  "file": "eval_suite/01_isr_nonfromisr_api.c",
  "headers": [],
  "domains": ["RTOS", "ISR"],
  "findings": [
    {
      "line_number": 35,
      "severity": "Critical",
      "rule": "ISR-001",
      "description": "xQueueSend called from ISR context — corrupts the FreeRTOS scheduler's ready-list structures.",
      "fix": "Replace with xQueueSendFromISR(g_rxQueue, &byte, &xHigherPriorityTaskWoken)."
    },
    {
      "line_number": 35,
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

**Why router template injection?**
The router is split into `router_base.md` (C-parsing rules, comment stripping, prompt injection resistance — platform-agnostic) and `router_signals_{platform}.md` (domain vocabulary specific to each MCU family). Any hardening to the base rules automatically benefits all platforms. Adding a new platform requires only a signal file, not a full prompt fork.

**Why a reasoning scratchpad?**
LLM output is autoregressive — each token is conditioned on prior tokens. Forcing the model to write its chain-of-thought before producing findings means vulnerability judgments are conditioned on explicit reasoning, not surface-level pattern matching. This reduces false positives.

**Why temperature=0.0?**
Static analysis is a deterministic task. Non-zero temperature introduces variance that makes the eval suite non-deterministic — the same prompt can pass on one run and fail on the next. Greedy decoding eliminates that noise.

**Why inject headers?**
Bug classes like MEM-005 (packed struct to DMA) are defined in header files. A reviewer that only sees the `.c` file is blind to the struct layout. The tool scans `#include "..."` directives and prepends local headers as labelled blocks, preserving original source line numbers.

**Why an eval suite before tuning prompts?**
Without a ground-truth test set, prompt tuning is guesswork. The eval suite converts the problem into a measurable regression: a prompt change that drops detection from 8/8 to 7/8 is visible immediately. Prompts are treated like code — with tests and a CI gate.

**Why separate STM32 experts?**
The STM32 HAL and Cortex-M7 D-Cache introduce bug classes (cache coherency, HAL locking) that have no CC2652R7 equivalent, and vice versa. Using platform-specific experts prevents CC2652R7-tuned ISR context patterns (TI driver callbacks, ClockP SWIs) from firing as false positives on STM32 code — and keeps each expert's rule set minimal and non-conflicting.

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
| SEC-   | Security (key zeroization, RNG seeding, hardcoded secrets) |
| UART-  | UART peripheral (FIFO, baud rate, DMA buffer reuse, ISR context) |
| STM-   | STM32-specific (Cortex-M7 D-Cache coherency, HAL locking, NVIC grouping) |

---

## Platform Context

### TI CC2652R7
- **MCU:** ARM Cortex-M4F, 48 MHz, 256 KB SRAM, 704 KB Flash
- **RTOS:** FreeRTOS 10.x, 3 NVIC priority bits (8 levels), `configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY = 5`
- **DMA:** TI µDMA controller, ping-pong mode, 32-channel
- **Standards:** C99, MISRA-C:2012 (advisory), IEC 62304 (informing safety rules)

### STM32 (F4 / F7 / H7)
- **MCU:** ARM Cortex-M4 (F4) / Cortex-M7 (F7, H7), up to 480 MHz
- **RTOS:** FreeRTOS via STM32CubeMX port, NVIC priority group 4 required
- **DMA:** STM32 DMA controller with streams; Cortex-M7 requires D-Cache maintenance (SCB_CleanDCache_by_Addr / SCB_InvalidateDCache_by_Addr) on every DMA buffer
- **HAL:** STM32 HAL uses `__HAL_LOCK` (byte flag, not a mutex) — not FreeRTOS-safe for multi-task access to the same peripheral handle
