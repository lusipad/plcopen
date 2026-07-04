# ADR-0002: Keep the Rewrite Core on C++17

## Context

The v0.x library and downstream consumer checks already target C++17. R1 starts the rewrite core
inside the same repository while the old `src/` line remains buildable and frozen. A C++20-only
core would split the toolchain story before R1 has proved the new RT and OTG foundation.

## Decision

Use C++17 as the long-term language standard for the rewrite core. Core code may still use templates
and compile-time checks, but it must not require C++20 concepts, `std::span`, designated
initializers, or C++20-only library facilities.

## Consequences

C++17 keeps the old line, new core, examples, and consumer packages on one compiler contract. The
cost is slightly more manual interface checks in L0-L4. That cost is accepted for R1 because the
core APIs are still small.

## Rejected

- C++20 for `core/` only: creates two public toolchain stories in one repository before v1.0-alpha.
- Full repository C++20: disturbs the frozen v0.x line for no R1-critical gain.

## Verification

`core/.clang-format` and `plcopen::core` use C++17. Core tests compile with exceptions and RTTI
disabled, proving the R1 foundation does not rely on newer language machinery.
