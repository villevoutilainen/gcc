// P4222 Initialization profile: companion to d4324-profiles-uninit-
// flavor-through-pointer-arith-ok.C -- confirms the fix there checks
// the base pointer's OWN flavor rather than unconditionally treating
// any pointer arithmetic as flavored. Arithmetic on an ordinary,
// unflavored pointer field must still be rejected when passed to a
// [[ref_to_uninit]] parameter.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

struct S { int* elem; };

void other (int* q [[ref_to_uninit]]);

void f (S &s, int i)
{
  other (s.elem + i); // { dg-error "must refer to" }
}
