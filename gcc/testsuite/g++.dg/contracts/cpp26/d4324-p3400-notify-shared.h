// D4324/P3400: shared declarations for the multi-TU notify_label test
// (d4324-p3400-notify-multi-tu.C + -handler.cc + -user2.cc). A real,
// properly-typed data item -- no void*, no key/index -- and a real,
// ordinary handler function: declared once here, defined in exactly
// one TU (-handler.cc), called from several others via plain
// one-definition-rule linkage. Nothing about this needs
// dg-additional-sources of its own: it's just an ordinary header
// #include'd by every TU.

#ifndef D4324_P3400_NOTIFY_SHARED_H
#define D4324_P3400_NOTIFY_SHARED_H

#include <contract_labels>

namespace P3400 = std::contracts::P3400;

struct OwnerNotification
{
  const char* owner_name;
  const char* comment;
};

// The real, strongly-typed custom handler -- defined in exactly one
// TU (d4324-p3400-notify-multi-tu-handler.cc), called from every
// other TU that names it. Nothing to do with
// ::handle_contract_violation/contract_violation at all.
extern void notify_owner (const OwnerNotification&);

// Test-observation state, also defined in -handler.cc, read back by
// the main TU after driving calls from itself and from -user2.cc.
constexpr int notify_owner_max_calls = 8;
extern int notify_owner_calls;
extern const char* notify_owner_names[notify_owner_max_calls];
extern const char* notify_owner_comments[notify_owner_max_calls];

// The label itself: opts into notify_label by defining notify(), which
// builds a real OwnerNotification from its own instance data (a real
// per-instance field, not a shared/static one -- two objects of this
// same type, one per TU below, carry different owner names) and calls
// the real notify_owner directly. Shared here (not duplicated per TU)
// so every user is the exact same type, avoiding any ODR question.
struct owner_notify_label : P3400::label_base<owner_notify_label>
{
  using assertion_control_object = owner_notify_label;
  const char* owner_name;

  constexpr owner_notify_label (const char* __name) noexcept
  : owner_name (__name) { }

  void
  notify (const std::contracts::assertion_context& ctx) const
  {
    OwnerNotification data{ owner_name, ctx.comment () };
    notify_owner (data);
  }
};

inline constexpr owner_notify_label alice_notify{"alice"};
inline constexpr owner_notify_label bob_notify{"bob"};

// -user2.cc's own labeled function, proving the custom handler is
// genuinely used from more than one TU besides the one that defines
// it. A function's contract-bearing declarations must all agree
// (C++26 Contracts: a later redeclaration may not add contracts an
// earlier one didn't have) -- so this declares the real contract here,
// once, rather than re-declaring g() bare and defining it with a
// contract only in -user2.cc.
extern int g (int x) pre<bob_notify>(x > 0);

#endif
