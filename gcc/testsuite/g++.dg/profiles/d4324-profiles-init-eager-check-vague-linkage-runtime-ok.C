// P4222 Initialization profile: a REGRESSION GUARD for the eager,
// per-function checking mechanism itself (profiles_eager_check_
// function/_1, profiles.cc), not specifically about [[std::init]]
// semantics -- every other test in this directory is dg-do compile
// only, so a bug that only manifests at LINK or RUN time would be
// completely invisible to the whole rest of this suite.
//
// Confirmed directly this was a real, live bug, not a hypothetical
// one: an in-class-defined (hence implicitly inline, vague-linkage)
// member function's own body was silently dropped at link time --
// "undefined reference to counter::bump()" -- despite a completely
// correct, genuinely-called definition. Root cause: gimplify_
// function_tree (called from this eager mechanism's own node->
// analyze ()) unconditionally clears DECL_SAVED_TREE, which c_parse_
// final_cleanups's own deferred_fns vague-linkage resolution sweep
// (decl2.cc) depends on still being intact to ever turn a comdat/
// inline function's own TENTATIVE "assume external" linkage decision
// into the real one -- its own "if (!DECL_SAVED_TREE (decl)) continue;"
// silently skips any entry whose DECL_SAVED_TREE is already gone,
// leaving that tentative decision stuck forever. Fixed by deferring
// any vague_linkage_p function to the one point already known to be
// safe: genuine end-of-TU (profiles_eager_check_drain_at_eof, called
// from c_parse_final_cleanups right after at_eof is set to 3) --
// exactly the same fix already needed, for the exact same reason, in
// the GIMPLE contracts engine's own identical eager-check architecture
// (contracts_gimple_eager_check_function, contracts-gimple.cc).
//
// { dg-do run { target c++11 } }

[[profiles::enforce(std::init)]];

struct counter
{
  int value = 0;
  int bump () { value = value + 1; return value; }
};

int main ()
{
  counter c;
  if (c.bump () != 1) return 1;
  if (c.bump () != 2) return 1;
  if (c.bump () != 3) return 1;
  return 0;
}
