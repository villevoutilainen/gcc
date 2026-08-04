// Auxiliary TU for d4324-symbolic-runtime-cross-tu.C (dg-additional-sources)
// -- establishes is_opened(this) for an io_facility whose identity is
// shared with the main TU only via the runtime record store, not via
// any shared address computed within one translation unit.
#include <contracts>
namespace sc = std::contracts;

// symbolic_ctrl's definition must stay token-for-token identical to the
// main TU's own (ODR: io_facility's contracts name this same class) --
// its operator() body is never actually invoked from this TU, though,
// since a postcondition's runtime establishment never calls the
// control object at all (see build_symbolic_runtime_check's comment in
// gcc/cp/contracts.cc). static (not extern) so two independent,
// same-named globals in two TUs don't collide at link time.
static bool checked = false;
static bool failed = false;

struct symbolic_ctrl {
  static constexpr bool is_symbolic (sc::assertion_static_info) { return true; }
  void operator() (const sc::assertion_context& ctx) const
  {
    checked = true;
    if (!ctx.check ())
      failed = true;
  }
};
inline constexpr symbolic_ctrl symbolic_ctrl_v{};

struct io_facility {
  static bool is_opened (io_facility*) symbolic;
  void open () post<symbolic_ctrl_v>(is_opened (this)) {}
  void write_data () pre<symbolic_ctrl_v>(is_opened (this)) {}
};

void open_it (io_facility& f) { f.open (); }
