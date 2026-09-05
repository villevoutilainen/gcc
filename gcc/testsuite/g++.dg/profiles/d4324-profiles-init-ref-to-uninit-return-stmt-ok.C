// P4222 Initialization profile: a [[ref_to_uninit]]-declared function's
// own 'return expr;' statements must match its declared flavor -- a
// flavored return (through a matching accessor) and a null return are
// both accepted. Also exercises the multi-return/PHI-merge shape
// directly: GCC unifies 'if (cond) return a; return b;' into a single
// canonical exit block with a PHI-merged return value (confirmed via
// -fdump-tree-ssa), which the checker must see through correctly
// rather than silently misjudging an unhandled GIMPLE_PHI shape.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

struct Widget
{
  [[ref_to_uninit]] void* get ();
};

[[ref_to_uninit]] void* flavored_fn (bool cond, Widget &w)
{
  if (cond)
    return w.get ();
  return nullptr;
}
