// P3589: profiles::suppress attached to a whole function-definition --
// itself a kind of declaration, so squarely within the paper's own
// "declaration or statement" wording, even though it has no separate
// cp_finish_decl call of its own to hook into the way a variable does
// (finish_function, decl.cc, is this case's own equivalent hook) --
// covers every diagnostic anywhere in the function's body, including
// ones this checker can otherwise never suppress by any other
// placement (the address-taken family, always anchored at the
// [[uninit]] declaration itself).
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

void write_somehow (int &r);

[[profiles::suppress(std::init)]]
void f ()
{
  int x [[uninit]];
  write_somehow (x);
}
