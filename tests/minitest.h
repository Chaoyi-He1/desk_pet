// Minimal test framework: no third-party dependency.
#pragma once
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace minitest {
struct Case { const char* name; std::function<void()> fn; };
inline std::vector<Case>& cases() { static std::vector<Case> c; return c; }
inline int& failures() { static int f = 0; return f; }
struct Registrar { Registrar(const char* n, std::function<void()> f) { cases().push_back({n, std::move(f)}); } };
inline int run() {
  int failed = 0;
  for (auto& c : cases()) {
    int before = failures();
    c.fn();
    bool ok = failures() == before;
    std::printf("[%s] %s\n", ok ? " OK " : "FAIL", c.name);
    if (!ok) ++failed;
  }
  std::printf("%zu tests, %d failed\n", cases().size(), failed);
  return failed;
}
}  // namespace minitest

#define TEST(name) \
  static void test_##name(); \
  static minitest::Registrar reg_##name(#name, test_##name); \
  static void test_##name()

#define CHECK(expr) \
  do { if (!(expr)) { ++minitest::failures(); \
    std::printf("  %s:%d CHECK failed: %s\n", __FILE__, __LINE__, #expr); } } while (0)

#define CHECK_EQ(a, b) \
  do { auto va_ = (a); auto vb_ = (b); if (!(va_ == vb_)) { ++minitest::failures(); \
    std::printf("  %s:%d CHECK_EQ failed: %s == %s (got %s vs %s)\n", __FILE__, __LINE__, #a, #b, \
      std::to_string(va_).c_str(), std::to_string(vb_).c_str()); } } while (0)

#define MINITEST_MAIN int main() { return minitest::run(); }
