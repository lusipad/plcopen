# L0 rt

`core/rt` is the realtime foundation for the rewrite core.

Responsibilities:

- integer cycle ticks and durations;
- fixed-capacity containers required by R1;
- single-producer / single-consumer queues;
- small error-code based `Result` values.

RT constraints:

- no heap allocation after construction;
- no blocking locks;
- no exceptions or RTTI;
- no OS calls or wall-clock reads;
- no floating-point time accumulation.

Dependency rule: L0 depends only on the C++17 standard library pieces that do not allocate
for the provided types. Higher layers may depend on L0; L0 must not depend on PLCopen FB
semantics, old `src/`, or adapters.
