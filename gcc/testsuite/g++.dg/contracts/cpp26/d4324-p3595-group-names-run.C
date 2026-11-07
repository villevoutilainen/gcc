// D4324, QoI: the P3595 configuration's "group" key matches any of the
// names in a control object's group_names, a range the compiler walks at
// compile time: a combined label is in both its operands' groups, and a
// user's own control object may give its names any type with data()
// (returning const char *) and size(), or const char *.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-evaluation-semantic=enforce" }
// { dg-additional-options {-fcontract-configuration=\[{"match":{"group":"perf"},"output":{"semantic":"observe"}},{"match":{"group":"mine"},"output":{"semantic":"ignore"}}\]} }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>
#include <contract_labels>
using namespace std::contracts::P3400;

int calls;

void
handle_contract_violation (const std::contracts::contract_violation& v)
{
  ++calls;
  if (v.semantic () == std::contracts::evaluation_semantic::enforce)
    throw 0;
}

// A name type of the user's own: data() and size(), no NUL needed.
struct name
{
  const char *p;
  unsigned long n;
  constexpr const char *data () const { return p; }
  constexpr unsigned long size () const { return n; }
};

struct names
{
  const name *b, *e;
  constexpr const name *begin () const { return b; }
  constexpr const name *end () const { return e; }
};

inline constexpr name my_names[] = { { "mineXXX", 4 }, { "other", 5 } };

struct mine_ctrl
{
  static constexpr bool is_ignored (std::contracts::assertion_static_info i)
  { return i.semantic () == std::contracts::evaluation_semantic::ignore; }
  static constexpr bool constify (std::contracts::assertion_static_info) { return false; }
  static constexpr bool assumable (std::contracts::assertion_static_info) { return false; }
  static constexpr names group_names { my_names, my_names + 2 };
  void operator() (const std::contracts::assertion_context& c) const
  { if (!c.check ()) __builtin_abort (); }
};
inline constexpr mine_ctrl mine_v {};

int both (int x) pre<"lib.io"group | "perf"group>(x > 0) { return x; }
int lib (int x) pre<"lib.io"group>(x > 0) { return x; }
int own (int x) pre<mine_v>(x > 0) { return x; }

int
main ()
{
  // In perf too: observed.
  if (both (-1) != -1 || calls != 1)
    __builtin_abort ();
  // Only lib.io: enforced.
  try { lib (-1); __builtin_abort (); } catch (int) {}
  if (calls != 2)
    __builtin_abort ();
  // "mine" (the first 4 characters of its first name): ignored.
  if (own (-1) != -1)
    __builtin_abort ();
}
