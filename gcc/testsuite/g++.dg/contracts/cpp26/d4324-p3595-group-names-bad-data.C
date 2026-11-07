// D4324: a name in a control object's group_names read through data() and
// size() needs a data() that returns const char *.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }
// { dg-additional-options {-fcontract-configuration=\[{"match":{"group":"g"},"output":{"semantic":"observe"}}\]} }

#include <contracts>

struct name
{
  const unsigned char *p;
  unsigned long n;
  constexpr const unsigned char *data () const { return p; }
  constexpr unsigned long size () const { return n; }
};
struct names
{
  const name *b, *e;
  constexpr const name *begin () const { return b; }
  constexpr const name *end () const { return e; }
};
inline constexpr unsigned char g[] = { 'g' };
inline constexpr name my_names[] = { { g, 1 } };

struct ctrl
{
  static constexpr bool is_ignored (std::contracts::assertion_static_info) { return false; }
  static constexpr bool constify (std::contracts::assertion_static_info) { return false; }
  static constexpr bool assumable (std::contracts::assertion_static_info) { return false; }
  static constexpr names group_names { my_names, my_names + 1 };
  void operator() (const std::contracts::assertion_context&) const {}
};
inline constexpr ctrl ctrl_v {};

int f (int x) pre<ctrl_v>(x > 0) { return x; } // { dg-error "does not return 'const char \\\*'" }
