// P3446R0 Invalidation profile: an entirely fieldless return type
// structurally cannot carry any argument's address out, no matter
// what the callee does with it -- confirmed a real bug
// (https://godbolt.org/z/ofxz943nf): GCC elides X's return-slot local
// entirely for a trivial empty type ('gimple_call <f1, NULL, &tmp>',
// a genuinely NULL call-lhs), routing this through ip_call_escapes_
// locally_p, which -- unlike ip_var_contents_escape_locally_p's own,
// identical gate on the var-materialized path a non-empty plain
// struct takes -- had no ip_type_may_hold_pointer_p check at all, so
// it examined f1's arguments unconditionally and wrongly concluded
// the local's address might be smuggled out.
// { dg-do compile { target c++14 } }

[[profiles::enforce(std::invalidation)]];

struct X
{
};

X f1 (int const &m);

auto g1 ()
{
  return f1 (7);
}
