#ifndef PLAYGROUND_RUNTIME_ASSERTION_DIAGNOSTICS_H
#define PLAYGROUND_RUNTIME_ASSERTION_DIAGNOSTICS_H

#include "KLBBase64.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <iterator>
#include <string>

namespace playground::runtime {

inline std::string readableAssertionMessage(const char *message) {
  if (!message || !message[0])
    return {};

  const std::size_t length = std::strlen(message);
  if (length < 8 || !std::all_of(message, message + length, [](char value) {
        const unsigned char byte = static_cast<unsigned char>(value);
        return std::isalnum(byte) || value == '+' || value == '/' ||
               value == '=';
      }))
    return message;

  std::string decoded(static_cast<std::size_t>(Base64decode_len(message)),
                      '\0');
  const int decodedLength = Base64decode(decoded.data(), message);
  if (decodedLength <= 0)
    return message;
  decoded.resize(std::min<std::size_t>(
      static_cast<std::size_t>(decodedLength), decoded.find('\0')));

  static constexpr const char *LuaPrefixes[]{
      "runtime error:", "memory allocation error:", "error in error:",
      "unknown error:"};
  const bool luaError = std::any_of(
      std::begin(LuaPrefixes), std::end(LuaPrefixes),
      [&decoded](const char *prefix) { return decoded.rfind(prefix, 0) == 0; });
  return luaError ? decoded : std::string(message);
}

} // namespace playground::runtime

#endif
