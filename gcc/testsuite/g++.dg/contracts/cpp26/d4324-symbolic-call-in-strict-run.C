// D4324: a strict condition and a symbolic fact on one function are two
// assertions -- a function declared 'symbolic' is named only in a symbolic
// contract, never evaluated -- so the strict one is checked at run time and
// the symbolic one statically (P2176R0's example).
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-evaluation-semantic=observe" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>
namespace sc = std::contracts;

int violations;
void handle_contract_violation (const sc::contract_violation&) { ++violations; }

struct strict_t
{
  static constexpr bool is_ignored (sc::assertion_static_info) { return false; }
  static constexpr bool constify (sc::assertion_static_info) { return false; }
  static constexpr bool assumable (sc::assertion_static_info) { return false; }
  static constexpr bool is_conveyor (sc::assertion_static_info) { return true; }
  void operator() (const sc::assertion_context& c) const
  { if (!c.check ()) sc::invoke_violation_handler (c.info (), sc::detection_mode::predicate_false); }
};
inline constexpr strict_t strict_v {};

struct File { int fd; };
bool is_open (const File*) symbolic;
void close_file (File* const f)
  pre<strict_v>(f != nullptr)
  pre<sc::symbolic_assert_v>(is_open (f)) {}

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
}
