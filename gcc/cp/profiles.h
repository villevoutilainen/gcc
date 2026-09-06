/* Declarations for the P3589 Profiles framework, for the C++ front end.
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

#ifndef GCC_CP_PROFILES_H
#define GCC_CP_PROFILES_H

/* Called by cp_parser_declaration (parser.cc) the moment it commits to
   parsing anything other than an empty-declaration -- see that
   function's own call site for why this single spot is a sound,
   unified chokepoint for "has any non-empty declaration happened yet
   in this translation unit", covering every top-level and
   namespace-scope declaration kind by construction, not by an
   incomplete per-kind audit.  */
extern void profiles_note_nonempty_declaration (void);

/* Called by cp_parser_declaration in place of the bare "attribute
   ignored" warning empty-declarations previously got unconditionally.
   Gives the attribute-specifier-seq of an empty-declaration a real
   semantic dispatch point, matching what the standard actually says
   ("the attribute-specifier-seq appertains to the empty-declaration")
   -- currently only profiles::enforce is recognized; anything else
   still falls back to the same generic warning as before.  */
extern void cp_finish_empty_declaration (location_t, tree);

/* True if the named profile (e.g. "std::init") is enforced for this
   translation unit.  */
extern bool profiles_enforced_p (const char *);

/* Register the profile-checking GIMPLE passes (currently just the
   P4222 Initialization profile's) -- called once, early, the same way
   init_contracts (contracts.cc) registers D4324's own experimental
   GIMPLE engine.  Unlike that engine, registration itself is never
   gated on a command-line flag: a profile can be enabled from source
   (profiles::enforce) or, non-intrusively, from the command line
   (-fprofiles-enforced=, see profiles_process_command_line_
   enforcement just below), so registration stays unconditional and
   each pass's own gate () is what makes it a no-op when its profile
   isn't enforced by either means.  */
extern void init_profiles (void);

/* Apply every -fprofiles-enforced=name[,name...] occurrence
   (c-family/c-opts.cc's own deferred profiles_enforced_table) to
   profiles_enforced_mask, the same bit '[[profiles::enforce(name)]]'
   itself sets -- letting an unmodified translation unit be compiled
   under an enforced profile without adding that attribute to its
   source.  Called once, from cxx_init_decl_processing (decl.cc)
   right after init_profiles, before any parsing begins.  */
extern void profiles_process_command_line_enforcement (void);

/* Run both profile-checking GIMPLE passes on FNDECL's body right now,
   eagerly, rather than waiting for the normal end-of-compilation
   pipeline -- see the definition in profiles.cc for why (in short:
   that pipeline's own driver skips every function once the whole
   translation unit has any front-end error anywhere in it).  Called
   from expand_or_defer_fn (semantics.cc) once FNDECL's body is
   otherwise complete.  A cheap no-op unless std::init or
   std::invalidation is actually enforced.  */
extern void profiles_eager_check_function (tree fndecl);

/* True if DECL (a PARM_DECL or VAR_DECL of pointer or reference type)
   is flavored "points to [[uninit]] memory" -- carries
   [[ref_to_uninit]] directly, or [[must_init]], which implies it
   (P4222 S9.3: "[[must_init]] implies [[ref_to_uninit]]").  Shared
   between the front-end declaration-time check (decl.cc) and the
   GIMPLE-level call-site/dominance checker (init-profile-gimple.cc), so
   both sides agree on exactly one definition of "uninit-flavored" --
   this function itself never inspects DECL's type at all (a pure
   attribute-presence check), so it already worked for a reference the
   moment tree.cc's attribute handlers started accepting one; only they
   ever actually gated on POINTER_TYPE.  */
extern bool profiles_uninit_pointee_p (tree decl);

/* True if FNDECL's parameter at 1-based POSITION carries
   [[ref_to_uninit]] or [[must_init]] -- consults the synthesized
   function-level "profiles_uninit_flavor" marker (grokfndecl, decl.cc)
   rather than the PARM_DECL directly, so this still answers correctly
   even when FNDECL is only declared, never defined, in this
   translation unit (see that marker's own comment for why a direct
   PARM_DECL lookup can't be trusted to survive that case).  If
   MUST_INIT_ONLY, only a [[must_init]] parameter at that position
   counts -- used by the caller-side postcondition check ("does this
   call establish the argument as now-initialized"), which a plain
   [[ref_to_uninit]] parameter does not.  */
extern bool profiles_uninit_flavor_at_position_p (tree fndecl,
						   unsigned position,
						   bool must_init_only);

/* P3589, Phase 5: true if LOC is exempt from PROFILE_NAME (e.g.
   "std::init"). Three independent sources of exemption:

   1. LOC is in a system header (in_system_header_at) -- automatic,
      unconditional, no declaration needed. This is what makes
      '#include <vector>' (or any other unannotated standard-library
      header) usable at all under an enforced profile today, ahead of
      the standard library itself being annotated.

   2. LOC falls within a '[[profiles::suppress(profile)]]' attached to
      the enclosing declaration -- see profiles_register_suppression's
      own comment for how that range gets established and what
      "enclosing" means precisely.

   3. LOC's own file, OR ANY FILE ON ITS #include CHAIN UP TO THE MAIN
      FILE, was named by an explicit
      '[[profiles::exempt(profile, angle_header: "NAME")]]' /
      quote_header: with a matching angle/quote-ness -- for user code
      the profile can't yet be made to accept, or a non-system
      third-party header. Transitive the same way (1) is: exempting
      "vector" exempts LOC whether LOC's own file is <vector> itself
      or anything <vector> transitively #includes.

   Every diagnostic site across this project's own profile checkers
   (decl.cc, tree.cc's attribute handlers, init.cc, init-profile-
   gimple.cc) is expected to consult this before actually emitting,
   the same "always check, never skip" discipline profiles_enforced_p
   itself already has -- consult profiles.cc's own profiles_handle_
   exempt_attribute for why explicit exemptions are only resolvable
   via libcpp's cpp_get_include_spelling, not from line_map_ordinary
   alone.  */
extern bool profiles_header_exempt_p (location_t loc, const char *profile_name);

/* The GIMPLE-level checkers' (init-profile-gimple.cc, invalidation-
   profile-gimple.cc) own counterpart to profiles_header_exempt_p above
   -- not a fourth, independent exemption source, but a more robust way
   of checking source (1) (system header) specifically for a GIMPLE
   diagnostic, whose own triggering statement's location can be
   missing/synthetic (an optimizer-merged or otherwise unattributed
   block -- confirmed directly: a template instantiation's own body can
   have a real statement with no valid location at all, even while
   every OTHER statement in that same function correctly carries the
   template's own header-based locations) in a way a front-end
   declaration's own location never is.  FNDECL is the enclosing
   function being checked (its own DECL_SOURCE_LOCATION, unlike a
   specific statement's, is never subject to that degradation, and --
   this is the key guarantee this function relies on -- a function
   TEMPLATE's own instantiations keep DECL_SOURCE_LOCATION at the
   template PATTERN's own definition site, not the instantiation point,
   so a function whose template was written in an exempted header is
   itself exempt via this fallback, regardless of which specific
   per-statement location a given diagnostic would otherwise have
   used). Checks LOC first (the precise, common case), falling back to
   FNDECL's own location only if that fails -- this can only WIDEN
   exemption coverage versus calling profiles_header_exempt_p (loc, ...)
   directly, never narrow it.  */
extern bool profiles_diagnostic_exempt_p (location_t loc, tree fndecl,
					   const char *profile_name);

/* P3589: register that PROFILE_NAME's (e.g. "std::init") diagnostics
   are suppressed for the source range [START, END] -- the real
   semantic effect of '[[profiles::suppress(profile)]]' attached to an
   ordinary declaration OR STATEMENT (the paper's own wording grants
   both equally: "The dominion of a profile-suppression attribute is
   the sequence of tokens starting after the attribute extending till
   the last token of the declaration or statement to which the
   attribute appertains"). Called from cp_finish_decl (decl.cc, for a
   declaration -- tree.cc's own handle_profiles_suppress_attribute is
   what lets the attribute survive parsing onto DECL_ATTRIBUTES in the
   first place, unlike profiles::enforce/exempt) or cp_parser_statement
   (parser.cc, for an ordinary statement) once the declaration/
   statement carrying the attribute is fully parsed and its own extent
   is therefore known: START is the declaration's own DECL_SOURCE_
   LOCATION (or the statement's own first-token location), END is
   input_location at that point (just past the trailing ';', the
   earliest point either caller can be reached). Diagnoses an
   unrecognized PROFILE_NAME at START, matching profiles_handle_exempt_
   attribute's own "unknown profile" error for the identical situation
   -- the attribute's own parse accepts an arbitrary dotted identifier
   without validating it, so this is the first point that can.  */
extern void profiles_register_suppression (const char *profile_name,
					    location_t start, location_t end);

/* Shared implementation for both call sites above: walk every
   '[[profiles::suppress(profile)]]' found directly in ATTRS (a
   DECL_ATTRIBUTES list or a parsed statement attribute-specifier-seq)
   and register [START, END] as suppressed for each one's named
   profile via profiles_register_suppression.  Does not strip the
   found attributes back out of ATTRS -- callers that need that (e.g.
   cp_parser_statement, so its own generic "attribute ignored" warning
   doesn't also fire for an attribute this already gave real meaning
   to) do so themselves with remove_attribute, same as this file's own
   handling of [[fallthrough]]/[[assume]].  */
extern void profiles_process_suppress_attributes (tree attrs,
						   location_t start,
						   location_t end);

/* P3446R0/P4296R0, Phase 7a: true if EXP (an expression, taken
   verbatim from the delete-expression's own operand in decl2.cc's
   delete_sanity) resolves to a declaration carrying [[owning_ptr]].
   See profiles.cc's own definition for exactly how much of EXP's
   shape this can see through.  */
extern bool profiles_owning_ptr_p (tree exp);

/* P3446R0, Phase 7b: true if FNDECL (a non-static member function)
   carries [[not_invalidating]] -- see invalidation-profile-gimple.cc's
   own ip_mutating_call_p for the single call site.  */
extern bool profiles_not_invalidating_p (tree fndecl);

/* P3446R0: true if FNDECL's parameter at 1-based POSITION carries
   [[not_invalidating]] -- consults the synthesized function-level
   "profiles_not_invalidating_flavor" marker (grokfndecl, decl.cc),
   the free-function analogue of profiles_not_invalidating_p above;
   see that marker's own comment (tree.cc's handle_not_invalidating_
   attribute) for why a direct PARM_DECL lookup can't be trusted to
   survive a declared-but-never-defined function.  */
extern bool profiles_not_invalidating_at_position_p (tree fndecl,
						      unsigned position);

#endif /* ! GCC_CP_PROFILES_H */
