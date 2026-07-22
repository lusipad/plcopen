# Central registry submission assets

The repository-owned Conan recipe and vcpkg overlay are usable and tested in
this repository. Central registry inclusion is a separate external review:

- ConanCenter submission tree: `packaging/conan-center/recipes/plcopen/`
- vcpkg submission port: `ports/plcopen/`

Before submission, copy each tree into a current checkout of the corresponding
upstream index, run its official lint/version/test commands, and review the
generated diff. Project policy treats account use, signatures, and external
commitments as human-only, so these files are submission-ready inputs rather
than a claim that either upstream has accepted the package.

The source archive and hashes are pinned to `v0.20.0`. Do not silently retarget
them to `main`; a new release needs a new version entry and freshly verified
hashes.
