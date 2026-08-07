# Tremolo provenance

The x86-64 SIF target uses the Android Tremor source at commit
`0ad0141e864cfbb130db4f22e279d421832e7336` from
`https://android.googlesource.com/platform/external/tremor`.

This revision was selected from target evidence, not version-string
similarity. Its natural `codebook` field order produces the shipped
0x58-byte layout, and all 13 functions emitted by `codebook.c` match the
target mechanically. Rebuilding the remaining common C translation units
from the same revision likewise produces exact bodies for every emitted
function. Android/ARM assembly files retained from the OSS tree are outside
the x86-64 census.
