# MessagePack-C 0.5.7 provenance

These files come from the upstream `msgpack/msgpack-c` repository at commit
`4fa7cffc37da0a21b45898fe88d2a2da0b64d311` (`fixed sysdep.h`, 2011-08-09).
That commit is the immediate child of
`79b83e78a528b77216bae0bd756460d061227254` (`cpp: version 0.5.7`).  It is used
instead of the version commit itself because the shipped engine contains the
child commit's corrected byte-wise load macros; those macros mechanically make
the 3,733-byte `template_execute` body exact.

The original repository kept generated C template headers in top-level
`msgpack/` and copied them beside the public headers during preprocessing.
This vendored layout places that already-authored upstream header set under
`include/msgpack/` without modifying its contents.  The six translation units
proven present in the shipped engine and their required headers are included.
`include/msgpack/version.h` is the upstream generated header with the 0.5.7
version substitutions applied.

The C++ headers and `src/object.cpp` were mechanically extracted from the
official MessagePack 0.5.7 source distribution archived by Debian.  Its
SHA-256 is
`7c203265cf14a4723820e0fc7ac14bf4bad5578f7bc525e9835c70cd36e7d1b8`.
The distribution's `object.cpp` is byte-identical to upstream commit
`79b83e78a528b77216bae0bd756460d061227254`.  Compiling that unmodified source
with the pinned Android recipe produces the shipped object formatter and its
six unique emitted helper bodies exactly.

Upstream repository: <https://github.com/msgpack/msgpack-c>
Archived source distribution:
<https://archive.debian.org/debian/pool/main/m/msgpack/msgpack_0.5.7.orig.tar.gz>

## SHA-256

```text
17420f7cd150cd906ea8f88deb575be8539677cd2634a09615505e9bad2a7be1  src/objectc.c
4d1cd1e15d9f0c4d60d731db6b6bbabfd2754a854408251c79d16daac89ade84  src/unpack.c
e4582c2ec22f173e7d4a45063f02a794af2bf91be89f7e1d16a90c260527c09c  src/vrefbuffer.c
4edacf74d9ef62a8cc40e8075fb45f8cb92288d18267170a2194b9bdfb361e4d  src/zone.c
e344c2293f0adcab05a03c4e219b19feeccc9fed87e9c6d9e1ec12830546003d  src/version.c
ab988e2f9031cc64280f4038b7a27cd44044b84c499cb6fcb52c07ecc9975011  src/object.cpp
5bfa9c175c9946f92ebc9c309b1d401cd486556dfaa0d4fc793325af3e0fe739  include/msgpack.h
116114c06d064d69f506252ed968bc59ec2f992cfc1702cf77f59c284d62acfd  include/msgpack/object.h
74d2ab47dd0564eca48ae7a549fb4b7b0730854116d90ecea746fc2f3054f627  include/msgpack/pack.h
7977d89c71c4f6cfa1a9e7eeb0e7c5f9ecf1e9c212d4297aa7e83ee480dd034d  include/msgpack/pack_define.h
64f6fa7f3d4be2247ab678a5ffa1174df5efc14834d0e2d7b9429f6ab5f96c8f  include/msgpack/pack_template.h
86afaaaa423b09c084377204937c87d761f4de8e225eb8ef92bad059b8cf4f50  include/msgpack/sysdep.h
50b15350c266bbfb3fe619c1a7c62f4e481f8195da2a38e4b7057b707153a389  include/msgpack/sbuffer.h
74725cc4179037c810f16548d43b8926d15556b3dc3e171f6170fbbbcaaec7ce  include/msgpack/unpack.h
c838bee85a231d08a3e77d248887ce4f005e8e7629ed055af86c7334b384d327  include/msgpack/unpack_define.h
d75f44278d92aaafd193e308809fdc0151397eb5c6107fed9153bb7172a88be9  include/msgpack/unpack_template.h
aaec2691fbd0886d6a738aebd2fdeef36be32c0f20a3f0a85683d2bfbcbebeb0  include/msgpack/version.h
7fbb72a014b7f13d001fd5efcfd4c444e50896bf545a4821e02dbfc8072ccdfb  include/msgpack/vrefbuffer.h
e30752cac7f34917b1080721123a57fec76b12c363711ae03ba16509e9bed1f1  include/msgpack/zone.h
cfc7749b96f63bd31c3c42b5c471bf756814053e847c10f3eb003417bc523d30  LICENSE
f9c45f50daeb137aef0c7f471b326d9cdd2fb6e2ea1b3bd3910e079c58542303  NOTICE
```
