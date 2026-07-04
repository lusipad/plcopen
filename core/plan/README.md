# L3 plan

`core/plan` owns planning-domain path buffers and conservative planning decisions.

R2 v1 scope:

- fixed-capacity path buffer;
- monotonic front consumption;
- bounded look-ahead speed pass over the current buffer;
- blending decisions that produce a bounded quadratic Bezier curve constrained by tolerance.

R2 v1 keeps the curve insertion policy explicit: the caller receives the blend segment and chooses
where to splice it into a committed path. The planner does not mutate source buffers in place.
