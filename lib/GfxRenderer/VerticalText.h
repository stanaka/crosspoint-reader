#pragma once

#include <cstdint>
#include <string_view>

enum class WritingMode : uint8_t { Auto, Horizontal, Vertical };
enum class PageProgression : uint8_t { Default, Ltr, Rtl };

namespace verticalText {
enum class Behavior : uint8_t { Upright, Sideways, TateChuYoko };
struct PageButtons {
  bool prev;
  bool next;
};
constexpr PageButtons pageButtons(bool prev, bool next, bool vertical) {
  return vertical ? PageButtons{next, prev} : PageButtons{prev, next};
}

constexpr uint32_t firstCodepoint(std::string_view text) {
  if (text.empty()) return 0;
  const auto a = static_cast<uint8_t>(text[0]);
  if (a < 0x80) return a;
  const int n = a < 0xe0 ? 2 : a < 0xf0 ? 3 : 4;
  if (text.size() < static_cast<size_t>(n)) return 0xfffd;
  uint32_t cp = a & (0x7f >> n);
  for (int i = 1; i < n; ++i) cp = (cp << 6) | (static_cast<uint8_t>(text[i]) & 0x3f);
  return cp;
}

constexpr bool upright(uint32_t cp) {
  return (cp >= 0x2e80 && cp <= 0xa4cf) || (cp >= 0xac00 && cp <= 0xd7af) || (cp >= 0xf900 && cp <= 0xfaff) ||
         (cp >= 0xfe10 && cp <= 0xfe4f) || (cp >= 0xff01 && cp <= 0xffef) || (cp >= 0x20000 && cp <= 0x3ffff) ||
         cp == 0x2014 || cp == 0x2015 || cp == 0x2025 || cp == 0x2026 || cp == 0x22ef;
}

constexpr Behavior classify(std::string_view text) {
  if (upright(firstCodepoint(text))) return Behavior::Upright;
  if (!text.empty() && text.size() <= 2) {
    bool digits = true;
    for (char c : text) digits = digits && c >= '0' && c <= '9';
    if (digits) return Behavior::TateChuYoko;
  }
  return Behavior::Sideways;
}

constexpr bool smallKana(uint32_t cp) {
  switch (cp) {
    case 0x3041:
    case 0x3043:
    case 0x3045:
    case 0x3047:
    case 0x3049:
    case 0x3063:
    case 0x3083:
    case 0x3085:
    case 0x3087:
    case 0x308e:
    case 0x30a1:
    case 0x30a3:
    case 0x30a5:
    case 0x30a7:
    case 0x30a9:
    case 0x30c3:
    case 0x30e3:
    case 0x30e5:
    case 0x30e7:
    case 0x30ee:
    case 0x30f5:
    case 0x30f6:
      return true;
    default:
      return false;
  }
}

constexpr bool prohibitedHead(uint32_t cp) {
  switch (cp) {
    case 0x3001:
    case 0x3002:
    case 0x3009:
    case 0x300b:
    case 0x300d:
    case 0x300f:
    case 0x3011:
    case 0x3015:
    case 0x3017:
    case 0x3019:
    case 0x301b:
    case 0xff09:
    case 0xff3d:
    case 0xff5d:
    case 0xff0c:
    case 0xff0e:
    case 0xff01:
    case 0xff1f:
    case 0xff1a:
    case 0xff1b:
    case 0x30fc:
    case 0x3041:
    case 0x3043:
    case 0x3045:
    case 0x3047:
    case 0x3049:
    case 0x3063:
    case 0x3083:
    case 0x3085:
    case 0x3087:
    case 0x308e:
    case 0x30a1:
    case 0x30a3:
    case 0x30a5:
    case 0x30a7:
    case 0x30a9:
    case 0x30c3:
    case 0x30e3:
    case 0x30e5:
    case 0x30e7:
    case 0x30ee:
    case 0x30f5:
    case 0x30f6:
      return true;
    default:
      return false;
  }
}

constexpr bool prohibitedTail(uint32_t cp) {
  switch (cp) {
    case 0x3008:
    case 0x300a:
    case 0x300c:
    case 0x300e:
    case 0x3010:
    case 0x3014:
    case 0x3016:
    case 0x3018:
    case 0x301a:
    case 0xff08:
    case 0xff3b:
    case 0xff5b:
      return true;
    default:
      return false;
  }
}

constexpr bool alternate(uint32_t cp) {
  return prohibitedTail(cp) || (cp >= 0x3008 && cp <= 0x3020) || cp == 0x3001 || cp == 0x3002 || cp == 0xff01 ||
         cp == 0xff1f || cp == 0xff09 || cp == 0xff3d || cp == 0xff5d || cp == 0xff0c || cp == 0xff0e || cp == 0xff1a ||
         cp == 0xff1b || cp == 0xff5e || cp == 0x30fc || cp == 0x2014 || cp == 0x2015 || cp == 0x2025 || cp == 0x2026 ||
         cp == 0x22ef;
}

constexpr bool languageFallback(std::string_view language, PageProgression progression) {
  if (progression != PageProgression::Rtl) return false;
  language = language.substr(0, language.find_first_of("-_"));
  if (language.size() != 2 && language.size() != 3) return false;
  const auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c + ('a' - 'A')) : c; };
  const char a = lower(language[0]), b = lower(language[1]);
  return (language.size() == 2 && ((a == 'j' && b == 'a') || (a == 'z' && b == 'h'))) ||
         (language.size() == 3 &&
          ((a == 'j' && b == 'p' && lower(language[2]) == 'n') || (a == 'z' && b == 'h' && lower(language[2]) == 'o')));
}
}  // namespace verticalText
