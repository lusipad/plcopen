# R1 72h RT Report Template

This template is a placeholder for real hardware evidence. Do not fill it with simulated data.

Harness (A6): `plcopen_core_jitter_harness --seconds 259200` on the tuned
PREEMPT_RT target (isolcpus, IRQ affinity, C-states off; the tool attempts
mlockall + SCHED_FIFO itself). It drives the DoD load (1 ms period, 8
coordinated axes + 32 single axes) and prints the wake-latency percentiles
below plus a per-cycle work-time distribution. Run
`plcopen_core_a2_alloc_guard --cycles 259200000` alongside for the
allocation-guard row.

## Hardware

- CPU / board:
- RAM:
- Storage:
- Servo / fieldbus hardware:

## Kernel And Runtime

- OS:
- Kernel:
- PREEMPT_RT patch level:
- CPU isolation / governor settings:
- Cycle period:

## Build

- Commit:
- Compiler:
- CMake options:
- Core benchmark output:

## 72h Result

- Start:
- End:
- Total cycles:
- Missed cycles:
- p50 jitter:
- p99 jitter:
- p99.999 jitter:
- Max jitter:
- Allocation guard violations:

## Verdict

- Pass / fail:
- Notes:
