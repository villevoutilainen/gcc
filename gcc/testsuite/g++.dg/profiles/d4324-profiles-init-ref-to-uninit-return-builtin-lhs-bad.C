// P4222 Initialization profile: the identical shape d4324-profiles-
// init-ref-to-uninit-return-builtin-lhs-ok.C accepts, but assigned into
// an UNMARKED destination -- confirms the builtin-recognized direct-
// LHS shape ('p = malloc(4);', no intermediate temp) is actually
// checked, not silently skipped the way it was before this fix (this
// exact case previously compiled clean with zero diagnostics).
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

typedef __SIZE_TYPE__ size_t;

extern "C" [[ref_to_uninit]] void* malloc (size_t n);

int main ()
{
  void* p = malloc (4); // { dg-error "assigning a pointer marked \[^\n\]*ref_to_uninit\[^\n\]* into a pointer not marked" }
}
