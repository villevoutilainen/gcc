// P4222 Initialization profile: dereferencing a [[ref_to_uninit]]
// pointer whose value provably traces back to '&x' (x itself
// [[uninit]]) is exactly as much a read of x as naming x directly --
// this used to compile clean with zero diagnostics, since a
// dereference's operand is an opaque SSA_NAME to the purely syntactic
// per-statement DAA scan, unlike '&x'/'x.field'/'x[i]', which contain
// x as a literal subtree.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

int main ()
{
  int x [[uninit]];
  int* p [[ref_to_uninit]] = &x;
  return *p; // { dg-error "read before it is definitely assigned" }
}
