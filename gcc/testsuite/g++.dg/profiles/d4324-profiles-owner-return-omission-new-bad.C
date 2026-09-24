// P3446R0/P4296R0 Invalidation profile: a function not declared [[owner]]
// on its own return must not return a value that is provably a fresh
// owner source -- doing so silently launders ownership through an
// undeclared return type, with no way for any caller to ever recover
// that the returned pointer needs to be owned/deleted (this engine
// never reads a callee's body). Confirmed a real gap
// (michael wong): 'int* f(){ return new int{9}; }' compiled clean with
// no diagnostic anywhere, caller included.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

int *
f ()
{
  return new int{9}; // { dg-error "freshly-allocated pointer from a function not marked" }
}
