# L4 exec

`core/exec` owns RT-side sampling of an already committed path.

R2 v1 scope:

- fixed-capacity committed path storage;
- O(number-of-segments) bounded sampling over a small fixed capacity;
- scalar OTG profile position mapped onto L2 geometry.
- minimal gear, cam, and overlay primitives needed by later PLCopen semantic migration.

Gear and cam are pure value mappings. Overlay is vector addition on sampled path output; state and
command lifecycle stay out of L4 until R3.
