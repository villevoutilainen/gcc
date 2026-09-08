// P3589: a real GCC warning, not merely a message the checker prints
// on its own -- the user's own, unrelated, global -Werror promotes an
// -fprofiles-warning=std::init violation back to a hard error, the
// same as any other GCC warning.
// { dg-do compile { target c++11 } }
// { dg-options "-fprofiles-warning=std::init -Werror" }

void f ()
{
  int x; // { dg-error "not initialized and not marked" }
  (void) x;
}

// { dg-message "all warnings being treated as errors" "" { target *-*-* } 0 }
