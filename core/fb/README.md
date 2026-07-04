# core/fb

`core/fb` is the R3 L6 PLCopen function-block contract layer.

The first migration keeps the code small: shared Execute/Enable lifecycle latches and basic IEC
blocks live here, while the first single-axis and group motion facades bind to the `core/axis`
command contract through `motion.h`.

Non-goals:

- No one-class-per-legacy-FB copy until the public v1 facade is switched in R4.
- No new behavior beyond the existing compliance matrix and known-boundary IDs.
