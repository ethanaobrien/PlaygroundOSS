# TimelineFX provenance

- Upstream: `https://github.com/damucz/timelinefx.git`
- Commit: `eac48367e264b48462b8f69cd9a44a4aebcb27c0`
- Commit date: 2016-10-30
- Upstream source tree: `timelinefx/source`
- Git tree: `f7158192254f050736b7f587e25a2d982dcbbc5a`
- Imported content: the 13 C++ translation units and their headers, plus the
  upstream README. Samples and Marmalade build metadata are omitted.

`source/PlaygroundParticle.cpp` and its header are target-specific adapter
sources, not part of the upstream tree. The leaked path, retained
`KLBInnerParticleAsset` RTTI/vtable, and exact PugiXML loader factory anchor
their identity. The engine-image factory remains abstract until its typed
asset ownership is recovered.

The target retains TimelineFX RTTI for `PugiXMLLoader`, `EffectsLibrary`,
`ParticleManager`, `Effect`, `Entity`, `Emitter`, `Particle`, `AnimImage`, and
`XMLLoader`, as well as the `SUPER_EFFECT` and super-effect diagnostics. It
also leaks `jni/libs/timelinefx/source/PlaygroundParticle.cpp`, independently
anchoring the bundled library family.

The upstream repository contains no separate TimelineFX license file at this
commit. No notice was removed from the imported sources; the upstream README
is retained verbatim.

Because the shipped library is a KLab-adapted fork, automatic exact-match
admission is restricted to its coherent target block. Larger nonmatching
methods are not treated as reconstructed engine behavior.

The bundled library uses the upstream release assertion policy (`NDEBUG`).
The target implementations of `EmitterArray::Compile` and `SetCompiled`
independently retain their typed work while omitting the debug bounds checks;
this configuration adds exact bodies without losing any prior exact range.

The shipped KLab fork also replaces the virtual `Entity::Update` slot with a
natural Itanium pointer-to-member update handler stored per entity. Entity,
Effect, Emitter, and Particle constructors install their respective typed
handlers, and child traversal dispatches through that member. RTTI vtables,
constructor vptr references, the continuous Entity function block, and typed
field accesses independently establish the resulting three-slot virtual ABI
and natural 0x1e8-byte Entity layout. Existing tween-capture fields move to a
tail group, alpha precedes dimensions, and `_timediff` follows the lifetime
field while the obsolete `_oldAge` field is absent. Unnamed derived-class
state remains deliberately unrecovered.
