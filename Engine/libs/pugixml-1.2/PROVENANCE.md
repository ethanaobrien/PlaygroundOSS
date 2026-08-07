# PugiXML 1.2 provenance

- Upstream carrier: `https://github.com/damucz/timelinefx.git`
- Carrier commit: `eac48367e264b48462b8f69cd9a44a4aebcb27c0`
- First carrier commit: `ba1f3115198054d8eafb10317524417bf7870868`
- Upstream subtree: `pugixml`
- Git tree: `3241d3b07f17ae15bc3aec090d568edea22fdf4c`
- `pugixml.cpp` SHA-256:
  `2dd68d40c97727429071f6b58aec47744d80a31622c52ebd096802eec16aeeab`
- `pugixml.hpp` SHA-256:
  `46a62db7cb5e359de9505c7a9a7f2700fb9beeb40f7dfeb52a7632e6082cf40a`
- `pugiconfig.hpp` SHA-256:
  `457a1c6a478894d88b0848fa6fdb1f66a7cf63796db7372b76d9f380d9e65631`

The source identifies itself as PugiXML 1.2 (`PUGIXML_VERSION 120`) and
retains its complete MIT license notice in `src/pugixml.cpp`. Target RTTI,
TimelineFX XML-loader behavior, and the coherent exact-function block jointly
anchor this bundled version.

The shipped object uses pugixml's release assertion policy (`NDEBUG`). With
that coherent configuration 71 additional globally unique target bodies are
exact and no previously exact body is lost; the remaining source mismatches
fall from 102 to 11.
