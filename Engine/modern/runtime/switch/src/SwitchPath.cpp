#include "Playground/Switch/SwitchPath.h"

#include <algorithm>
#include <cstdint>
#include <set>

namespace playground::switch_runtime {
namespace {

constexpr std::size_t ComponentLimit = 64;
constexpr std::size_t PhysicalPathLimit = 0x300;
constexpr std::size_t MarkerLength = 4;
constexpr std::size_t PayloadLimit = ComponentLimit - MarkerLength;
constexpr const char *ReservedPrefix = "~pg";
constexpr const char *SingleMarker = "~pgs";
constexpr const char *BeginMarker = "~pgb";
constexpr const char *ContinueMarker = "~pgc";
constexpr const char *EndMarker = "~pge";
constexpr char Base64Alphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

bool hasPrefix(const std::string &value, const char *prefix) {
  return value.rfind(prefix, 0) == 0;
}

std::string base64UrlEncode(const std::string &input) {
  std::string output;
  output.reserve((input.size() * 4 + 2) / 3);
  std::uint32_t accumulator = 0;
  unsigned bits = 0;
  for (unsigned char value : input) {
    accumulator = (accumulator << 8) | value;
    bits += 8;
    while (bits >= 6) {
      bits -= 6;
      output.push_back(Base64Alphabet[(accumulator >> bits) & 0x3f]);
    }
  }
  if (bits)
    output.push_back(Base64Alphabet[(accumulator << (6 - bits)) & 0x3f]);
  return output;
}

int base64Value(unsigned char value) {
  if (value >= 'A' && value <= 'Z')
    return value - 'A';
  if (value >= 'a' && value <= 'z')
    return value - 'a' + 26;
  if (value >= '0' && value <= '9')
    return value - '0' + 52;
  if (value == '-')
    return 62;
  if (value == '_')
    return 63;
  return -1;
}

bool base64UrlDecode(const std::string &input, std::string &output) {
  output.clear();
  std::uint32_t accumulator = 0;
  unsigned bits = 0;
  for (unsigned char value : input) {
    const int decoded = base64Value(value);
    if (decoded < 0)
      return false;
    accumulator = (accumulator << 6) | static_cast<unsigned>(decoded);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      output.push_back(static_cast<char>((accumulator >> bits) & 0xff));
    }
  }
  if ((bits && (accumulator & ((1u << bits) - 1u))) ||
      base64UrlEncode(output) != input) {
    output.clear();
    return false;
  }
  return true;
}

bool validUtf8(const std::string &value) {
  const auto *bytes = reinterpret_cast<const unsigned char *>(value.data());
  std::size_t index = 0;
  while (index < value.size()) {
    const unsigned char lead = bytes[index++];
    if (lead < 0x80) {
      if (!lead)
        return false;
      continue;
    }
    unsigned remaining = 0;
    std::uint32_t code = 0;
    if ((lead & 0xe0) == 0xc0) {
      remaining = 1;
      code = lead & 0x1f;
    } else if ((lead & 0xf0) == 0xe0) {
      remaining = 2;
      code = lead & 0x0f;
    } else if ((lead & 0xf8) == 0xf0) {
      remaining = 3;
      code = lead & 0x07;
    } else {
      return false;
    }
    if (index + remaining > value.size())
      return false;
    for (unsigned count = 0; count < remaining; ++count) {
      const unsigned char continuation = bytes[index++];
      if ((continuation & 0xc0) != 0x80)
        return false;
      code = (code << 6) | (continuation & 0x3f);
    }
    if ((remaining == 1 && code < 0x80) ||
        (remaining == 2 && code < 0x800) ||
        (remaining == 3 && code < 0x10000) || code > 0x10ffff ||
        (code >= 0xd800 && code <= 0xdfff))
      return false;
  }
  return true;
}

std::vector<std::string> encodeComponent(const std::string &component) {
  if (component.size() <= ComponentLimit &&
      !hasPrefix(component, ReservedPrefix))
    return {component};

  const std::string payload = base64UrlEncode(component);
  if (payload.size() <= PayloadLimit)
    return {std::string(SingleMarker) + payload};

  std::vector<std::string> result;
  std::size_t offset = 0;
  result.push_back(std::string(BeginMarker) +
                   payload.substr(offset, PayloadLimit));
  offset += PayloadLimit;
  while (payload.size() - offset > PayloadLimit) {
    result.push_back(std::string(ContinueMarker) +
                     payload.substr(offset, PayloadLimit));
    offset += PayloadLimit;
  }
  result.push_back(std::string(EndMarker) + payload.substr(offset));
  return result;
}

bool decodeLogicalName(const std::string &payload, std::string &logical) {
  if (!base64UrlDecode(payload, logical) || logical.empty() ||
      logical.find('/') != std::string::npos || !validUtf8(logical))
    return false;
  // Canonical encoding prevents a second physical spelling for an ordinary
  // short component.
  return logical.size() > ComponentLimit ||
         hasPrefix(logical, ReservedPrefix);
}

bool collectContinuation(const std::filesystem::path &directory,
                         const std::string &payload, unsigned depth,
                         std::set<std::string> &names, std::string &error) {
  if (depth > 32 || payload.size() > PhysicalPathLimit) {
    error = "Encoded Switch storage component exceeds its safety bound";
    return false;
  }
  std::error_code fsError;
  bool foundContinuation = false;
  for (const auto &entry :
       std::filesystem::directory_iterator(directory, fsError)) {
    if (fsError)
      break;
    const std::string component = entry.path().filename().string();
    if (hasPrefix(component, ContinueMarker)) {
      foundContinuation = true;
      if (!collectContinuation(entry.path(),
                               payload + component.substr(MarkerLength),
                               depth + 1, names, error))
        return false;
    } else if (hasPrefix(component, EndMarker)) {
      foundContinuation = true;
      std::string logical;
      if (!decodeLogicalName(payload + component.substr(MarkerLength),
                             logical)) {
        error = "Switch storage contains a malformed encoded component";
        return false;
      }
      names.insert(std::move(logical));
    } else {
      error = "Switch storage encoding continuation contains an unexpected "
              "entry";
      return false;
    }
  }
  if (fsError) {
    error = "Could not enumerate encoded Switch storage: " +
            fsError.message();
    return false;
  }
  // An empty spine can remain after rename/removal. It has no logical child
  // and is safe to hide; a non-empty malformed spine fails above.
  (void)foundContinuation;
  return true;
}

} // namespace

std::filesystem::path
encodeStoragePath(const std::filesystem::path &logicalPath) {
  const std::string input = logicalPath.generic_string();
  std::string output;
  output.reserve(input.size());
  std::size_t start = 0;
  while (start <= input.size()) {
    const std::size_t separator = input.find('/', start);
    const std::size_t end =
        separator == std::string::npos ? input.size() : separator;
    const auto encoded = encodeComponent(input.substr(start, end - start));
    for (std::size_t index = 0; index < encoded.size(); ++index) {
      if (index)
        output.push_back('/');
      output += encoded[index];
    }
    if (separator == std::string::npos)
      break;
    output.push_back('/');
    start = separator + 1;
  }
  if (output.size() > PhysicalPathLimit)
    return {};
  return std::filesystem::path(output);
}

bool listStorageDirectory(const std::filesystem::path &physicalDirectory,
                          std::vector<std::string> &logicalNames,
                          std::string &error) {
  logicalNames.clear();
  std::set<std::string> names;
  std::error_code fsError;
  for (const auto &entry :
       std::filesystem::directory_iterator(physicalDirectory, fsError)) {
    if (fsError)
      break;
    const std::string component = entry.path().filename().string();
    if (hasPrefix(component, SingleMarker)) {
      std::string logical;
      if (!decodeLogicalName(component.substr(MarkerLength), logical)) {
        error = "Switch storage contains a malformed encoded component";
        return false;
      }
      names.insert(std::move(logical));
    } else if (hasPrefix(component, BeginMarker)) {
      if (!collectContinuation(entry.path(),
                               component.substr(MarkerLength), 1, names,
                               error))
        return false;
    } else if (hasPrefix(component, ReservedPrefix)) {
      error = "Switch storage contains an unknown reserved component";
      return false;
    } else {
      names.insert(component);
    }
  }
  if (fsError) {
    error = "Could not enumerate Switch storage: " + fsError.message();
    return false;
  }
  logicalNames.assign(names.begin(), names.end());
  return true;
}

} // namespace playground::switch_runtime
