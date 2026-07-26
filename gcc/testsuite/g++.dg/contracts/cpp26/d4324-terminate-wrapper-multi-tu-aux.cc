// See d4324-terminate-wrapper-multi-tu.C.
#include <contracts>

// See d4324-terminate-wrapper-multi-tu.C for why these are inline: this is
// this scratch test's own stand-in for a program's single real violation-
// reporting implementation (normally provided once, e.g. by libstdc++exp),
// made inline purely so that both TUs here can each satisfy the compiler's
// own "used but never defined" check without conflicting with each other
// at link time -- unrelated to the bug this test targets.
namespace std { namespace contracts {
inline void __d4324_log_violation (const char*, std::source_location) noexcept {}
[[noreturn]] inline void __d4324_terminate () noexcept { __builtin_trap (); }
} }

int g (int x) pre (x >= 0) { return x; }
