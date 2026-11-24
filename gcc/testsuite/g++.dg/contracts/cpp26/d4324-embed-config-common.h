// A P3595-style contract configuration as an ordinary D4324 control object.
//
// The rules (parsed at compile time from #embed'ed data: JSON in
// d4324-embed-config-json.h, a compact binary form in
// d4324-embed-config-compact.h) are a constexpr
// table.  One named control object serves every assertion
// (pre<configured_v>): its consteval compute_semantic query matches the
// rules against what the compiler tells it about the assertion -- the
// reflection of its function (assertion_declaration_info::function: for
// "namespace" and "function") and its static info (kind(), side(),
// location()) -- and the compiler makes the result the semantic the rest
// of the control object (is_ignored, operator()) sees.
//
// A rule may assign a group ("output": "group") instead of, or as well
// as, a semantic: the first such rule matching the assertion assigns it,
// and the rules naming a group then match it, as if the code had given
// it; without one, they match the group the compiler's own configuration
// assigned (assertion_static_info::configured_group()).  A rule naming
// only a group, and a semantic, is also for the labels
// (semantic_for_group).
#ifndef D4324_EMBED_CONFIG_COMMON_H
#define D4324_EMBED_CONFIG_COMMON_H

#include <contracts>
#include <meta>
#include <array>
#include <cstdint>
#include <exception>
#include <string_view>

namespace cfg {

using std::contracts::evaluation_semantic;
using std::contracts::assertion_kind;
using std::contracts::assertion_check_side;
using std::contracts::assertion_static_info;
using std::contracts::assertion_context;

// A short fixed-capacity string: constexpr tables can't hold std::string.
struct fstr
{
  char s[64] = {};
  unsigned char n = 0;
  constexpr std::string_view view () const { return {s, n}; }
  constexpr bool empty () const { return n == 0; }
  // Its "::"-separated components, split once when it is assigned.
  unsigned char parts = 0;
  unsigned char start[9] = {}, len[9] = {};
  constexpr std::string_view part (unsigned i) const
  { return {s + start[i], len[i]}; }

  constexpr void assign (std::string_view v)
  {
    if (v.size () >= sizeof s)
      throw "configuration string too long";
    n = v.size ();
    for (unsigned i = 0; i < n; ++i)
      s[i] = v[i];
    parts = 0;
    for (unsigned b = 0; b <= n;)
      {
	unsigned e = b;
	while (e < n && !(s[e] == ':' && e + 1 < n && s[e + 1] == ':'))
	  ++e;
	if (parts == 9)
	  throw "configuration name has too many components";
	start[parts] = b;
	len[parts] = e - b;
	++parts;
	b = e + 2;
      }
  }
};

struct rule
{
  unsigned char kind = 0;	// 0: any; 1 pre, 2 post, 3 contract_assert
  bool caller = false;		// matches only caller-side checks
  unsigned char semantic = 0;	// evaluation_semantic value
  unsigned char nlines = 0;	// line intervals of "location", 0: any line
  std::array<std::uint32_t, 16> lines{};	// lo/hi pairs
  fstr ns, function, file;
  fstr group;			// "a.b": matched by a control object's own group
  fstr out_group;		// "output": "group": the group it assigns
};

struct rules_t
{
  std::array<rule, 64> r{};
  unsigned n = 0;
  // For each [caller][kind], the indices of the rules that can match an
  // assertion of that kind on that side, in order: built once, by
  // index (), so that a query scans only those.
  unsigned char cand[2][4][64] = {};
  unsigned char ncand[2][4] = {};

  // The semantic of the first rule naming only a group that covers NAME
  // ("lib" covers "lib" and "lib.io"), or 0.  A group is a control
  // object's own data, so it can ask this itself, when it is constructed.
  constexpr unsigned char semantic_for_group (std::string_view name) const
  {
    for (unsigned i = 0; i < n; ++i)
      {
	const rule& g = r[i];
	if (g.group.empty () || !g.semantic || g.kind || g.caller
	    || !g.ns.empty ()
	    || !g.function.empty () || !g.file.empty ())
	  continue;
	std::string_view grp = g.group.view ();
	if (name == grp
	    || (name.size () > grp.size () && name.starts_with (grp)
		&& name[grp.size ()] == '.'))
	  return g.semantic;
      }
    return 0;
  }

  constexpr void index ()
  {
    for (unsigned c = 0; c < 2; ++c)
      for (unsigned k = 1; k < 4; ++k)
	for (unsigned i = 0; i < n; ++i)
	  if (r[i].caller == bool (c) && (!r[i].kind || r[i].kind == k))
	    cand[c][k][ncand[c][k]++] = i;
  }
};

// Whether group G covers NAME: NAME is G, or nested in it ("G.x").
constexpr bool
group_covers (std::string_view g, std::string_view name)
{
  return name == g || (name.size () > g.size () && name.starts_with (g)
		       && name[g.size ()] == '.');
}

// A file name matches an entry naming it, or naming a trailing part of
// its path.
constexpr bool
file_matches (std::string_view entry, std::string_view file)
{
  return file == entry
	 || (file.size () > entry.size () && file.ends_with (entry)
	     && file[file.size () - entry.size () - 1] == '/');
}

template <const rules_t& Rules>
struct configured
{
  // The names of the namespaces enclosing a function, outermost first.
  struct ns_chain
  {
    std::string_view name[16];
    unsigned n = 0;
  };

  static consteval ns_chain
  namespaces_of (std::meta::info p)
  {
    std::string_view inner[16];
    unsigned k = 0;
    for (; p != ^^::; p = std::meta::parent_of (p))
      if (std::meta::is_namespace (p) && k < 16)
	inner[k++] = std::meta::identifier_of (p);
    ns_chain c;
    while (k)
      c.name[c.n++] = inner[--k];
    return c;
  }

  // Whether the namespace named by ENTRY ("a::b", split into its parts)
  // is the chain C or encloses it: its K parts are the K outermost names
  // of C, compared innermost first.
  static constexpr bool
  ns_chain_matches (const fstr& entry, const ns_chain& c)
  {
    if (entry.parts > c.n)
      return false;
    for (unsigned i = entry.parts; i-- > 0;)
      if (c.name[i] != entry.part (i))
	return false;
    return true;
  }

  // Whether "a::C::f" (split into its parts) names FN: its last part is
  // FN's name (its template's, for a specialization), and the others,
  // innermost first, the enclosing classes and namespaces up to the
  // global namespace.
  static consteval bool
  function_matches (const fstr& entry, std::meta::info fn)
  {
    auto named = (std::meta::has_template_arguments (fn)
		  ? std::meta::template_of (fn) : fn);
    unsigned i = entry.parts;
    if (!std::meta::has_identifier (named)
	|| std::meta::identifier_of (named) != entry.part (--i))
      return false;
    auto scope = std::meta::parent_of (fn);
    while (i-- > 0)
      {
	if (scope == ^^:: || !std::meta::has_identifier (scope)
	    || std::meta::identifier_of (scope) != entry.part (i))
	  return false;
	scope = std::meta::parent_of (scope);
      }
    return scope == ^^::;
  }

  // Whether rule R matches the assertion D describes (CHAIN its
  // namespaces), its group being GROUP (empty: none).
  static consteval bool
  matches (const rule& r, const std::contracts::assertion_declaration_info& d,
	   const ns_chain& chain, std::string_view group)
  {
    const assertion_static_info& info = d.info;
    bool in_fn = d.function != std::meta::info{};
    auto loc = info.location ();
    if (!r.group.empty ()
	&& (group.empty () || !group_covers (r.group.view (), group)))
      return false;
    if (!r.ns.empty () && !ns_chain_matches (r.ns, chain))
      return false;
    if (!r.function.empty ()
	&& !(in_fn && function_matches (r.function, d.function)))
      return false;
    if (!r.file.empty ())
      {
	if (!file_matches (r.file.view (), loc.file_name ()))
	  return false;
	bool in = r.nlines == 0;
	for (unsigned j = 0; j < r.nlines; ++j)
	  if (loc.line () >= r.lines[2 * j]
	      && loc.line () <= r.lines[2 * j + 1])
	    in = true;
	if (!in)
	  return false;
      }
    return true;
  }

  // The semantic of the first rule with one matching the assertion D
  // describes, else the one it already has.  The group the rules naming
  // one match is the one the first matching rule with a "group" output
  // assigns, else the compiler's configuration's.  The compiler then
  // gives this assertion that semantic: in the static info of the other
  // queries and in the assertion_context of operator().
  static consteval evaluation_semantic
  compute_semantic (std::contracts::assertion_declaration_info d)
  {
    const assertion_static_info& info = d.info;
    // The namespaces enclosing the assertion, from the one the compiler
    // gives (that of its function, or where it is).
    ns_chain chain = namespaces_of (d.enclosing_namespace);
    bool caller = (info.side () == assertion_check_side::client
		   && !info.is_virtual ());
    unsigned kind = unsigned (info.kind ());
    std::string_view group = info.configured_group ();
    for (unsigned j = 0; j < Rules.ncand[caller][kind]; ++j)
      {
	const rule& r = Rules.r[Rules.cand[caller][kind][j]];
	if (!r.out_group.empty () && matches (r, d, chain, group))
	  {
	    group = r.out_group.view ();
	    break;
	  }
      }
    for (unsigned j = 0; j < Rules.ncand[caller][kind]; ++j)
      {
	const rule& r = Rules.r[Rules.cand[caller][kind][j]];
	if (r.semantic && matches (r, d, chain, group))
	  return evaluation_semantic (r.semantic);
      }
    return info.semantic ();
  }

  // No is_ignored: an assertion is ignored when the semantic computed for
  // it is ignore.  From here on, as default_v: semantic() is the computed
  // one.

  constexpr void
  operator() (const assertion_context& ctx) const
  {
    if (ctx.check ())
      return;
    evaluation_semantic sem = ctx.semantic ();
    if (sem != evaluation_semantic::quick_enforce)
      std::contracts::invoke_violation_handler
	(ctx.info (), std::contracts::detection_mode::predicate_false);
    if (sem != evaluation_semantic::observe)
      std::terminate ();
  }
};

} // namespace cfg

#endif
