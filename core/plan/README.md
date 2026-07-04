# L3 plan

`core/plan` owns planning-domain path buffers and conservative planning decisions.

R2 v1 scope:

- fixed-capacity path buffer;
- monotonic front consumption;
- bounded look-ahead speed pass over the current buffer;
- blending decision metadata constrained by tolerance.

R2 v1 does not rewrite path geometry for blending yet. It records the allowed blend radius and keeps
the original path unless a later slice commits a curve insertion policy.
