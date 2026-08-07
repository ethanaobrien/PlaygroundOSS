# FreeType source provenance

`src/` is the pristine source tree from FreeType 2.4.4.  It was restored from
the canonical `freetype-2.4.4.tar.bz2` release archive, SHA-256
`4b8281c7dc4d375c6b65d3c6f4808e488a313fab47d7be82aad2c871c8480fef`.

The pre-existing headers and Android module configuration in this directory
already identify version 2.4.4.  Target `FT_New_Library` independently stores
version 2.4.4, and all 575 emitted functions from the ten retained Android
translation units match the shipped x86-64 binary exactly.
