# L4 exec

`core/exec` owns RT-side sampling of an already committed path.

R2 v1 scope:

- fixed-capacity committed path storage;
- O(number-of-segments) bounded sampling over a small fixed capacity;
- scalar OTG profile position mapped onto L2 geometry.

The first sampler intentionally does not implement gear/cam/overlay primitives yet. Those enter a
later R2 slice after the path sampling contract is stable.
