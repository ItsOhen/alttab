#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <immintrin.h>
#include <string>

struct simd4x4 {
  float x[4], y[4], z[4], scale[4];
};

inline __m128 fast_sin_ps(__m128 x) {
  __m128 x2 = _mm_mul_ps(x, x);
  __m128 x3 = _mm_mul_ps(x2, x);
  __m128 x5 = _mm_mul_ps(_mm_mul_ps(x3, x2), _mm_set1_ps(1.0f / 120.0f));
  x3 = _mm_mul_ps(x3, _mm_set1_ps(1.0f / 6.0f));
  return _mm_add_ps(_mm_sub_ps(x, x3), x5);
}

inline std::string middleTruncate(std::string str, size_t maxLen = 40) {
  if (str.length() <= maxLen)
    return str;
  maxLen = std::max((int)maxLen, 5);

  size_t sideLen = (maxLen - 3) / 2;
  return str.substr(0, sideLen) + "..." + str.substr(str.length() - sideLen);
}

inline std::string toLower(std::string_view str) {
  std::string out(str);
  std::transform(out.begin(), out.end(), out.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return out;
}
