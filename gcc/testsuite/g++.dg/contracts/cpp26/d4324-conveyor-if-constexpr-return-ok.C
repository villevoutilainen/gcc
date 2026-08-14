// D4324: a conveyor function's "every exit path must return" check
// (oa_stmt_terminates_p) must special-case 'if constexpr': the branch
// its condition doesn't select is never substituted into the
// instantiated tree at all (pt.cc's own tsubst_expr IF_STMT case skips
// it), so it reads as an ordinary empty clause -- which would
// legitimately fail to terminate on its own -- even though it can
// never actually run and so can never threaten falling through
// regardless of its own, never-executed shape. Found via a real
// regression extending _GLIBCXX_CONVEYOR_ASSERTIONS to
// std::ranges::subrange::size() (if constexpr (_S_store_size) return
// ...; else return ...;), misdiagnosed as never returning even though
// both source branches plainly do. See .claude/plans/well-we-last-
// discussed-ethereal-duckling.md.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

template <bool B>
int
f (int x) conveyor
{
  if constexpr (B)
    return x;
  else
    return x;
}

int main () { return f<true> (1) + f<false> (1); }
