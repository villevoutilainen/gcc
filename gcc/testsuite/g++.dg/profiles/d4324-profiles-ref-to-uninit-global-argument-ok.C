// P4222 Initialization profile: ip_arg_uninit_flavored_p used to miss
// a [[ref_to_uninit]]-marked GLOBAL/namespace-scope pointer variable
// entirely when passed as a call argument -- a global is never
// is_gimple_reg (its value could be observed/modified from outside
// this function's own CFG), so its read is first copied into an
// anonymous SSA temporary ('src.0_1 = src;') before being passed,
// and the call never sees 'src' itself at all. Confirmed via direct
// -fdump-tree-gimple reading; fixed by recursing through any
// single-operand SSA def (not just an ADDR_EXPR-shaped one).
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

void* src [[ref_to_uninit]] = nullptr;

void take (void* p [[ref_to_uninit]]);

int main ()
{
  take (src);
}
