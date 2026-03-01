#pragma once

#include <cstddef>
#include <string>
inline std::string middleTruncate(std::string str, size_t maxLen = 40) {
  if (str.length() <= maxLen)
    return str;
  maxLen = std::max((int)maxLen, 5);

  size_t sideLen = (maxLen - 3) / 2;
  return str.substr(0, sideLen) + "..." + str.substr(str.length() - sideLen);
}
