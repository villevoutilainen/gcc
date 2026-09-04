/* P3589 Profiles framework, for the C++ front end.

   Copyright (C) 2026 Free Software Foundation, Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 3, or (at your option)
any later version.

GCC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING3.  If not see
<http://www.gnu.org/licenses/>.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "cp-tree.h"
#include "stringpool.h"
#include "diagnostic.h"
#include "attribs.h"
#include "context.h"
#include "tree-pass.h"
#include "input.h"
#include "c-family/c-pragma.h"
#include "profiles.h"

/* Defined in init-profile-gimple.cc.  */
extern gimple_opt_pass *make_pass_init_profile_gimple (gcc::context *ctxt);
/* Defined in invalidation-profile-gimple.cc.  */
extern gimple_opt_pass *make_pass_invalidation_profile_gimple (gcc::context *ctxt);

/* A minimal, fixed table -- "std::init" (P4222) and "std::invalidation"
   (P3446/P4296), the informal names each profile is already known by
   (see the CppCon 2026 "Profiles" talk).  Grows into a real,
   open-ended, string-keyed table (matching e.g. gcc/opts.cc's own
   sanitizer_opts[] as a structural template) if a third profile ever
   needs more than a fixed handful of entries to justify the extra
   machinery.  */

struct profiles_registry_entry
{
  const char *name;
  unsigned bit;
};

static constexpr profiles_registry_entry profiles_registry[] =
{
  { "std::init", 1u << 0 },
  { "std::invalidation", 1u << 1 },
};

static unsigned
profiles_lookup (const char *name)
{
  for (const auto &entry : profiles_registry)
    if (strcmp (entry.name, name) == 0)
      return entry.bit;
  return 0;
}

/* Which profiles are enforced, for the whole translation unit.  Sound
   for Increment 1 specifically because of the placement restriction
   profiles_handle_enforce_attribute enforces below: profiles::enforce
   must appear at global scope before any non-empty-declaration, so
   "enforced at all" and "enforced for the entire TU" coincide.  A
   module-declaration-scoped dominion (P3589's own more general model)
   is later work; nothing here assumes it won't need revisiting then.
   One bitmask per compilation, never reset: GCC only ever compiles one
   TU per process.  */
static unsigned profiles_enforced_mask;

/* True once cp_parser_declaration (parser.cc) has processed anything
   other than an empty-declaration.  See profiles_note_nonempty_
   declaration's own call site for why this is a sound, unified signal
   rather than a per-declaration-kind audit.  */
static bool profiles_seen_nonempty_declaration_p;

void
profiles_note_nonempty_declaration (void)
{
  profiles_seen_nonempty_declaration_p = true;
}

/* ATTR is a profiles::enforce attribute (TREE_VALUE (ATTR) is the
   single-element TREE_LIST cp_parser_profiles_attribute_args built,
   whose own TREE_VALUE is the profile-name IDENTIFIER_NODE -- see that
   function's own comment in parser.cc for the current, deliberately
   minimal grammar). Validates placement and registers the profile as
   enforced for the rest of the translation unit.  */

static void
profiles_handle_enforce_attribute (tree attr, location_t loc)
{
  tree arg_list = TREE_VALUE (attr);
  if (arg_list == NULL_TREE || arg_list == error_mark_node)
    /* Parse error already diagnosed by cp_parser_profiles_attribute_args.  */
    return;
  tree name_id = TREE_VALUE (arg_list);

  if (current_namespace != global_namespace)
    {
      error_at (loc, "%<profiles::enforce%> only allowed at global scope");
      return;
    }
  if (profiles_seen_nonempty_declaration_p)
    {
      error_at (loc, "%<profiles::enforce%> must appear before any "
		"declaration in the translation unit");
      return;
    }

  const char *name = IDENTIFIER_POINTER (name_id);
  unsigned bit = profiles_lookup (name);
  if (!bit)
    {
      error_at (loc, "unknown profile %qs", name);
      return;
    }
  profiles_enforced_mask |= bit;
}

/* True if the named profile is enforced for this translation unit.
   NAME matches profiles_registry's own spelling, e.g. "std::init".  */

bool
profiles_enforced_p (const char *name)
{
  unsigned bit = profiles_lookup (name);
  return bit != 0 && (profiles_enforced_mask & bit) != 0;
}

/* P3589, Phase 5: registered exemptions -- one entry per
   '[[profiles::exempt(profile, angle_header: "NAME")]]'/
   quote_header:.  A plain vec, not GTY-marked: this bookkeeping is
   pure front-end, compile-time-only state, exactly like profiles_
   enforced_mask above -- never touched by the GC or persisted
   anywhere.  */

struct profiles_exemption
{
  unsigned profile_bit;
  bool angle;
  const char *header_name;
};

static vec<profiles_exemption> profiles_exemptions;

/* ATTR is a profiles::exempt attribute (TREE_VALUE (ATTR) is the
   nested TREE_LIST cp_parser_profiles_exempt_args built -- see that
   function's own comment in parser.cc for the exact shape).
   Validates placement (the same "before any declaration" restriction
   profiles_handle_enforce_attribute has, and for the same reason: an
   exemption must be visible before anything -- including the
   exempted header's own contents, wherever it's #included -- gets
   checked against it) and registers the exemption for the rest of
   the translation unit.  */

static void
profiles_handle_exempt_attribute (tree attr, location_t loc)
{
  tree arg_list = TREE_VALUE (attr);
  if (arg_list == NULL_TREE || arg_list == error_mark_node)
    /* Parse error already diagnosed by cp_parser_profiles_exempt_args.  */
    return;

  tree profile_name_id = TREE_PURPOSE (arg_list);
  tree rest = TREE_VALUE (arg_list);
  tree angle_cst = TREE_PURPOSE (rest);
  tree header_str = TREE_VALUE (rest);

  if (current_namespace != global_namespace)
    {
      error_at (loc, "%<profiles::exempt%> only allowed at global scope");
      return;
    }
  if (profiles_seen_nonempty_declaration_p)
    {
      error_at (loc, "%<profiles::exempt%> must appear before any "
		"declaration in the translation unit");
      return;
    }

  const char *profile_name = IDENTIFIER_POINTER (profile_name_id);
  unsigned bit = profiles_lookup (profile_name);
  if (!bit)
    {
      error_at (loc, "unknown profile %qs", profile_name);
      return;
    }

  profiles_exemption exemption;
  exemption.profile_bit = bit;
  exemption.angle = !integer_zerop (angle_cst);
  exemption.header_name = xstrdup (TREE_STRING_POINTER (header_str));
  profiles_exemptions.safe_push (exemption);
}

/* Give STD_ATTRS -- the attribute-specifier-seq of an empty-declaration
   at location ATTRS_LOC -- real semantic meaning, matching what the
   standard actually says ("the attribute-specifier-seq appertains to
   the empty-declaration") instead of the unconditional "attribute
   ignored" warning cp_parser_declaration used to give it regardless of
   what the attribute was.

   profiles::enforce and profiles::exempt (Phase 5) are recognized so
   far. This deliberately does not route through decl_attributes
   (attribs.cc): that function requires a real decl/type as its own
   first argument and is used throughout the compiler on that
   assumption, so making arbitrary attribute-table entries (including
   plugin-registered ones) usable on an empty-declaration -- which has
   no decl to attach to at all -- is a separate, materially larger
   undertaking than this one attribute namespace needs, not something
   to fold in here.  Anything this function doesn't recognize keeps
   exactly the same generic warning empty-declarations have always
   gotten.  */

void
cp_finish_empty_declaration (location_t attrs_loc, tree std_attrs)
{
  bool recognized_any = false;
  for (tree a = std_attrs; a; a = TREE_CHAIN (a))
    {
      tree name = get_attribute_name (a);
      tree ns = get_attribute_namespace (a);
      if (ns == profiles_identifier && is_attribute_p ("enforce", name))
	{
	  profiles_handle_enforce_attribute (a, attrs_loc);
	  recognized_any = true;
	}
      else if (ns == profiles_identifier && is_attribute_p ("exempt", name))
	{
	  profiles_handle_exempt_attribute (a, attrs_loc);
	  recognized_any = true;
	}
    }

  if (!recognized_any
      && std_attrs != NULL_TREE
      && any_nonignored_attribute_p (std_attrs))
    warning_at (attrs_loc, OPT_Wattributes, "attribute ignored");
}

void
init_profiles (void)
{
  struct register_pass_info pass_info;
  pass_info.pass = make_pass_init_profile_gimple (g);
  pass_info.reference_pass_name = "ssa";
  pass_info.ref_pass_instance_number = 1;
  pass_info.pos_op = PASS_POS_INSERT_AFTER;
  register_pass (&pass_info);

  struct register_pass_info inv_pass_info;
  inv_pass_info.pass = make_pass_invalidation_profile_gimple (g);
  inv_pass_info.reference_pass_name = "ssa";
  inv_pass_info.ref_pass_instance_number = 1;
  inv_pass_info.pos_op = PASS_POS_INSERT_AFTER;
  register_pass (&inv_pass_info);
}

bool
profiles_uninit_pointee_p (tree decl)
{
  return lookup_attribute ("ref_to_uninit", DECL_ATTRIBUTES (decl)) != NULL_TREE
	 || lookup_attribute ("must_init", DECL_ATTRIBUTES (decl)) != NULL_TREE;
}

/* True if EXP -- the operand of a delete-expression, after ordinary
   expression semantics have run but before delete_sanity's own
   pointer-conversion (decl2.cc) -- is a reference to a declaration
   carrying [[owning_ptr]].  P4296R0's Negative Baseline (S7.2,
   [ub:expr.delete.mismatch]) requires this of every deleted pointer;
   anything this can't trace back to a single, directly-named
   declaration (an arbitrary expression, a temporary, a call result)
   is conservatively treated as NOT owning -- "erring on the safe
   side", the same stance the paper itself takes throughout S7.2.  */

bool
profiles_not_invalidating_p (tree fndecl)
{
  return lookup_attribute ("not_invalidating", DECL_ATTRIBUTES (fndecl))
	 != NULL_TREE;
}

bool
profiles_owning_ptr_p (tree exp)
{
  STRIP_ANY_LOCATION_WRAPPER (exp);
  while (CONVERT_EXPR_P (exp) || TREE_CODE (exp) == NON_LVALUE_EXPR)
    {
      exp = TREE_OPERAND (exp, 0);
      STRIP_ANY_LOCATION_WRAPPER (exp);
    }
  if (TREE_CODE (exp) == COMPONENT_REF)
    exp = TREE_OPERAND (exp, 1);
  if (!DECL_P (exp))
    return false;
  return lookup_attribute ("owning_ptr", DECL_ATTRIBUTES (exp)) != NULL_TREE;
}

bool
profiles_uninit_flavor_at_position_p (tree fndecl, unsigned position,
				      bool must_init_only)
{
  tree marker = lookup_attribute ("profiles_uninit_flavor",
				  DECL_ATTRIBUTES (fndecl));
  if (!marker)
    return false;
  for (tree e = TREE_VALUE (marker); e; e = TREE_CHAIN (e))
    if (TREE_INT_CST_LOW (TREE_PURPOSE (e)) == position)
      return !must_init_only || TREE_INT_CST_LOW (TREE_VALUE (e)) != 0;
  return false;
}

bool
profiles_header_exempt_p (location_t loc, const char *profile_name)
{
  if (profiles_exemptions.is_empty ())
    return false;

  unsigned bit = profiles_lookup (profile_name);
  if (!bit)
    return false;

  const char *resolved_path = LOCATION_FILE (loc);
  if (!resolved_path)
    return false;

  const char *spelled_name;
  bool angle;
  if (!cpp_get_include_spelling (parse_in, resolved_path, &spelled_name,
				 &angle))
    return false;

  for (unsigned i = 0; i < profiles_exemptions.length (); ++i)
    {
      const profiles_exemption &e = profiles_exemptions[i];
      if (e.profile_bit == bit && e.angle == angle
	  && strcmp (e.header_name, spelled_name) == 0)
	return true;
    }
  return false;
}
