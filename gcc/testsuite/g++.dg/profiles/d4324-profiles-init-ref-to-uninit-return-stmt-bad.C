// P4222 Initialization profile: the reverse of d4324-profiles-init-
// ref-to-uninit-return-stmt-ok.C -- returning an ORDINARY (unflavored)
// value from a [[ref_to_uninit]]-declared function is an error. Before
// this check existed, nothing examined a function's own return
// statements against its own declared flavor at all (only how callers
// treat the result), so this compiled clean with zero diagnostics.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

struct Widget
{
  void* get_plain ();
};

[[ref_to_uninit]] void* flavored_fn (Widget &w)
{
  return w.get_plain (); // { dg-error "returning a pointer not marked \[^\n\]*ref_to_uninit\[^\n\]* from a function marked" }
}
