#ifndef PLAYGROUND_CURL_TRUST_H
#define PLAYGROUND_CURL_TRUST_H

#if !defined(PLAYGROUND_WEB)
#include "curl.h"

static inline CURLcode playgroundConfigureCurlTrust(CURL *curl) {
  if (!curl)
    return CURLE_FAILED_INIT;
#if defined(PLAYGROUND_CURL_CA_BUNDLE)
  CURLcode result =
      curl_easy_setopt(curl, CURLOPT_CAINFO, PLAYGROUND_CURL_CA_BUNDLE);
  // devkitPro's Wii U libcurl is configured with CafeOS's internal scerts
  // directory as its default CAPATH. Homebrew cannot read that title-owned
  // directory, and mbedTLS treats failure to load either configured trust
  // source as fatal even when CAINFO is valid. Select the bundled Mozilla PEM
  // exclusively.
  if (result == CURLE_OK)
    result = curl_easy_setopt(curl, CURLOPT_CAPATH, nullptr);
  if (result == CURLE_OK)
    result = curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
  if (result == CURLE_OK)
    result = curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
  return result;
#else
  return CURLE_OK;
#endif
}
#endif

#endif
