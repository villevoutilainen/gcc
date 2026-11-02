// D4324: a 'conveyor(auto)' function's implicit is_object_address
// preconditions follow what deduction decides: a caller owes them when
// it's conveyor -- even when it's only instantiated after the caller --
// and doesn't when it isn't (its preconditions were synthesized while it
// was presumed conveyor).
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>

int plainf (int x) { return x; }

struct C
{
  int v = 0;
  template <class T> int yes (T) const conveyor(auto) { return v; }
  template <class T> int no (T) const conveyor(auto) { return plainf (v); }
  template <class S> int eyes (this S &&s) conveyor(auto) { return s.v; }
  template <class S> int eno (this S &&s) conveyor(auto) { return plainf (s.v); }
};
template <class T> int ref_yes (const T &x) conveyor(auto) { return x.v; }
template <class T> int ref_no (const T &x) conveyor(auto) { return plainf (x.v); }

int borrowed (const C &c)
{
  return (c.yes (1)	// { dg-error "cannot prove 'is_object_address'" }
	  + c.no (1)
	  + c.eyes ()	// { dg-error "cannot prove 'is_object_address'" }
	  + c.eno ()
	  + ref_yes (c)	// { dg-error "cannot prove 'is_object_address'" }
	  + ref_no (c));
}

int owned ()
{
  C c;
  contract_assert<std::contracts::never_proven_conveyor_v> (std::is_object_address (__builtin_addressof (c))); int __r = c.yes (1); contract_assert<std::contracts::never_proven_conveyor_v> (std::is_object_address (__builtin_addressof (c))); __r += c.no (1); contract_assert<std::contracts::never_proven_conveyor_v> (std::is_object_address (__builtin_addressof (c))); __r += c.eyes (); contract_assert<std::contracts::never_proven_conveyor_v> (std::is_object_address (__builtin_addressof (c))); __r += c.eno (); contract_assert<std::contracts::never_proven_conveyor_v> (std::is_object_address (__builtin_addressof (c))); __r += ref_yes (c); contract_assert<std::contracts::never_proven_conveyor_v> (std::is_object_address (__builtin_addressof (c))); __r += ref_no (c); contract_assert<std::contracts::never_proven_conveyor_v> (std::is_object_address (__builtin_addressof (c))); return __r;
}
