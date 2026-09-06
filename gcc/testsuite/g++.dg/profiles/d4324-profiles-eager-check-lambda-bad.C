// P3446R0/P4222 profiles: a lambda's own closure-type operator() also
// goes through expand_or_defer_fn (like any other function) and gets
// eagerly checked once its own body is complete, alongside an
// unrelated front-end error elsewhere in the translation unit.  The
// lambda has a 'static' local specifically to disqualify it from
// C++20's own "implicitly constexpr if it could be" rule: a
// constexpr-eligible function is deliberately exempted from this
// eager mechanism (gimplifying it early would permanently discard the
// GENERIC tree a later constant-evaluation of it might still need --
// see profiles_eager_check_function_1's own comment,
// profiles.cc), so this test specifically needs a non-constexpr-
// eligible lambda to exercise the eager path.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

int bad_early ()
{
  return undeclared_identifier; // { dg-error "was not declared" }
}

int use_lambda ()
{
  auto l = [] ()
    {
      static int counter = 0;
      counter++;
      int x [[uninit]];
      int *p [[ref_to_uninit]] = &x;
      return *p; // { dg-error "read before it is definitely assigned" }
    };
  return l ();
}
