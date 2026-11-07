// D4324: a strict predicate, and a conveyor function, may call a symbolic
// function: it has no side effects and no undefined behavior.  At run time
// such a call is true -- its facts are proven, not run -- so an assertion
// both strict and symbolic checks its strict part at run time and its
// symbolic part statically (P2176R0's example).
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-evaluation-semantic=observe" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>
namespace sc = std::contracts;

int violations;
void handle_contract_violation (const sc::contract_violation&) { ++violations; }

struct both_t
{
  static constexpr bool is_ignored (sc::assertion_static_info) { return false; }
  static constexpr bool constify (sc::assertion_static_info) { return false; }
  static constexpr bool assumable (sc::assertion_static_info) { return false; }
  static constexpr bool is_conveyor (sc::assertion_static_info) { return true; }
  static constexpr bool is_symbolic (sc::assertion_static_info) { return true; }
  void operator() (const sc::assertion_context& c) const
  { if (!c.check ()) sc::invoke_violation_handler (c.info (), sc::detection_mode::predicate_false); }
};
inline constexpr both_t both_v {};

struct File { int fd; };
bool is_open (const File*) symbolic;
bool usable (const File* f) conveyor { return f != nullptr && is_open (f); }

void close_file (File* const f) pre<both_v>(f != nullptr && is_open (f)) {}
int read_file (const File* const f) pre<sc::conveyor_assert_v>(usable (f)) { return 1; }

int
main ()
{
  File file;
  close_file (&file);
  if (violations != 0)
    __builtin_abort ();
  close_file (nullptr);
  if (violations != 1)
    __builtin_abort ();
  if (read_file (&file) != 1 || violations != 1)
    __builtin_abort ();
}
