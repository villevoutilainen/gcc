// Companion to d4324-profiles-invalidation-nontrivial-return-
// escape-bad.C: the same non-trivially-copyable (destructor-having)
// SSA-name-wrapped-RESULT_DECL shape, but with an entirely fieldless
// return type -- must still be silent. Confirms the type-capacity
// gate in ip_call_escapes_locally_p is reached and applies even when
// the elided call is found via the broadened, return-slot-
// optimization-aware matching in ip_call_returns_type_p, not just the
// plain genuinely-NULL-lhs shape.
// { dg-do compile { target c++14 } }

[[profiles::enforce(std::invalidation)]];

struct X
{
  ~X ();
};

X f1 (int const &m);

auto g1 ()
{
  return f1 (7);
}
