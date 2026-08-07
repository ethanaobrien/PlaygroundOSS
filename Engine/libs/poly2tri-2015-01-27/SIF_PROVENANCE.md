# poly2tri source provenance

This tree is the upstream poly2tri source at commit
`f52c0c1c0dd2d3722b3dfa2c7f722c4eac32087e` (2015-01-27). The exact commit is
identified independently by the shipped `EdgeEvent - collinear points not
supported` diagnostic and byte-exact bodies from all five library translation
units in the target's contiguous `p2t` code block.

Android NDK r11c's STLport does not provide the `std::runtime_error(const
char*)` overload assumed by upstream. The three runtime-error constructions use
an explicit `std::string`, which preserves the intended upstream behavior and
allows the source to compile against the shipped STL. This compatibility edit
does not serve as evidence for accepting any non-exact function.

The target library uses the engine's Clang 3.8 release C++ recipe with RTTI
disabled and `NDEBUG` enabled for these files. Generated objects and scan
reports live under `/home/ethan/work/tooling` and are not part of this source
tree.

The five implementation files are compiled through `poly2tri_unity.cc` as one
translation unit. This is mechanically distinguished from five separate
objects by cross-file frontend inlining in the shipped code: for example,
`SweepContext::CreateAdvancingFront` contains the body of the out-of-line
`Triangle` constructor while that constructor still has its own FDE. The unity
boundary reproduces those decisions across the library without merging or
obscuring the upstream implementation files.

The shipped release fallback in `Triangle::Index` returns point index zero
after its disabled assertion, rather than upstream's `-1`. The surrounding
three pointer comparisons and successful return values are otherwise
instruction-identical to upstream.

The shipped `AdvancingFront::LocateNode` retains the earlier upstream
`const double&` parameter ABI. Its traversal body is otherwise identical to
the 2015 source. The pointer load at the target function entry and callers
passing the address of a point coordinate independently prove this signature.

`SweepContext::GetTriangles` and `GetMap` return their containers by value in
the shipped library. Their hidden-return calling convention and copies from
the typed `triangles_` and `map_` members are present both in the context
methods and in the forwarding `CDT` methods.

The game-facing headers additionally expose an inline const-reference triangle
accessor while retaining those four out-of-line value-return bodies. Shipped
`CKLBUIPolygon::commandUI` directly reaches the typed `SweepContext`
`triangles_` vector through its `CDT` instance and copies it locally, with no
call to either value-return accessor. The separate exact library bodies and
the inlined game call jointly prove this header/library extension.

The game-facing `CDT` header also exposes its two internals through inline
accessors. `CDT::Triangulate` is present in the shipped library as its own
out-of-line 12-byte forwarding body in `cdt.cc` source order, between
`AddPoint` and `GetTriangles`, and it has no callers anywhere in the target.
Shipped `CKLBUIPolygon::commandUI` instead loads `sweep_context_` and `sweep_`
from its `CDT` instance and calls `Sweep::Triangulate` directly. An out-of-line
definition in another translation unit cannot be inlined, so the game reaches
those members through the header rather than through `CDT::Triangulate`. The
accessors are declaration-only and change no Poly2Tri body.

`SweepContext::GetPoint` retains the earlier signed-index reference ABI. The
target loads a 32-bit index through the argument pointer, sign-extends it, and
indexes the `points_` vector directly.

The shipped context/CDT container-taking APIs use the earlier by-value vector
signatures for construction, hole insertion, edge initialization, and
advancing-front creation. Their target bodies include the corresponding owned
parameter cleanup, distinguishing them from the later const-reference API.

The nested `SweepContext::Basin` and `EdgeEvent` types retain their upstream
typed default constructors. The context constructor then performs the
historical aggregate reset before copying the input polyline. This exact
source form reproduces the shipped 603-byte constructor, including its
exception cleanup paths, without explicit store sequencing.

`SweepContext::InitEdges` retains the earlier signed `int` loop variables.
The target truncates the by-value vector length to 32 bits and uses signed
comparisons for the wraparound index; the complete 382-byte body is exact.

The shipped `Point` layout has naturally pointer-sized client data between its
coordinates and edge vector. Point allocations are 0x30 bytes and constructor
stores place the vector header at +0x18 while deliberately leaving the client
data available to the caller. `CKLBUIPolyline2` uses it as an identity pointer.
`CKLBUIPolygon` independently proves the alternate payload: a 32-bit color at
+0x10 and a boolean color-override flag at +0x14. A named union represents both
uses without changing the natural size, alignment, or Poly2Tri-owned behavior.

`SweepContext::point_count` and `Sweep::SweepPoints` retain a signed 32-bit
count. The target truncates the vector length to 32 bits and uses a signed loop
comparison, unlike the later upstream `size_t` form.

For a collinear flip, the shipped `Sweep::NextFlipPoint` returns the supplied
edge point rather than constructing and throwing `std::runtime_error`. The
target's collinear branch directly moves that typed argument to the return
register; this also makes both callers' control flow exact.
