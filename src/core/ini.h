// Minimal INI parser. Platform independent.
#pragma once
#include <map>
#include <string>

namespace pet {

class Ini {
public:
  // Parses "[section]" headers and "key=value" lines. Lines starting with ';' or '#'
  // are comments; a trailing " ;comment" after a value is stripped. Section and key
  // names are case-insensitive.
  static Ini parse(const std::string& text);

  bool has(const std::string& sec, const std::string& key) const;
  std::string get(const std::string& sec, const std::string& key, const std::string& def) const;
  int getInt(const std::string& sec, const std::string& key, int def) const;
  double getDouble(const std::string& sec, const std::string& key, double def) const;

private:
  std::map<std::string, std::map<std::string, std::string>> sections_;
};

}  // namespace pet
