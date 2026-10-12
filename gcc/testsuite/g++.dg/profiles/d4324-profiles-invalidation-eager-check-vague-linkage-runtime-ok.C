// P3446R0/P4296R0 Invalidation profile: the same eager-check-mechanism
// regression guard as d4324-profiles-init-eager-check-vague-linkage-
// runtime-ok.C's own comment (see that file for the full rationale) --
// an in-class-defined (hence implicitly inline, vague-linkage) member
// function, genuinely called from main (), checked at runtime (not
// just compile-time) to confirm its body survives to link and actually
// runs, rather than being silently dropped.
//
// { dg-do run { target c++11 } }
// { dg-options "-std=c++20" }

[[profiles::enforce(std::invalidation)]];

struct holder
{
  int value;
  int *get () { return &value; }
};

int main ()
{
  holder h{42};
  int *p = h.get ();
  *p = 7;
  return h.value == 7 ? 0 : 1;
}
