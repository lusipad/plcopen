<title>ST runtime reference</title>

# ST runtime

plcopen's IEC 61131-3 Structured Text (ST) support is not an external
source-to-C++ tool. It is a complete outer consumer surface: source is
compiled to bytecode during loading, while scan cycles execute only a
validated program and its static memory image.

## Where to start

- To run a first program, use the [ST quick start](../getting-started/st.md).
- To look up `MC_*` inputs and outputs, use the
  [function-block reference](fb-reference.md).
- To decide whether a language feature is closed, follow the canonical
  feature ledger and semantic matrices below.

## Execution model

```text
ST source
  └─ load time: lex → parse → type-check → bytecode and static memory
       └─ scan time: task schedule → deterministic VM → C++ FB bindings
```

Loading may construct programs and diagnostics. Scan-time execution follows
bounded, exception-free, zero-dynamic-allocation rules. The user's executor
chooses whether an ST task runs in lockstep with motion or at a lower rate.
The ST surface consumes public function blocks and real-time error contracts;
the production kernel never depends back on the language runtime.

## Capability ladder

| Level | Closed scope |
|---|---|
| L0 | Front end, control flow, deterministic VM, and basic function blocks |
| L1 | Scalars, conversions, enums, subranges, arrays, structs, strings, and date/time |
| L2 | User POUs, parameters/scopes, axis and group references, complete standard-FB binding |
| L3 | Located variables, process images, RETAIN/PERSISTENT, force, and snapshots |
| L4 | Standard functions and basic IEC function blocks |
| L5 | Configurations, resources, multitasking, watchdogs, and recovery |
| L6 | Textual SFC and action qualifiers |
| L7 | Monitoring, breakpoints, stepping, and trace |

L0–L7 and the cross-cutting verification ledger are closed with machine
status `pending=0`. That does not include graphical LD/FBD editors, online
change, the deprecated IL language, system I/O, or legacy-format
compatibility.

## How to evaluate an ST claim

1. Find the feature's level in the ledger and check explicit exclusions.
2. Read its semantic matrix for conversion, storage, diagnostics, and
   rejection behavior.
3. Check the machine sets and test anchors. An approved design does not, by
   itself, mean an implemented feature.

This order keeps language coverage, function-block binding, and real-hardware
behavior from being collapsed into one claim.

## Canonical sources

- The [ST runtime design](https://github.com/lusipad/plcopen/blob/main/doc/design/core/st-runtime-design.md)
  records compiler, VM, task, and architecture choices.
- The [ST L0–L7 feature ledger](https://github.com/lusipad/plcopen/blob/main/doc/compliance/st-feature-table.md)
  is the human-readable current capability entry point.
- The [L0 semantic matrix](https://github.com/lusipad/plcopen/blob/main/doc/compliance/st-l0-semantics.md)
  defines the front-end and VM foundation; later-level matrices live beside
  it in `doc/compliance/`.
