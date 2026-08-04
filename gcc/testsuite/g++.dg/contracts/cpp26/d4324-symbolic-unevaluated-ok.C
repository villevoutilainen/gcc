// Axiom contracts (~/gcc-axiom-contracts.md): a symbolic function may
// be freely named in an unevaluated context (decltype, a
// requires-expression's own requirement) -- these never odr-use it and
// leave no residual call in the executable body the stray-use scan
// (d4324-symbolic-stray-use-error.C) walks, exactly like
// std::is_object_address already coexists with unevaluated uses.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

bool is_opened (int* p) symbolic;

void ok_decltype (int* p)
{
  decltype (is_opened (p)) b;
  (void) b;
}

template <typename T>
concept Openable = requires (T* p) { is_opened (p); };

static_assert (Openable<int>);

int main () { return 0; }
