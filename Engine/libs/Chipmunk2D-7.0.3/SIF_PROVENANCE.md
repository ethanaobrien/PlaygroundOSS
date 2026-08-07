# Chipmunk source provenance

This is the official Chipmunk2D 7.0.3 tag at commit
`87340c216bf97554dc552371bbdecf283f7c540e`.  The observed GitHub tag archive
`Chipmunk-7.0.3.tar.gz` has SHA-256
`1e6f093812d6130e45bdf4cb80280cb3c93d1e1833d8cf989d554d7963b7899a`.

The shipped binary retains the `7.0.3` version literal. Android links Chipmunk
as a static dependency, so only referenced archive members survive in
`libGame.so`. The target's dynamic symbol table independently identifies the
retained implementation objects as `chipmunk`, `cpArbiter`, `cpArray`,
`cpBBTree`, `cpBody`, `cpCollision`, `cpHashSet`, `cpPolyShape`, `cpRobust`,
`cpShape`, `cpSpace`, `cpSpaceComponent`, `cpSpaceHash`, `cpSpaceStep`, and
`cpSpatialIndex`.

No target dynamic symbol names any API from `cpConstraint`, the joint/spring
objects, `cpMarch`, `cpPolyline`, `cpSpaceDebug`, `cpSpaceQuery`, or
`cpSweep1D`. The engine has no reference to their public entry points, and the
target retains none of their hard-assertion strings or callback tables. These
unreferenced objects, together with the platform-specific `cpHastySpace`, are
therefore intentionally outside the binary census. Scanning them produces
demonstrably accidental identities: for example, pristine
`cpPivotJointAlloc` collides with retained `cpCircleShapeAlloc`, while
`cpSweep1DNew` collides with retained `cpBBTreeNew`.

The shipped static library is compiled with `NDEBUG`. Target setters such as
`cpBodySetTorque` retain activation and the typed field store but omit
`cpAssertSaneBody`; applying that coherent release configuration adds 28
distinct exact bodies without losing a previous exact range.

The application carries two small source-level changes from the upstream tag.
`cpMessage` prints only the warning/error prefix and initializes its variadic
argument list; it omits the upstream message, condition, source-location, and
Android-log output. Its retained dynamic symbol is uniquely byte-exact with
that implementation. `cpSpaceAddShape` also requires a non-null shape body and
requires that body to have already been added to the same space. The target's
retained assertion strings and control flow independently prove both contracts.

The retained `cpPolyShape.c` additionally includes official upstream commit
`bbd6d1eae1803b802cc020997e4effe8be27a21b` from 2019-06-10. Its
`cpPolyShapeSegmentQuery` clamps the positive denominator with `CPFLOAT_MIN`
to avoid division by zero. The resulting function is byte-exact, and the real
added source line also makes all five later assertion-bearing accessors exact.

Two official post-tag `cpCollision.c` fixes are retained as well. Commit
`bc09ed54eca2605c3bdd4d54c983eade456e60ff` corrects the circle/poly contact
normal sign, and commit `e7ea51e7b8d4987f63c2126550f4267d66b2287c`
adds `CPFLOAT_MIN` to the `ClosestT` denominator. Together they make
`CircleToPoly`, `EPARecurse`, and `GJKRecurse` byte-exact.

Chipmunk uses the complete NDK r11c x86-64 GCC release recipe, including
`-fomit-frame-pointer`, `-fstrict-aliasing`, `-funswitch-loops`, and
`-finline-limit=300`. The inline limit is observable: it makes `GJK` exact and
also resolves every residual body in `cpArbiter`, `cpBBTree`, and
`cpSpaceComponent`. With those flags and the three official post-tag fixes,
every emitted function in all retained Chipmunk objects is either uniquely or
ambiguously byte-exact; no retained source body remains non-exact.
