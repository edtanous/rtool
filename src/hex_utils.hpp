#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

static constexpr std::array<char, 16> kDigitsArray = {
    '0', '1', '2', '3', '4', '5', '6', '7',
    '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};

inline std::string IntToHexString(uint64_t value, size_t digits) {
  std::string rc(digits, '0');
  size_t bit_index = (digits - 1) * 4;
  for (size_t digit_index = 0; digit_index < digits; digit_index++) {
    rc[digit_index] = kDigitsArray[(value >> bit_index) & 0x0f];
    bit_index -= 4;
  }
  return rc;
}

inline std::string BytesToHexString(const std::vector<uint8_t>& bytes) {
  std::string rc(bytes.size() * 2, '0');
  for (size_t i = 0; i < bytes.size(); ++i) {
    rc[i * 2] = kDigitsArray[(bytes[i] & 0xf0) >> 4];
    rc[i * 2 + 1] = kDigitsArray[bytes[i] & 0x0f];
  }
  return rc;
}

// Returns nibble.
inline uint8_t HexCharToNibble(char ch) {
  uint8_t rc = 16;
  if (ch >= '0' && ch <= '9') {
    rc = static_cast<uint8_t>(ch) - '0';
  } else if (ch >= 'A' && ch <= 'F') {
    rc = static_cast<uint8_t>(ch) - 'A' + 10;
  } else if (ch >= 'a' && ch <= 'f') {
    rc = static_cast<uint8_t>(ch) - 'a' + 10;
  }

  return rc;
}

// Returns empty vector in case of malformed hex-string.
inline std::vector<uint8_t> HexStringToBytes(const std::string& str) {
  std::vector<uint8_t> rc(str.size() / 2, 0);
  for (size_t i = 0; i < str.length(); i += 2) {
    uint8_t hi = HexCharToNibble(str[i]);
    if (i == str.length() - 1) {
      return {};
    }
    uint8_t lo = HexCharToNibble(str[i + 1]);
    if (lo == 16 || hi == 16) {
      return {};
    }

    rc[i / 2] = static_cast<uint8_t>(hi << 4) | lo;
  }
  return rc;
}
