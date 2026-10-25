// D4324: a class template's control type whose operator() returns after a
// failed check establishes nothing.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>
#include <contract_labels>

void report ();

template <bool C>
struct Label : std::contracts::P3400::label_base<Label<C> >
{
  using assertion_control_object = Label;
  static constexpr bool is_conveyor (std::contracts::assertion_static_info) noexcept
  { return C; }
  static constexpr bool constify (std::contracts::assertion_static_info) noexcept
  { return false; }
  constexpr void operator() (const std::contracts::assertion_context &c) const
  {
    if (!c.check ())
      report ();
  }
};

int f (int a) conveyor
{
  contract_assert<Label<true>{}> (0 < a);
  return a - 1;	// { dg-error "not provably free of overflow" }
}
