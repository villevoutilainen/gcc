// std::format and print, with each argument kind.
#include "types.h"
#include <format>
#include <string>
#include <vector>
#include <map>
#include <tuple>
#include <chrono>

template<>
struct std::formatter<census::Val> : std::formatter<int>
{
  auto format (const census::Val &v, std::format_context &ctx) const
  { return std::formatter<int>::format (v.v, ctx); }
};

namespace census
{
  int all ()
  {
    int n = 0;
    std::string s = std::format ("{} {:>8} {:#x} {:.3f} {:e}", 1, "ab", 255u, 1.5, 2.0);
    n += s.size ();
    n += std::format ("{} {}", Val (1), std::string ("x")).size ();
    std::wstring w = std::format (L"{} {}", 1, L"x");
    n += w.size ();
    std::vector<int> v = { 1, 2, 3 };
    n += std::format ("{} {:n}", v, v).size ();
    std::map<int, std::string> m = { { 1, "a" } };
    n += std::format ("{}", m).size ();
    n += std::format ("{}", std::tuple (1, 'c', 2.5)).size ();
    n += std::format ("{}", std::pair (1, 2)).size ();
    n += std::format ("{}", std::chrono::seconds (3)).size ();
    std::string out;
    std::format_to (std::back_inserter (out), "{}-{}", 1, 2);
    char buf[16];
    auto r = std::format_to_n (buf, sizeof buf, "{}", 12345);
    n += r.size + std::formatted_size ("{}", 42);
    n += std::vformat ("{}", std::make_format_args (n)).size ();
    return n + out.size ();
  }
}

int main () { return census::all () == 0; }
