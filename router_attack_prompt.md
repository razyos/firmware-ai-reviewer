# Attack the Router: Find Every Weakness

## Your Role

You are a hostile red-team prompt engineer. Your job is to break the router prompt below.
Find every way it will misclassify firmware, miss domains, fire on wrong input, or fail
to route to the right expert. Be adversarial. Be specific. Do not be polite.

---

## The Router Prompt (full text)

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

## Context: What the Router Feeds Into

The router's output selects which expert prompts run. Each domain maps to one expert file:

| Domain label   | Expert file           | Rules covered |
|----------------|-----------------------|---------------|
| RTOS, ISR      | rtos_expert.md        | ISR-001..004, RTOS-001..004 |
| DMA, I2C, SPI  | hardware_expert.md    | HW-001..008 |
| MEMORY, POINTER| memory_expert.md      | MEM-001..008 |
| POWER, SAFETY  | power_expert.md       | PWR-001..005, SAF-001..002 |

If the router misses a domain, the corresponding expert never runs — bugs go unreported.
If the router fires on a domain that isn't present, a wasted LLM call occurs (cost + latency).

Target platform: TI CC2652R7 (ARM Cortex-M4F, FreeRTOS, BLE 5.2, C99).

---

## Attack Surface 1: Missing Domain Labels

The router only knows 9 domains. For a BLE wireless MCU like the CC2652R7, these are
entirely absent:

- **UART** — UART_open, UART_read, UART_write, UART2_open, UARTprintf, baud rate config,
  FIFO threshold, DMA-backed UART TX/RX
- **BLE / RF Core** — RF_open, RF_postCmd, RF_EventCmdDone, RF_cmdBle5Adv, rfClientEventCb,
  EasyLink, BLE5Stack, HCI commands — RF Core callbacks run at the highest NVIC priority;
  bugs here are as dangerous as ISR bugs but the router won't detect them
- **SECURITY / CRYPTO** — CryptoCC26X2_init, AESCCM_open, SHA2_open, TRNG_open,
  CryptoUtils_memset (key zeroization), CryptoKey_initKey, PKA operations,
  ECDH/ECDSA — hardcoded keys, no key zeroization, RNG not seeded = CVE-class bugs
- **NVS / FLASH** — NVS_open, NVS_write, NVS_erase, NVSInternal, NVSSPI25X — flash
  write cycle limits, concurrent access, write to SRAM mirror before commit
- **TIMER / ClockP** — ClockP_construct, ClockP_start, Timer_open — timer overflow,
  wrong period calculation, ClockP callback runs in software interrupt context

For each: write a 3-line C snippet that contains the domain signal and explain exactly
which expert would be silently skipped because the router doesn't know the label.

---

## Attack Surface 2: Label Boundary Ambiguity

For each pair below, write a C snippet where a human expert would classify it as label A,
but the router description would likely classify it as label B (or miss it entirely).
Explain the misclassification and its consequence (which expert is wrongly skipped/fired):

1. **UART vs DMA** — UART TX via uDMA; the DMA channel is used but the root bug is a
   UART FIFO interaction, not a DMA ownership race
2. **BLE RF callback vs ISR** — RF Core `rfClientEventCb` runs at high priority but is
   not named `*IRQHandler` and does not use portYIELD_FROM_ISR — the router will miss ISR
3. **SECURITY vs MEMORY** — `memset(key_buffer, 0, KEY_SIZE)` after crypto operation;
   the compiler may optimise it away (volatile-correct zeroization needed) — the router
   will fire MEMORY but the real bug class is SECURITY
4. **POWER vs RTOS** — `vTaskSuspendAll()` used to prevent context switch during a flash
   write; the intent is power/NVS sequencing but the router fires RTOS
5. **SAFETY vs ISR** — `WatchdogReloadSet()` called from an ISR — the router fires both
   SAF-001 (watchdog fed from ISR) and ISR, but the safety expert and RTOS expert don't
   coordinate; the finding may be duplicated or missed

---

## Attack Surface 3: Signal Vocabulary Gaps

The router description for each domain lists specific function names and macros as signals.
For each domain below, write a valid CC2652R7 code pattern that clearly belongs to that
domain but uses NONE of the listed signal words — causing the router to output `[]`:

1. **RTOS** — using `xTaskCreateStatic` and `StaticTask_t` only (no queue, no semaphore,
   no vTaskDelay in the snippet shown)
2. **ISR** — using `HwiP_construct` (TI-RTOS/SYS-BIOS style) instead of `*IRQHandler`
   naming or direct NVIC registration
3. **DMA** — using `UDMACC26XX_open` and `UDMACC26XX_channelEnable` (TI driver abstraction)
   instead of `uDMAChannelTransferSet`
4. **POWER** — using `PowerCC26X2_standbyPolicy` struct or `Power_registerNotify` with no
   `Power_setConstraint` call visible
5. **SAFETY** — using `WatchdogCC26X4_init` (TI driver layer) instead of direct
   `WatchdogIntClear` or `WatchdogReloadGet`

---

## Attack Surface 4: Over-Classification Triggers

Write 3 C snippets that contain domain signal words in COMMENTS or STRING LITERALS only —
the code does not actually exercise that domain. Explain whether the router would
incorrectly fire the domain.

Example pattern:
```c
// This function replaces the old xSemaphoreGive-based approach
void updateConfig(void) { config.value = 42; }
```

The string "xSemaphoreGive" appears in a comment. Does the router fire RTOS? Should it?

---

## Attack Surface 5: Cascading Routing Failure

The worst failure mode: the router returns `[]` (empty array). In `reviewer.py`, the
fallback is:
```python
if not expert_files:
    expert_files = ["rtos_expert.md", "memory_expert.md", "hardware_expert.md"]
```

The power_expert is NOT in the fallback list. Write a CC2652R7 firmware file that:
- Contains a genuine PWR-001 violation (Power_setConstraint after DMA start)
- Contains NO signals for RTOS, ISR, DMA, MEMORY, POINTER, I2C, SPI, SAFETY
- Is a realistic standalone power-management module

Explain how the router misses it, the fallback fires three wrong experts, and the
power expert is never called — the bug goes completely unreported.

---

## Attack Surface 6: Prompt Injection via Source Code

The router receives `f"```c\n{code}\n```"` as its user message with no further trust
labeling. The code is untrusted user input. Write a C comment or string literal that
attempts to manipulate the router's output. Test three injection strategies:

1. A comment that attempts to override the domain list: does the router include the
   injected domain?
2. A comment that attempts to return `[]` (suppress all findings)
3. A comment that attempts to return every domain label (cause all experts to run)

For each: state whether the current prompt resists or is vulnerable, and why.

---

## Output Format

Return a JSON object:

```json
{
  "missing_domains": [
    {
      "domain": "UART",
      "signal_snippet": "UART_write(handle, buf, len);",
      "skipped_expert": "uart_expert (does not exist yet)",
      "bug_class_missed": "TX DMA buffer reused before transfer completes"
    }
  ],
  "boundary_ambiguities": [
    {
      "pair": "UART vs DMA",
      "snippet": "...",
      "expected_label": "UART",
      "actual_label": "DMA",
      "consequence": "..."
    }
  ],
  "vocabulary_gaps": [
    {
      "domain": "RTOS",
      "snippet": "...",
      "missing_signal": "xTaskCreateStatic not in router description",
      "router_output": "[]",
      "consequence": "..."
    }
  ],
  "over_classification_triggers": [
    {
      "snippet": "...",
      "domain_fired": "RTOS",
      "should_fire": false,
      "explanation": "..."
    }
  ],
  "cascading_failure_example": {
    "snippet": "...",
    "router_output": "[]",
    "fallback_experts_run": ["rtos_expert", "memory_expert", "hardware_expert"],
    "expert_skipped": "power_expert",
    "bug_missed": "PWR-001"
  },
  "injection_attacks": [
    {
      "strategy": "domain override",
      "payload": "...",
      "vulnerable": true,
      "explanation": "..."
    }
  ],
  "top_5_fixes": [
    {
      "rank": 1,
      "fix": "...",
      "addresses": "..."
    }
  ]
}
```

Prioritize findings by impact: a missed domain that silently skips a whole expert is
worse than a mis-labelled domain that fires the wrong expert.
