// P3446R0 Invalidation profile: reading a plain (non-const, non-
// rvalue) reference DATA MEMBER directly and returning it must stay
// clean, symmetric with the already-established escape-ref-param-
// direct-ok.C for a plain reference PARAMETER.
//
// 'Handle::value' is a plain 'int&' -- a non-const lvalue reference
// cannot bind to a temporary at all (a language rule), so 'value' can
// only ever have been bound to a genuine lvalue. Whether that lvalue
// outlives the 'Handle' object itself is a separate question this
// read cannot see -- but it's independently, already checked wherever
// 'Handle' is actually constructed (see the sibling d4324-profiles-
// invalidation-escape-ref-member-construction-bad.C, which proves a
// too-short-lived binding is caught there, not here). Re-flagging
// every subsequent read of an already-validated member would be pure
// redundancy, the same reasoning this file already applies to an
// ordinary parameter's own value.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

struct Handle
{
  int &value;

  int &get () const
  {
    return value;
  }
};
