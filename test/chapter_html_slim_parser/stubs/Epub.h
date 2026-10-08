#pragma once

#include <cstddef>
#include <map>
#include <string>
#include <vector>

class Epub {
 public:
  std::map<std::string, std::vector<uint8_t>> entries;
  template <typename Output>
  bool readItemContentsToStream(const std::string& path, Output& output, size_t, bool allowEarlyStop = false) const {
    const auto it = entries.find(path);
    if (it == entries.end()) return false;
    for (const uint8_t byte : it->second) {
      if (output.write(byte) != 1) return allowEarlyStop;
    }
    return true;
  }
};
