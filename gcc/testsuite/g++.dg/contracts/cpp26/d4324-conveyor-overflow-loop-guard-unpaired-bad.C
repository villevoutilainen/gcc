// D4324/P2680 item 8's overflow scan: an increment with no recognizable
// guard establishing any fact at all (numeric or type-bound witness)
// still conservatively errors -- confirms the new type-bound-witness
// route doesn't overreach into genuinely unprovable cases, matching
// this pass's own "unprovable is always an error, never silently
// skipped" discipline used throughout.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

int use_loop_guard_unpaired_bad (int i) conveyor
{
  while (true)
    ++i; // { dg-error "increment of .i. not provably free of overflow" }
  return i;
}

int main () { return use_loop_guard_unpaired_bad (0); }
