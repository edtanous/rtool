#pragma once

inline std::ostream& operator<<(std::ostream& os, const key_filter& filt) {
  os << filt.key;
  return os;
}

inline std::ostream& operator<<(std::ostream& os, const path& path) {
  os << "path[";
  std::visit([&os](auto&& out) { os << out << ','; }, path.first);
  for (const auto& filter : path.filters) {
    std::visit([&os](auto&& out) { os << out << ','; }, filter);
  }
  os << ']';
  return os;
}
