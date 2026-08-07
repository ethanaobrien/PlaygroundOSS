# Curl source provenance

This is the canonical Curl 7.60.0 release from
`https://curl.se/download/curl-7.60.0.tar.xz`, SHA-256
`8736ff8ded89ddf7e926eec7b16f82597d029fc1469f3a551f1fafaac164e6a0`.
The corresponding `curl-7_60_0` tag resolves to commit
`cb013830383f1ccc9757aba36bc32df5ec281c02`.

The target's `curl_version` constructs `libcurl/7.60.0`.  Its protocol and
feature table proves a no-TLS, zlib-enabled Android build with LDAP and LDAPS
disabled.  `config/x86_64-android/curl_config.h` was generated out of tree with
NDK r11c GCC 4.9 using:

```
configure --host=x86_64-linux-android --disable-shared --enable-static \
  --without-ssl --with-zlib --disable-ldap --disable-ldaps \
  --disable-threaded-resolver
```

The shipped x86-64 engine deliberately reuses the 32-bit x86 configuration:
its retained Curl OS string is `i686-pc-linux-android`, and target
`Curl_timediff`, send/receive wrappers, and size conversions independently use
the header's 32-bit `time_t`, `long`, `off_t`, `size_t`, and return-type
configuration. Compiling x86-64 code against that coherent generated header
adds 21 distinct exact bodies without losing a previous exact range. The final
header's hash is pinned by the workspace source-quality gate.

The shipped `curl_version_info_data.protocols` array contains only `http`
followed by its null terminator. The engine's IMAP, SMTP, and POP3 strings are
part of its own URL-routing wrapper, while the retained FTP diagnostics come
from Curl's protocol-independent error table. The generated configuration
therefore disables every non-HTTP protocol rather than compiling unused
protocol handlers into the static library.

The target uses Curl's synchronous IPv6-capable resolver rather than the
POSIX threaded resolver. Its exported `Curl_getaddrinfo` is the synchronous
`hostip6.c` implementation, while the threaded `Curl_resolver_*` surface is
absent. Leaving `HAVE_PTHREAD_H` intact but undefining `USE_THREADS_POSIX`
also removes the 40-byte `Curl_async` member from `connectdata`, placing the
connection-bundle pointer at the shipped offset.
