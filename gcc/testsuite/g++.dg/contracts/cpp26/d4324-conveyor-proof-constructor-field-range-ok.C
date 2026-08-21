// D4324/P2680: -fcontract-conveyor-proofs, a constructor call establishes
// a field-range fact for its own caller, exactly like an ordinary member
// function's postcondition already does (d4324-conveyor-proof-field-
// range-ok.C) -- a constructor call always resolves to one of its own
// compiler-generated clones (__ct_comp/__ct_base), never to the decl the
// user wrote, and cloning never used to copy contract specifiers onto
// those clones, so get_fn_contract_specifiers on the callee returned
// NULL and no precondition-obligation check or postcondition-based fact
// establishment ever ran for any constructor call at all. Fixed via
// propagate_cdtor_contracts_to_clones, called once a constructor's own
// contracts are a real, parsed condition (not a DEFERRED_PARSE
// placeholder).  thing's constructor establishes this->count in
// [40,100); consume_count()'s precondition requires this->count in
// [20,1000) on the same object -- [40,100) is a subset of [20,1000), so
// the obligation is discharged silently, entirely at compile time.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-conveyor-proofs" }

#include <contracts>
namespace sc = std::contracts;

struct thing {
  int count;
  explicit thing (int c)
    pre<sc::proven_conveyor_v>(c >= 40 && c < 100)
    post<sc::proven_conveyor_v>(this->count >= 40 && this->count < 100)
  { count = c; }
  void consume_count ()
    pre<sc::proven_conveyor_v>(std::is_object_address (this))
    pre<sc::proven_conveyor_v>(this->count >= 20 && this->count < 1000)
  { }
};

int main ()
{
  thing t (55);
  t.consume_count ();
  return 0;
}
