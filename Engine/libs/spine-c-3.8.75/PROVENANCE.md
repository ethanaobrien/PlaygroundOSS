# Spine C 3.8.75-era provenance

- Upstream: `https://github.com/EsotericSoftware/spine-runtimes.git`
- Target data version: `3.8.75`
- Commit: `c0f101969d486a9f5f94e91d33a98af1c700a4dc`
- Commit date: 2020-04-13
- Upstream subtree: `spine-c/spine-c`
- Git tree: `d060730729f583b5f385a73e71a49342adcffe6d`
- Imported content: the `include/` and `src/` runtime trees plus the adjacent
  Spine Runtimes license; examples, tests, and build-project metadata are not
  target link inputs and are omitted.

The shipped x86-64 `libGame.so` leaks all 39 paths under
`jni/libs/spine-c/src/spine/*.c` and its binary loader checks the exact
`3.8.75` data-version literal. Spine did not publish a tag with that name, so
the source is pinned to a coherent commit rather than described as a release
tag. A complete historical-tree census selects this commit as the latest
matching C runtime before the 2020-04-15 AnimationState change: all 529 emitted
source identities are byte-exact (365 globally unique, 164 ambiguous, zero
misses). The former 3.8.95-tag tree emitted 55 misses and 53 fewer distinct
exact target bodies.

The target recipe is Android NDK r11c Clang 3.8 with the engine's ordinary C
flags and this runtime's `include/` directory. Headers and sources are kept as
one commit-pinned tree; individual historical fixes are not cherry-picked.
Every admitted ambiguous body must still pass the normal identity gates.
