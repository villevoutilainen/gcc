// P4222 Initialization profile: for a function GCC recognizes as an
// actual BUILTIN by name+signature (the real libc 'malloc', not a
// renamed/differently-signed stand-in), the gimplifier assigns the
// call's result DIRECTLY into the named destination with no
// intermediate anonymous SSA temporary ('p = malloc(4);', confirmed
// via -fdump-tree-ssa) -- a shape ip_check_assign_flavor_consistency
// alone (which only examines GIMPLE_ASSIGN, not GIMPLE_CALL) can never
// see. A matching flavor on both sides must still compile clean.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

typedef __SIZE_TYPE__ size_t;

extern "C" [[ref_to_uninit]] void* malloc (size_t n);

int main ()
{
  void* p [[ref_to_uninit]] = malloc (4);
}
