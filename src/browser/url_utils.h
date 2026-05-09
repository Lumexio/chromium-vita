#pragma once

#include <string>

namespace browser {

// Returns true if the URL starts with "http://" or "https://".
bool starts_with_http(const std::string& url);

// Normalises a raw URL entered by the user:
//   - Trims surrounding whitespace.
//   - Empty input  → returns "".
//   - Contains spaces → DuckDuckGo search URL with percent-encoded query.
//   - Already has http(s) scheme → returned unchanged.
//   - Otherwise → prepends "https://".
std::string normalize_url(const std::string& raw_url);

// Percent-encodes a query component (RFC 3986 unreserved + '+' for space).
std::string encode_query_component(const std::string& input);

} // namespace browser
