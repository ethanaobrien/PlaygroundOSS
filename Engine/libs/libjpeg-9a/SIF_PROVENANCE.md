# IJG libjpeg 9a provenance

- Upstream: `https://www.ijg.org/files/jpegsrc.v9a.tar.gz`
- Release: `9a`, 2014-01-19
- Archive SHA-256:
  `c133068c1adebed7c36b95445fca78cf74849cd3fed0d05c5d211c8325b22740`
- Imported content: the 46 library translation units selected by upstream
  `LIBSOURCES` with `jmemnobs.c`, their internal/public headers, configuration
  templates, README distribution terms, change log, and coding rules. Command
  line programs and format adapters are not part of the shipped static block
  and are omitted so the engine's recursive Android source discovery remains
  correct.

The target retains upstream `JVERSION` (`9a  19-Jan-2014`), `JCOPYRIGHT`, the
canonical IJG error table, and the `JPEGMEM` memory-manager diagnostic. Its
352-function range is `0x80890..0xb1be2`, immediately between the Tremolo and
libpng blocks, and `libjpeg` is absent from `DT_NEEDED`, proving this is linked
code rather than a system dependency.

The shipped x86-64 objects use Android NDK r11c Clang 3.8 at `-O2` and define
IJG `boolean` as `unsigned char`. With the default enum representation, 260 of
352 functions miss. The typed one-byte configuration in `jconfig.h` makes all
352 emitted functions byte-identical to target bodies with zero misses. It is
an ABI declaration recovered from the complete object family, not an
alignment or packing aid.

The IJG distribution conditions are preserved verbatim in `README` and source
notices are unchanged.
