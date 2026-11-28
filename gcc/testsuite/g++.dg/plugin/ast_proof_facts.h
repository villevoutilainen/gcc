/* ast_proof_facts.h: the facts and the walk of the AST proof plugins
   (conveyor_proof_plugin.cc, symbolic_proof_plugin.cc).

   An alternative to the built-in AST engine's proof checks, with facts of
   its own: the plugins walk a function's body (pre-genericize GENERIC)
   themselves, keep what the code, the function's own preconditions
   (self-trust), contract_asserts and callees' postconditions establish,
   and check each call's, contract_assert's and the function's own
   postconditions' conjuncts against it.  Only the contract-text shape
   recognizers (oa_match_*, is_object_address_call_p, ...), the contract
   property queries (which flavors a contract is active in, strictness,
   forcing) and the argument substitution helpers are the engine's.

   Facts are kept in two sets: what a conveyor obligation may use (the
   code's own facts and conveyor contracts'), and everything (symbolic
   contracts' facts too).  A fact is about a key: a variable, a member of
   an object ('p->f', 'o.f'), an accessor call on an object ('v.size ()',
   a call to a conveyor or symbolic function, which depends on its
   arguments only), or a call's own result.  Kinds:

   - integer and floating-point ranges of a key;
   - relations 'A - B - OFF' in {<0, =0, >0} (a mask) between two keys;
   - atoms: boolean expressions known true or false ('pred (p)',
     'rel (a, b)');
   - object addresses (is_object_address) and nonzero pointers;
   - aliases: a pointer variable known to point to an object.

   Facts are a must-analysis: at a join, what holds on every path; loops
   are a fixed point.  An assignment ends what was known of what it
   writes; a call ends what was known of the objects it is given a route
   to and of locals whose address escaped.  */

#ifndef AST_PROOF_FACTS_H
#define AST_PROOF_FACTS_H

#include "basic-block.h"
#include "gimple.h"
#include "gimple-range.h"
#include "range-op.h"
#include "tree-pretty-print.h"
#include "langhooks.h"
#include "tree-iterator.h"

namespace apf {

/* Debug output (APF_DUMP set in the environment).  */
static bool dump_p;

/* ---------------------------------------------------------------------- */
/* Keys.  */

static tree strip (tree t);

/* Whether a conversion from FROM to TO keeps every value of FROM.  */

static bool
value_preserving_conversion_p (tree to, tree from)
{
  if (!TYPE_P (to) || !TYPE_P (from))
    return false;
  if (POINTER_TYPE_P (to) && POINTER_TYPE_P (from))
    return true;
  if (TREE_CODE (to) == REFERENCE_TYPE || TREE_CODE (from) == REFERENCE_TYPE)
    return true;
  if (INTEGRAL_TYPE_P (to) && INTEGRAL_TYPE_P (from))
    {
      if (TREE_CODE (to) == BOOLEAN_TYPE && TREE_CODE (from) != BOOLEAN_TYPE)
	return false;
      unsigned tp = TYPE_PRECISION (to), fp = TYPE_PRECISION (from);
      if (TYPE_UNSIGNED (to) == TYPE_UNSIGNED (from))
	return tp >= fp;
      if (TYPE_UNSIGNED (from))
	return tp > fp;
      return false;
    }
  if (SCALAR_FLOAT_TYPE_P (to) && SCALAR_FLOAT_TYPE_P (from))
    return TYPE_PRECISION (to) >= TYPE_PRECISION (from);
  if (TYPE_MAIN_VARIANT (to) == TYPE_MAIN_VARIANT (from))
    return true;
  return false;
}

/* T without location wrappers, value-preserving conversions, '*&x' and
   '&*p'.  */

static tree
strip (tree t)
{
  while (t)
    {
      t = tree_strip_any_location_wrapper (t);
      if (CONVERT_EXPR_P (t) || TREE_CODE (t) == NON_LVALUE_EXPR
	  || TREE_CODE (t) == VIEW_CONVERT_EXPR)
	{
	  tree op = TREE_OPERAND (t, 0);
	  if (op && TREE_TYPE (op) && TREE_TYPE (t)
	      && value_preserving_conversion_p (TREE_TYPE (t), TREE_TYPE (op)))
	    {
	      t = op;
	      continue;
	    }
	  return t;
	}
      if (INDIRECT_REF_P (t))
	{
	  tree op = tree_strip_any_location_wrapper (TREE_OPERAND (t, 0));
	  while (CONVERT_EXPR_P (op) || TREE_CODE (op) == NON_LVALUE_EXPR)
	    op = tree_strip_any_location_wrapper (TREE_OPERAND (op, 0));
	  if (TREE_CODE (op) == ADDR_EXPR)
	    {
	      t = TREE_OPERAND (op, 0);
	      continue;
	    }
	  return t;
	}
      if (TREE_CODE (t) == ADDR_EXPR)
	{
	  tree op = tree_strip_any_location_wrapper (TREE_OPERAND (t, 0));
	  if (INDIRECT_REF_P (op))
	    {
	      t = TREE_OPERAND (op, 0);
	      continue;
	    }
	  return t;
	}
      if (VAR_P (t) && DECL_HAS_VALUE_EXPR_P (t))
	{
	  t = DECL_VALUE_EXPR (t);
	  continue;
	}
      if (TREE_CODE (t) == SAVE_EXPR || TREE_CODE (t) == CLEANUP_POINT_EXPR
	  || TREE_CODE (t) == PAREN_EXPR)
	{
	  t = TREE_OPERAND (t, 0);
	  continue;
	}
      /* A temporary: its slot, which the walk gave the facts of its
	 initializer.  */
      if (TREE_CODE (t) == TARGET_EXPR)
	{
	  /* A plain copy of a value: that value.  */
	  tree init = tree_strip_any_location_wrapper (TARGET_EXPR_INITIAL (t));
	  if (init && !VOID_TYPE_P (TREE_TYPE (init))
	      && TREE_CODE (init) != CALL_EXPR
	      && TREE_CODE (init) != AGGR_INIT_EXPR
	      && TREE_CODE (init) != CONSTRUCTOR
	      && (TREE_CODE (init) == TARGET_EXPR || !TREE_SIDE_EFFECTS (init)))
	    {
	      t = init;
	      continue;
	    }
	  return TARGET_EXPR_SLOT (t);
	}
      /* A conversion operator's result stands for its object, as the
	 engine reads it ('q < 5' with q of class type).  */
      if (TREE_CODE (t) == CALL_EXPR && call_expr_nargs (t) == 1)
	{
	  tree fn = cp_get_callee_fndecl_nofold (t);
	  if (fn && DECL_CONV_FN_P (fn) && DECL_IOBJ_MEMBER_FUNCTION_P (fn))
	    {
	      tree a = tree_strip_any_location_wrapper (CALL_EXPR_ARG (t, 0));
	      STRIP_NOPS (a);
	      if (TREE_CODE (a) == ADDR_EXPR)
		{
		  t = TREE_OPERAND (a, 0);
		  continue;
		}
	      if (POINTER_TYPE_P (TREE_TYPE (a)))
		return build1 (INDIRECT_REF, TREE_TYPE (TREE_TYPE (a)), a);
	    }
	}
      return t;
    }
  return t;
}

/* Whether calls to FN have a value depending on their arguments only, so
   that two calls with equal arguments are one key: a conveyor or symbolic
   function (an accessor, a predicate).  */

static bool
pure_callee_p (tree fn)
{
  return fn && TREE_CODE (fn) == FUNCTION_DECL
	 && (DECL_DECLARED_CONVEYOR_P (fn) || DECL_DECLARED_SYMBOLIC_P (fn)
	     || DECL_CONV_FN_P (fn));
}

static tree call_fn (tree call);

/* The alias resolution in effect (state::resolve), for key_eq.  */
struct state;
static state *resolve_state;
static tree resolve (tree t);

/* Whether keys A and B denote the same value.  */

static bool
key_eq (tree a, tree b, int depth = 0)
{
  if (depth > 32)
    return false;
  a = strip (a);
  b = strip (b);
  if (a == b)
    return a != NULL_TREE;
  if (!a || !b)
    return false;
  if (resolve_state)
    {
      tree ra = resolve (a), rb = resolve (b);
      if (ra != a || rb != b)
	return key_eq (ra, rb, depth + 1);
    }
  if (TREE_CODE (a) != TREE_CODE (b))
    return false;
  switch (TREE_CODE (a))
    {
    case INTEGER_CST:
      return wi::to_widest (a) == wi::to_widest (b);
    case REAL_CST:
      return real_identical (TREE_REAL_CST_PTR (a), TREE_REAL_CST_PTR (b));
    case VAR_DECL:
    case PARM_DECL:
    case RESULT_DECL:
    case FUNCTION_DECL:
    case FIELD_DECL:
      return false;
    case COMPONENT_REF:
      return (TREE_OPERAND (a, 1) == TREE_OPERAND (b, 1)
	      && key_eq (TREE_OPERAND (a, 0), TREE_OPERAND (b, 0), depth + 1));
    case INDIRECT_REF:
    case ADDR_EXPR:
    case NEGATE_EXPR:
    case BIT_NOT_EXPR:
    case TRUTH_NOT_EXPR:
    case ABS_EXPR:
      return key_eq (TREE_OPERAND (a, 0), TREE_OPERAND (b, 0), depth + 1);
    case NOP_EXPR:
    case CONVERT_EXPR:
    case FLOAT_EXPR:
    case FIX_TRUNC_EXPR:
      return (TYPE_MAIN_VARIANT (TREE_TYPE (a))
	      == TYPE_MAIN_VARIANT (TREE_TYPE (b))
	      && key_eq (TREE_OPERAND (a, 0), TREE_OPERAND (b, 0), depth + 1));
    case ARRAY_REF:
    case PLUS_EXPR:
    case MINUS_EXPR:
    case MULT_EXPR:
    case TRUNC_DIV_EXPR:
    case TRUNC_MOD_EXPR:
    case RDIV_EXPR:
    case POINTER_PLUS_EXPR:
    case LT_EXPR: case LE_EXPR: case GT_EXPR: case GE_EXPR:
    case EQ_EXPR: case NE_EXPR:
    case TRUTH_ANDIF_EXPR: case TRUTH_ORIF_EXPR:
    case BIT_AND_EXPR: case BIT_IOR_EXPR: case LSHIFT_EXPR: case RSHIFT_EXPR:
      return (key_eq (TREE_OPERAND (a, 0), TREE_OPERAND (b, 0), depth + 1)
	      && key_eq (TREE_OPERAND (a, 1), TREE_OPERAND (b, 1), depth + 1));
    case CALL_EXPR:
      {
	tree fa = call_fn (a), fb = call_fn (b);
	if (!fa || fa != fb || !pure_callee_p (fa)
	    || call_expr_nargs (a) != call_expr_nargs (b))
	  return false;
	for (int i = 0; i < call_expr_nargs (a); ++i)
	  if (!key_eq (CALL_EXPR_ARG (a, i), CALL_EXPR_ARG (b, i), depth + 1))
	    return false;
	return true;
      }
    default:
      return false;
    }
}

/* The function CALL calls, or NULL_TREE.  */

static tree
call_fn (tree call)
{
  call = tree_strip_any_location_wrapper (call);
  if (TREE_CODE (call) == AGGR_INIT_EXPR)
    {
      tree fn = AGGR_INIT_EXPR_FN (call);
      STRIP_NOPS (fn);
      if (TREE_CODE (fn) == ADDR_EXPR)
	fn = TREE_OPERAND (fn, 0);
      return TREE_CODE (fn) == FUNCTION_DECL ? fn : NULL_TREE;
    }
  if (TREE_CODE (call) != CALL_EXPR)
    return NULL_TREE;
  if (CALL_EXPR_FN (call) == NULL_TREE)
    return NULL_TREE;
  tree fn = cp_get_callee_fndecl_nofold (call);
  return fn;
}

static unsigned
call_nargs (tree call)
{
  return (TREE_CODE (call) == AGGR_INIT_EXPR ? aggr_init_expr_nargs (call)
	  : call_expr_nargs (call));
}

static tree
call_arg (tree call, unsigned i)
{
  return (TREE_CODE (call) == AGGR_INIT_EXPR ? AGGR_INIT_EXPR_ARG (call, i)
	  : CALL_EXPR_ARG (call, i));
}

/* Whether expression T mentions key K (a subexpression equal to it).  */

struct mentions_data { tree key; bool found; };

static tree
mentions_r (tree *tp, int *walk_subtrees, void *data)
{
  mentions_data *d = (mentions_data *) data;
  if (TYPE_P (*tp))
    {
      *walk_subtrees = 0;
      return NULL_TREE;
    }
  if (*tp == d->key || key_eq (*tp, d->key))
    {
      d->found = true;
      return *tp;
    }
  return NULL_TREE;
}

static bool
mentions_p (tree t, tree key)
{
  if (!t || !key)
    return false;
  mentions_data d = { key, false };
  walk_tree_without_duplicates (&t, mentions_r, &d);
  return d.found;
}

/* Whether expression T mentions declaration D (by pointer).  */

static tree
mentions_decl_r (tree *tp, int *walk_subtrees, void *data)
{
  if (TYPE_P (*tp))
    *walk_subtrees = 0;
  else if (*tp == (tree) data)
    return *tp;
  return NULL_TREE;
}

/* Whether expression T reads the value of declaration D (a use not
   under '&').  */

static tree
reads_decl_r (tree *tp, int *walk_subtrees, void *data)
{
  if (TYPE_P (*tp) || TREE_CODE (*tp) == ADDR_EXPR)
    *walk_subtrees = 0;
  else if (*tp == (tree) data)
    return *tp;
  return NULL_TREE;
}

static bool
reads_decl_p (tree t, tree d)
{
  if (!t)
    return false;
  return walk_tree_without_duplicates (&t, reads_decl_r, d) != NULL_TREE;
}

static bool
mentions_decl_p (tree t, tree d)
{
  if (!t)
    return false;
  return walk_tree_without_duplicates (&t, mentions_decl_r, d) != NULL_TREE;
}

/* Whether T mentions key K as an object, by address ('&k').  */

static bool
mentions_by_address_p (tree t, tree k, int depth = 0)
{
  if (!t || depth > 100 || TYPE_P (t))
    return false;
  t = tree_strip_any_location_wrapper (t);
  /* '*&x' reads x's value.  */
  if (INDIRECT_REF_P (t))
    {
      tree op = strip (TREE_OPERAND (t, 0));
      if (op && TREE_CODE (op) == ADDR_EXPR)
	return mentions_by_address_p (TREE_OPERAND (op, 0), k, depth + 1);
    }
  if (TREE_CODE (t) == ADDR_EXPR && key_eq (TREE_OPERAND (t, 0), k))
    return true;
  if (!EXPR_P (t))
    return false;
  for (int i = 0; i < TREE_OPERAND_LENGTH (t); ++i)
    if (mentions_by_address_p (TREE_OPERAND (t, i), k, depth + 1))
      return true;
  return false;
}

/* Whether T reads memory: a member, a dereference, an array element, an
   accessor call.  */

static tree
reads_memory_r (tree *tp, int *walk_subtrees, void *)
{
  if (TYPE_P (*tp))
    {
      *walk_subtrees = 0;
      return NULL_TREE;
    }
  switch (TREE_CODE (*tp))
    {
    case INDIRECT_REF:
    case COMPONENT_REF:
    case ARRAY_REF:
    case CALL_EXPR:
      return *tp;
    default:
      return NULL_TREE;
    }
}

/* Call F on each memory read in key K: a member, an element, a whole
   object through a pointer, an accessor call.  The object expression a
   member or element is selected from is not itself a read (only the
   pointers loaded in it are).  */

template<typename F>
static void
for_each_read_1 (tree k, F &f, bool base, int depth)
{
  if (!k || depth > 64 || TYPE_P (k))
    return;
  k = tree_strip_any_location_wrapper (k);
  switch (TREE_CODE (k))
    {
    case COMPONENT_REF:
      if (!base)
	f (k);
      for_each_read_1 (TREE_OPERAND (k, 0), f, true, depth + 1);
      return;
    case ARRAY_REF:
      if (!base)
	f (k);
      for_each_read_1 (TREE_OPERAND (k, 0), f, true, depth + 1);
      for_each_read_1 (TREE_OPERAND (k, 1), f, false, depth + 1);
      return;
    case INDIRECT_REF:
      if (!base)
	f (k);
      for_each_read_1 (TREE_OPERAND (k, 0), f, false, depth + 1);
      return;
    case ADDR_EXPR:
      for_each_read_1 (TREE_OPERAND (k, 0), f, true, depth + 1);
      return;
    case CALL_EXPR:
      f (k);
      for (int i = 0; i < call_expr_nargs (k); ++i)
	for_each_read_1 (CALL_EXPR_ARG (k, i), f, false, depth + 1);
      return;
    default:
      if (EXPR_P (k))
	for (int i = 0; i < TREE_OPERAND_LENGTH (k); ++i)
	  for_each_read_1 (TREE_OPERAND (k, i), f, false, depth + 1);
      return;
    }
}

template<typename F>
static void
for_each_read (tree k, F f)
{
  for_each_read_1 (k, f, false, 0);
}

/* ---------------------------------------------------------------------- */
/* Integer ranges.  */

struct irng
{
  bool has_lo = false, has_hi = false;
  widest_int lo = 0, hi = 0;

  bool any () const { return has_lo || has_hi; }
  bool empty () const { return has_lo && has_hi && wi::lts_p (hi, lo); }
  static irng exact (const widest_int &v)
  {
    irng r;
    r.has_lo = r.has_hi = true;
    r.lo = r.hi = v;
    return r;
  }
  void intersect (const irng &o)
  {
    if (o.has_lo && (!has_lo || wi::gts_p (o.lo, lo)))
      lo = o.lo, has_lo = true;
    if (o.has_hi && (!has_hi || wi::lts_p (o.hi, hi)))
      hi = o.hi, has_hi = true;
  }
  /* The hull: what holds of either.  */
  void hull (const irng &o)
  {
    if (!o.has_lo)
      has_lo = false;
    else if (has_lo && wi::lts_p (o.lo, lo))
      lo = o.lo;
    if (!o.has_hi)
      has_hi = false;
    else if (has_hi && wi::gts_p (o.hi, hi))
      hi = o.hi;
  }
  bool operator== (const irng &o) const
  {
    return (has_lo == o.has_lo && has_hi == o.has_hi
	    && (!has_lo || lo == o.lo) && (!has_hi || hi == o.hi));
  }
};

/* The values of TYPE.  */

static irng
type_range (tree type)
{
  irng r;
  if (!type || !INTEGRAL_TYPE_P (type))
    return r;
  if (TREE_CODE (type) == BOOLEAN_TYPE)
    {
      r.has_lo = r.has_hi = true;
      r.lo = 0;
      r.hi = 1;
      return r;
    }
  r.has_lo = r.has_hi = true;
  r.lo = wi::to_widest (TYPE_MIN_VALUE (type));
  r.hi = wi::to_widest (TYPE_MAX_VALUE (type));
  return r;
}

/* Whether R lies within the values of TYPE.  */

static bool
fits_type_p (const irng &r, tree type)
{
  irng t = type_range (type);
  if (!t.any ())
    return false;
  return r.has_lo && r.has_hi && wi::les_p (t.lo, r.lo) && wi::les_p (r.hi, t.hi);
}

/* Orderings: bit 0 '<', bit 1 '=', bit 2 '>'.  */
enum { ORD_LT = 1, ORD_EQ = 2, ORD_GT = 4, ORD_ALL = 7 };

static int
code_mask (tree_code code)
{
  switch (code)
    {
    case LT_EXPR: return ORD_LT;
    case LE_EXPR: return ORD_LT | ORD_EQ;
    case GT_EXPR: return ORD_GT;
    case GE_EXPR: return ORD_GT | ORD_EQ;
    case EQ_EXPR: return ORD_EQ;
    case NE_EXPR: return ORD_LT | ORD_GT;
    default: return 0;
    }
}

static int
mirror_mask (int m)
{
  return (m & ORD_EQ) | ((m & ORD_LT) ? ORD_GT : 0) | ((m & ORD_GT) ? ORD_LT : 0);
}

/* The range of 'X' when 'X CODE C' holds (for '!=', none).  */

static irng
range_for_comparison (tree_code code, const widest_int &c)
{
  irng r;
  switch (code)
    {
    case LT_EXPR: r.has_hi = true; r.hi = c - 1; break;
    case LE_EXPR: r.has_hi = true; r.hi = c; break;
    case GT_EXPR: r.has_lo = true; r.lo = c + 1; break;
    case GE_EXPR: r.has_lo = true; r.lo = c; break;
    case EQ_EXPR: r = irng::exact (c); break;
    default: break;
    }
  return r;
}

/* Whether every value of R satisfies MASK relative to C (OA_PROVEN_TRUE),
   none does (OA_PROVEN_FALSE), or neither is known.  */

static oa_proof_result
range_mask_verdict (const irng &r, int mask, const widest_int &c)
{
  if (r.empty ())
    return OA_PROVEN_TRUE;
  /* The orderings R allows relative to C.  */
  int have = 0;
  if (!r.has_lo || wi::lts_p (r.lo, c))
    have |= ORD_LT;
  if ((!r.has_lo || wi::les_p (r.lo, c)) && (!r.has_hi || wi::les_p (c, r.hi)))
    have |= ORD_EQ;
  if (!r.has_hi || wi::gts_p (r.hi, c))
    have |= ORD_GT;
  if ((have & ~mask) == 0)
    return OA_PROVEN_TRUE;
  if ((have & mask) == 0)
    return OA_PROVEN_FALSE;
  return OA_UNKNOWN;
}

/* Interval arithmetic: A CODE B.  */

static bool
irng_binop (tree_code code, const irng &a, const irng &b, irng *out)
{
  irng r;
  switch (code)
    {
    case PLUS_EXPR:
      r.has_lo = a.has_lo && b.has_lo;
      r.has_hi = a.has_hi && b.has_hi;
      if (r.has_lo) r.lo = a.lo + b.lo;
      if (r.has_hi) r.hi = a.hi + b.hi;
      break;
    case MINUS_EXPR:
      r.has_lo = a.has_lo && b.has_hi;
      r.has_hi = a.has_hi && b.has_lo;
      if (r.has_lo) r.lo = a.lo - b.hi;
      if (r.has_hi) r.hi = a.hi - b.lo;
      break;
    case MULT_EXPR:
      {
	if (!(a.has_lo && a.has_hi && b.has_lo && b.has_hi))
	  {
	    /* Half-bounded, each of one sign.  */
	    int sa = (a.has_lo && wi::ges_p (a.lo, 0)) ? 1
		     : (a.has_hi && wi::les_p (a.hi, 0)) ? -1 : 0;
	    int sb = (b.has_lo && wi::ges_p (b.lo, 0)) ? 1
		     : (b.has_hi && wi::les_p (b.hi, 0)) ? -1 : 0;
	    if (!sa || !sb)
	      return false;
	    /* The magnitudes: at least |the bound nearest zero|.  */
	    widest_int ma = sa > 0 ? a.lo : -a.hi;
	    widest_int mb = sb > 0 ? b.lo : -b.hi;
	    widest_int m = ma * mb;
	    if (sa * sb > 0)
	      r.has_lo = true, r.lo = m;
	    else
	      r.has_hi = true, r.hi = -m;
	    break;
	  }
	widest_int c[4] = { a.lo * b.lo, a.lo * b.hi, a.hi * b.lo, a.hi * b.hi };
	r.has_lo = r.has_hi = true;
	r.lo = r.hi = c[0];
	for (int i = 1; i < 4; ++i)
	  {
	    if (wi::lts_p (c[i], r.lo)) r.lo = c[i];
	    if (wi::gts_p (c[i], r.hi)) r.hi = c[i];
	  }
	break;
      }
    case TRUNC_DIV_EXPR:
      {
	if (!(a.has_lo && a.has_hi) && b.has_lo && b.has_hi && b.lo == b.hi
	    && b.lo != 0)
	  {
	    /* Half-bounded by a constant: monotone.  */
	    const widest_int &d = b.lo;
	    bool pos = wi::gts_p (d, 0);
	    if (a.has_lo)
	      {
		widest_int q = wi::sdiv_trunc (a.lo, d);
		if (pos)
		  r.has_lo = true, r.lo = q;
		else
		  r.has_hi = true, r.hi = q;
	      }
	    if (a.has_hi)
	      {
		widest_int q = wi::sdiv_trunc (a.hi, d);
		if (pos)
		  r.has_hi = true, r.hi = q;
		else
		  r.has_lo = true, r.lo = q;
	      }
	    break;
	  }
	if (!(a.has_lo && a.has_hi && b.has_lo && b.has_hi))
	  return false;
	/* A divisor range containing zero: unknown.  */
	if (wi::les_p (b.lo, 0) && wi::les_p (0, b.hi))
	  return false;
	widest_int c[4] = { wi::sdiv_trunc (a.lo, b.lo), wi::sdiv_trunc (a.lo, b.hi),
			    wi::sdiv_trunc (a.hi, b.lo), wi::sdiv_trunc (a.hi, b.hi) };
	r.has_lo = r.has_hi = true;
	r.lo = r.hi = c[0];
	for (int i = 1; i < 4; ++i)
	  {
	    if (wi::lts_p (c[i], r.lo)) r.lo = c[i];
	    if (wi::gts_p (c[i], r.hi)) r.hi = c[i];
	  }
	break;
      }
    default:
      return false;
    }
  *out = r;
  return r.any ();
}

/* ---------------------------------------------------------------------- */
/* The facts.  */

struct range_fact { tree key; irng r; };
struct frange_fact { tree key; frange r; };

/* 'A - B - OFF' has one of the orderings MASK relative to zero.  */
struct rel_fact { tree a; tree b; widest_int off; int mask; };

/* Boolean expression EXPR is known to have value POLARITY.  */
struct atom_fact
{
  tree expr;
  bool polarity;
  /* The objects a call was given a route to whose postcondition
     established it: it ends when they change.  */
  std::vector<tree> deps;
};

/* The deps of the atoms being established.  */
static std::vector<tree> current_deps;

struct factset
{
  std::vector<range_fact> ranges;
  std::vector<frange_fact> franges;
  std::vector<rel_fact> rels;
  std::vector<atom_fact> atoms;
  std::vector<tree> oaddr;
  std::vector<tree> nonzero;
};

/* A pointer variable PTR known to point to the object TARGET points to
   (TARGET: an address '&x', or another pointer).  */
struct alias_fact { tree ptr; tree target; };

struct state
{
  bool reachable = true;
  factset f[2];
  std::vector<alias_fact> aliases;
  /* Locals whose address was given to a call or stored.  */
  std::vector<tree> escaped;
  /* Objects whose lifetime may have ended.  */
  std::vector<tree> ended;
  /* Variables holding pointers (or copies of them, or pointers loaded
     from them) and the variable whose pointees they share.  */
  std::vector<std::pair<tree, tree>> reach;
  /* Those whose pointees were handed out: any call may change them.  */
  std::vector<tree> reach_escaped;

  static state unreachable ()
  {
    state s;
    s.reachable = false;
    return s;
  }
};

static tree
resolve (tree t)
{
  if (!resolve_state || !t)
    return t;
  if (VAR_P (t) || TREE_CODE (t) == PARM_DECL)
    {
      for (const alias_fact &a : resolve_state->aliases)
	if (a.ptr == t)
	  return a.target;
      return t;
    }
  /* '*p' with p known to point to x: x.  */
  if (TREE_CODE (t) == INDIRECT_REF)
    {
      tree p = strip (TREE_OPERAND (t, 0));
      if (TREE_CODE (p) == POINTER_PLUS_EXPR && integer_zerop (TREE_OPERAND (p, 1)))
	p = strip (TREE_OPERAND (p, 0));
      tree r = resolve (p);
      if (r != p)
	{
	  r = strip (r);
	  if (TREE_CODE (r) == ADDR_EXPR)
	    return strip (TREE_OPERAND (r, 0));
	}
      return t;
    }
  /* A member slot holding a pointer.  */
  if ((TREE_CODE (t) == COMPONENT_REF || TREE_CODE (t) == ARRAY_REF)
      && TREE_TYPE (t) && POINTER_TYPE_P (TREE_TYPE (t)))
    {
      state *s = resolve_state;
      resolve_state = NULL;
      tree r = t;
      for (const alias_fact &a : s->aliases)
	if (!DECL_P (a.ptr) && key_eq (a.ptr, t))
	  {
	    r = a.target;
	    break;
	  }
      resolve_state = s;
      return r;
    }
  return t;
}

/* Index of the fact set a query in the conveyor flavor (CONVEYOR) or any
   uses.  */

static inline int
set_index (bool conveyor)
{
  return conveyor ? 0 : 1;
}

/* Debug: print S.  */

static void
dump_state (const char *what, const state &s)
{
  if (!dump_p)
    return;
  fprintf (stderr, "apf: state at %s%s\n", what,
	   s.reachable ? "" : " (unreachable)");
  for (int i = 0; i < 2; ++i)
    {
      const factset &f = s.f[i];
      for (const range_fact &x : f.ranges)
	{
	  fprintf (stderr, "  [%d] range ", i);
	  print_generic_expr (stderr, x.key);
	  fprintf (stderr, " (%s)", get_tree_code_name (TREE_CODE (x.key)));
	  if (x.r.has_lo)
	    fprintf (stderr, " lo %ld", (long) x.r.lo.to_shwi ());
	  if (x.r.has_hi)
	    fprintf (stderr, " hi %ld", (long) x.r.hi.to_shwi ());
	  fprintf (stderr, "\n");
	}
      for (const frange_fact &x : f.franges)
	{
	  fprintf (stderr, "  [%d] frange ", i);
	  print_generic_expr (stderr, x.key);
	  fprintf (stderr, " ");
	  x.r.dump (stderr);
	  fprintf (stderr, "\n");
	}
      for (const rel_fact &x : f.rels)
	{
	  fprintf (stderr, "  [%d] rel ", i);
	  print_generic_expr (stderr, x.a);
	  fprintf (stderr, " - ");
	  print_generic_expr (stderr, x.b);
	  fprintf (stderr, " - %ld in %d\n", (long) x.off.to_shwi (), x.mask);
	}
      for (const atom_fact &x : f.atoms)
	{
	  fprintf (stderr, "  [%d] atom %d ", i, x.polarity);
	  print_generic_expr (stderr, x.expr);
	  fprintf (stderr, "\n");
	}
      for (tree x : f.oaddr)
	{
	  fprintf (stderr, "  [%d] oaddr ", i);
	  print_generic_expr (stderr, x);
	  fprintf (stderr, "\n");
	}
      for (tree x : f.nonzero)
	{
	  fprintf (stderr, "  [%d] nonzero ", i);
	  print_generic_expr (stderr, x);
	  fprintf (stderr, "\n");
	}
    }
  for (const alias_fact &a : s.aliases)
    {
      fprintf (stderr, "  alias ");
      print_generic_expr (stderr, a.ptr);
      fprintf (stderr, " (%s) -> ", get_tree_code_name (TREE_CODE (a.ptr)));
      print_generic_expr (stderr, a.target);
      fprintf (stderr, "\n");
    }
}

/* ---- Meet ---- */

static bool
atom_eq (const atom_fact &a, const atom_fact &b)
{
  return a.polarity == b.polarity && key_eq (a.expr, b.expr);
}

static bool
rel_eq (const rel_fact &a, const rel_fact &b)
{
  return (a.mask == b.mask && a.off == b.off && key_eq (a.a, b.a)
	  && key_eq (a.b, b.b));
}

static void
meet_set (factset &a, const factset &b)
{
  std::vector<range_fact> ranges;
  for (const range_fact &x : a.ranges)
    for (const range_fact &y : b.ranges)
      if (key_eq (x.key, y.key))
	{
	  range_fact m = x;
	  m.r.hull (y.r);
	  if (m.r.any ())
	    ranges.push_back (m);
	  break;
	}
  a.ranges = ranges;

  std::vector<frange_fact> franges;
  for (const frange_fact &x : a.franges)
    for (const frange_fact &y : b.franges)
      if (key_eq (x.key, y.key))
	{
	  frange_fact m = x;
	  frange t = y.r;
	  if (m.r.undefined_p () || t.undefined_p ())
	    break;
	  if (t.type () == m.r.type () || range_cast (t, m.r.type ()))
	    m.r.union_ (t);
	  else
	    m.r.set_varying (m.r.type ());
	  if (!m.r.varying_p ())
	    franges.push_back (m);
	  break;
	}
  a.franges = franges;

  std::vector<rel_fact> rels;
  for (const rel_fact &x : a.rels)
    for (const rel_fact &y : b.rels)
      if (key_eq (x.a, y.a) && key_eq (x.b, y.b))
	{
	  /* Same offset: the union of the orderings; else keep only a
	     subsumed one.  */
	  if (x.off == y.off)
	    {
	      rel_fact m = x;
	      m.mask |= y.mask;
	      if (m.mask != ORD_ALL)
		rels.push_back (m);
	    }
	  break;
	}
  a.rels = rels;

  std::vector<atom_fact> atoms;
  for (const atom_fact &x : a.atoms)
    for (const atom_fact &y : b.atoms)
      if (atom_eq (x, y))
	{
	  atoms.push_back (x);
	  break;
	}
  a.atoms = atoms;

  std::vector<tree> oaddr;
  for (tree x : a.oaddr)
    for (tree y : b.oaddr)
      if (key_eq (x, y))
	{
	  oaddr.push_back (x);
	  break;
	}
  a.oaddr = oaddr;

  std::vector<tree> nonzero;
  for (tree x : a.nonzero)
    for (tree y : b.nonzero)
      if (key_eq (x, y))
	{
	  nonzero.push_back (x);
	  break;
	}
  a.nonzero = nonzero;
}

/* A := what holds in both A and B.  */

static void
meet (state &a, const state &b)
{
  if (!b.reachable)
    return;
  if (!a.reachable)
    {
      a = b;
      return;
    }
  state *saved = resolve_state;
  resolve_state = NULL;
  for (int i = 0; i < 2; ++i)
    meet_set (a.f[i], b.f[i]);
  std::vector<alias_fact> aliases;
  for (const alias_fact &x : a.aliases)
    for (const alias_fact &y : b.aliases)
      if (x.ptr == y.ptr && key_eq (x.target, y.target))
	{
	  aliases.push_back (x);
	  break;
	}
  a.aliases = aliases;
  for (tree e : b.escaped)
    if (std::find (a.escaped.begin (), a.escaped.end (), e) == a.escaped.end ())
      a.escaped.push_back (e);
  for (tree e : b.ended)
    if (std::find (a.ended.begin (), a.ended.end (), e) == a.ended.end ())
      a.ended.push_back (e);
  for (tree e : b.reach_escaped)
    if (std::find (a.reach_escaped.begin (), a.reach_escaped.end (), e)
	== a.reach_escaped.end ())
      a.reach_escaped.push_back (e);
  for (auto &r : b.reach)
    if (std::find (a.reach.begin (), a.reach.end (), r) == a.reach.end ())
      a.reach.push_back (r);
  resolve_state = saved;
}

static bool
same_set (const factset &a, const factset &b)
{
  if (a.ranges.size () != b.ranges.size ()
      || a.franges.size () != b.franges.size ()
      || a.rels.size () != b.rels.size ()
      || a.atoms.size () != b.atoms.size ()
      || a.oaddr.size () != b.oaddr.size ()
      || a.nonzero.size () != b.nonzero.size ())
    return false;
  for (const range_fact &x : a.ranges)
    {
      bool hit = false;
      for (const range_fact &y : b.ranges)
	if (key_eq (x.key, y.key) && x.r == y.r)
	  hit = true;
      if (!hit)
	return false;
    }
  for (const frange_fact &x : a.franges)
    {
      bool hit = false;
      for (const frange_fact &y : b.franges)
	if (key_eq (x.key, y.key) && x.r == y.r)
	  hit = true;
      if (!hit)
	return false;
    }
  for (const rel_fact &x : a.rels)
    {
      bool hit = false;
      for (const rel_fact &y : b.rels)
	if (rel_eq (x, y))
	  hit = true;
      if (!hit)
	return false;
    }
  for (const atom_fact &x : a.atoms)
    {
      bool hit = false;
      for (const atom_fact &y : b.atoms)
	if (atom_eq (x, y))
	  hit = true;
      if (!hit)
	return false;
    }
  return true;
}

static bool
same_state (const state &a, const state &b)
{
  if (a.reachable != b.reachable)
    return false;
  if (!a.reachable)
    return true;
  state *saved = resolve_state;
  resolve_state = NULL;
  bool same = (same_set (a.f[0], b.f[0]) && same_set (a.f[1], b.f[1])
	       && a.aliases.size () == b.aliases.size ()
	       && a.escaped.size () == b.escaped.size ()
	       && a.reach.size () == b.reach.size ()
	       && a.reach_escaped.size () == b.reach_escaped.size ()
	       && a.ended.size () == b.ended.size ());
  resolve_state = saved;
  return same;
}

/* ---- Killing ---- */

/* Whether fact key K must go when what PRED says changed.  */

template<typename P>
static void
kill_if (state &s, P pred)
{
  for (int i = 0; i < 2; ++i)
    {
      factset &f = s.f[i];
      f.ranges.erase (std::remove_if (f.ranges.begin (), f.ranges.end (),
				      [&] (const range_fact &x)
				      { return pred (x.key); }),
		      f.ranges.end ());
      f.franges.erase (std::remove_if (f.franges.begin (), f.franges.end (),
				       [&] (const frange_fact &x)
				       { return pred (x.key); }),
		       f.franges.end ());
      f.rels.erase (std::remove_if (f.rels.begin (), f.rels.end (),
				    [&] (const rel_fact &x)
				    { return pred (x.a) || pred (x.b); }),
		    f.rels.end ());
      f.atoms.erase (std::remove_if (f.atoms.begin (), f.atoms.end (),
				     [&] (const atom_fact &x)
				     { return pred (x.expr); }),
		     f.atoms.end ());
      f.oaddr.erase (std::remove_if (f.oaddr.begin (), f.oaddr.end (), pred),
		     f.oaddr.end ());
      f.nonzero.erase (std::remove_if (f.nonzero.begin (), f.nonzero.end (),
				       pred),
		       f.nonzero.end ());
    }
}

static tree root_object (tree k, bool *deref);
static bool access_disjoint_p (tree a, tree b);

/* Remove from S the atoms with a dep PRED holds of.  */

template<typename P>
static void
kill_deps (state &s, P pred)
{
  for (int i = 0; i < 2; ++i)
    {
      std::vector<atom_fact> &v = s.f[i].atoms;
      v.erase (std::remove_if (v.begin (), v.end (),
			       [&] (const atom_fact &x)
			       {
				 for (tree d : x.deps)
				   if (pred (d))
				     return true;
				 return false;
			       }), v.end ());
    }
}

/* What S knows of variable D (and what mentions it) ends.  */

static void
kill_decl (state &s, tree d)
{
  state *saved = resolve_state;
  resolve_state = NULL;
  kill_if (s, [&] (tree k) { return mentions_decl_p (k, d); });
  kill_deps (s, [&] (tree dep)
	     {
	       bool dd;
	       return root_object (dep, &dd) == d && !dd;
	     });
  if (TREE_TYPE (d) && POINTER_TYPE_P (TREE_TYPE (d)))
    s.ended.erase (std::remove (s.ended.begin (), s.ended.end (), d),
		   s.ended.end ());
  s.aliases.erase (std::remove_if (s.aliases.begin (), s.aliases.end (),
				   [&] (const alias_fact &a)
				   { return a.ptr == d
					    || mentions_decl_p (a.target, d); }),
		   s.aliases.end ());
  resolve_state = saved;
}

/* ---------------------------------------------------------------------- */
/* Objects.  */

/* The object a pointer or reference value P points to, as a key: 'x'
   for '&x', '*p' otherwise.  NULL_TREE if unknown.  */

static tree
pointee (tree p)
{
  p = strip (p);
  if (!p)
    return NULL_TREE;
  p = resolve (p);
  p = strip (p);
  if (TREE_CODE (p) == ADDR_EXPR)
    return strip (TREE_OPERAND (p, 0));
  tree type = TREE_TYPE (p);
  if (type && POINTER_TYPE_P (type))
    return build1 (INDIRECT_REF, TREE_TYPE (type), p);
  return p;
}

/* The root object of key K: the variable it is a part of, or the pointer
   whose pointee it is part of (as '*p'), or NULL_TREE (unknown).
   *DEREF: the root is reached through a pointer.  */

static tree
root_object (tree k, bool *deref)
{
  *deref = false;
  for (int depth = 0; depth < 64 && k; ++depth)
    {
      k = strip (k);
      switch (TREE_CODE (k))
	{
	case VAR_DECL:
	case PARM_DECL:
	case RESULT_DECL:
	  if (TYPE_REF_P (TREE_TYPE (k)))
	    {
	      *deref = true;
	      return k;
	    }
	  return k;
	case COMPONENT_REF:
	case ARRAY_REF:
	case BIT_FIELD_REF:
	case REALPART_EXPR:
	case IMAGPART_EXPR:
	  k = TREE_OPERAND (k, 0);
	  continue;
	case INDIRECT_REF:
	case MEM_REF:
	  {
	    tree p = strip (TREE_OPERAND (k, 0));
	    tree r = resolve (p);
	    if (r != p)
	      {
		r = strip (r);
		if (TREE_CODE (r) == ADDR_EXPR)
		  {
		    k = TREE_OPERAND (r, 0);
		    continue;
		  }
		p = r;
	      }
	    *deref = true;
	    if (VAR_P (p) || TREE_CODE (p) == PARM_DECL)
	      return p;
	    return NULL_TREE;
	  }
	case CALL_EXPR:
	  {
	    /* An accessor: the object of its first argument.  */
	    if (call_expr_nargs (k) == 0)
	      return NULL_TREE;
	    tree o = pointee (CALL_EXPR_ARG (k, 0));
	    if (!o)
	      return NULL_TREE;
	    k = o;
	    continue;
	  }
	default:
	  return NULL_TREE;
	}
    }
  return NULL_TREE;
}

/* Whether D is a local object of the function (not static, not a
   reference).  */

static bool
local_object_p (tree d)
{
  return ((VAR_P (d) && !TREE_STATIC (d) && !DECL_EXTERNAL (d)
	   && !TYPE_REF_P (TREE_TYPE (d)))
	  || (TREE_CODE (d) == PARM_DECL && !TYPE_REF_P (TREE_TYPE (d))
	      && !DECL_BY_REFERENCE (d))
	  || TREE_CODE (d) == RESULT_DECL);
}

static bool
escaped_p (const state &s, tree d)
{
  return std::find (s.escaped.begin (), s.escaped.end (), d) != s.escaped.end ();
}


/* Whether pointers P1 and P2 (variables) may point to the same object:
   unless restrict, of compatible pointee types (ignoring qualifiers; any
   pointee through 'void *' or a character type).  */

static bool
pointers_may_alias (tree p1, tree p2)
{
  if (p1 == p2)
    return true;
  tree t1 = TREE_TYPE (p1), t2 = TREE_TYPE (p2);
  if (!t1 || !t2 || !POINTER_TYPE_P (t1) || !POINTER_TYPE_P (t2))
    return true;
  if (TYPE_RESTRICT (t1) || TYPE_RESTRICT (t2))
    return false;
  tree a = TYPE_MAIN_VARIANT (TREE_TYPE (t1));
  tree b = TYPE_MAIN_VARIANT (TREE_TYPE (t2));
  if (VOID_TYPE_P (a) || VOID_TYPE_P (b) || a == b)
    return true;
  if (char_type_p (a) || char_type_p (b))
    return true;
  if (CLASS_TYPE_P (a) && CLASS_TYPE_P (b)
      && (DERIVED_FROM_P (a, b) || DERIVED_FROM_P (b, a)))
    return true;
  return false;
}

/* Whether objects with roots R1 and R2 (D1, D2: reached through that
   pointer) may overlap.  */

static bool
roots_may_overlap (const state &s, tree r1, bool d1, tree r2, bool d2)
{
  if (!r1 || !r2)
    return true;
  if (r1 == r2)
    return d1 == d2;
  if (d1 && d2)
    return pointers_may_alias (r1, r2);
  if (!d1 && !d2)
    return false;
  /* A named object and an object through a pointer: the pointer may
     point to it unless it is a local whose address never escaped, or the
     pointer is a parameter (which can't point to the function's own
     locals).  */
  tree obj = d1 ? r2 : r1;
  tree ptr = d1 ? r1 : r2;
  if (!local_object_p (obj))
    return true;
  if (TREE_CODE (ptr) == PARM_DECL)
    return false;
  return escaped_p (s, obj);
}

/* Whether TYPE is a class holding pointers (an iterator).  */

static bool
pointer_holder_p (tree type, int depth = 0)
{
  if (!type || depth > 4 || !CLASS_TYPE_P (type) || !COMPLETE_TYPE_P (type))
    return false;
  for (tree f = TYPE_FIELDS (type); f; f = DECL_CHAIN (f))
    if (TREE_CODE (f) == FIELD_DECL)
      {
	tree t = TREE_TYPE (f);
	if (POINTER_TYPE_P (t) || pointer_holder_p (t, depth + 1))
	  return true;
      }
  return false;
}

/* Whether atom E is about values holding pointers: a call with an argument
   of such a class type, by value.  */

static bool
reach_atom_p (tree e)
{
  e = strip (e);
  if (!e || TREE_CODE (e) != CALL_EXPR)
    return false;
  tree fn = call_fn (e);
  if (!fn)
    return false;
  for (tree p = DECL_ARGUMENTS (fn); p; p = DECL_CHAIN (p))
    if (pointer_holder_p (TREE_TYPE (p)))
      return true;
  for (tree a = TYPE_ARG_TYPES (TREE_TYPE (fn)); a; a = TREE_CHAIN (a))
    if (pointer_holder_p (TREE_VALUE (a)))
      return true;
  return false;
}

/* The variable whose pointees V (a variable) shares.  */

static tree
reach_rep (const state &s, tree v)
{
  for (auto &r : s.reach)
    if (r.first == v)
      return r.second;
  return v;
}

/* The reach representative of value E: a pointer-holding variable, a
   copy of one, or a pointer loaded from one's member.  */

static tree
reach_of (const state &s, tree e)
{
  e = strip (e);
  if (!e)
    return NULL_TREE;
  if (VAR_P (e) || TREE_CODE (e) == PARM_DECL)
    {
      tree r = reach_rep (s, e);
      if (r != e || pointer_holder_p (TREE_TYPE (e)))
	return r;
      return NULL_TREE;
    }
  if (TREE_CODE (e) == COMPONENT_REF && POINTER_TYPE_P (TREE_TYPE (e)))
    {
      bool d;
      tree root = root_object (e, &d);
      if (root && !d && pointer_holder_p (TREE_TYPE (root)))
	return reach_rep (s, root);
    }
  return NULL_TREE;
}

/* Whether atom E is a predicate of an object (by address) whose reach
   representative is escaped in S.  */

static bool
reach_escaped_atom_p (const state &s, tree e)
{
  e = strip (e);
  if (!e || TREE_CODE (e) != CALL_EXPR || s.reach_escaped.empty ())
    return false;
  for (int i = 0; i < call_expr_nargs (e); ++i)
    {
      tree a = strip (CALL_EXPR_ARG (e, i));
      if (!a || TREE_CODE (a) != ADDR_EXPR)
	continue;
      bool d;
      tree root = root_object (TREE_OPERAND (a, 0), &d);
      if (!root || d)
	continue;
      tree rep = reach_rep (s, root);
      if (std::find (s.reach_escaped.begin (), s.reach_escaped.end (), rep)
	  != s.reach_escaped.end ())
	return true;
    }
  return false;
}

static bool constant_value (tree t, widest_int *out);

/* The access path of reference T, root first: the members and elements
   selected.  */

static void
access_path (tree t, std::vector<tree> &out)
{
  t = strip (t);
  if (t && (TREE_CODE (t) == COMPONENT_REF || TREE_CODE (t) == ARRAY_REF))
    {
      access_path (TREE_OPERAND (t, 0), out);
      out.push_back (t);
    }
}

/* Whether references A and B (of one root object) select disjoint parts:
   different members, or elements at different constant indices.  */

static bool
access_disjoint_p (tree a, tree b)
{
  std::vector<tree> pa, pb;
  access_path (a, pa);
  access_path (b, pb);
  for (size_t i = 0; i < pa.size () && i < pb.size (); ++i)
    {
      tree x = pa[i], y = pb[i];
      if (TREE_CODE (x) != TREE_CODE (y))
	return false;
      if (TREE_CODE (x) == COMPONENT_REF)
	{
	  tree fx = TREE_OPERAND (x, 1), fy = TREE_OPERAND (y, 1);
	  if (fx != fy)
	    return TREE_CODE (DECL_CONTEXT (fx)) != UNION_TYPE;
	}
      else
	{
	  widest_int ix, iy;
	  if (!constant_value (TREE_OPERAND (x, 1), &ix)
	      || !constant_value (TREE_OPERAND (y, 1), &iy))
	    return false;
	  if (ix != iy)
	    return true;
	}
    }
  return false;
}

/* ---------------------------------------------------------------------- */
/* Queries.  */

static bool constant_value (tree t, widest_int *out);

/* The integer range of expression E in fact set F (of S).  */

static irng range_of (state &s, int fi, tree e, int depth = 0);

static irng
range_of (state &s, int fi, tree e, int depth)
{
  irng r;
  e = strip (e);
  if (!e || depth > 8)
    return r;
  tree type = TREE_TYPE (e);
  if (!type || !(INTEGRAL_TYPE_P (type) || POINTER_TYPE_P (type) || CLASS_TYPE_P (type)))
    return r;
  widest_int c;
  if (constant_value (e, &c))
    return irng::exact (c);
  for (const range_fact &x : s.f[fi].ranges)
    if (key_eq (x.key, e))
      r.intersect (x.r);
  switch (TREE_CODE (e))
    {
    case PLUS_EXPR:
    case MINUS_EXPR:
    case MULT_EXPR:
    case TRUNC_DIV_EXPR:
      {
	irng a = range_of (s, fi, TREE_OPERAND (e, 0), depth + 1);
	irng b = range_of (s, fi, TREE_OPERAND (e, 1), depth + 1);
	/* Unsigned operands are nonnegative.  */
	if (TYPE_UNSIGNED (TREE_TYPE (TREE_OPERAND (e, 0))) && !a.has_lo)
	  a.has_lo = true, a.lo = 0;
	if (TYPE_UNSIGNED (TREE_TYPE (TREE_OPERAND (e, 1))) && !b.has_lo)
	  b.has_lo = true, b.lo = 0;
	irng v;
	if (irng_binop (TREE_CODE (e), a, b, &v))
	  {
	    /* Out of the type's values: wraps (unknown), or overflows
	       (undefined: the values that fit).  */
	    if (fits_type_p (v, type))
	      r.intersect (v);
	    else if (INTEGRAL_TYPE_P (type) && !TYPE_OVERFLOW_WRAPS (type)
		     && !(v.has_lo && v.has_hi))
	      {
		v.intersect (type_range (type));
		r.intersect (v);
	      }
	  }
	break;
      }
    case NEGATE_EXPR:
      {
	irng a = range_of (s, fi, TREE_OPERAND (e, 0), depth + 1);
	irng v;
	v.has_lo = a.has_hi;
	v.has_hi = a.has_lo;
	if (v.has_lo) v.lo = -a.hi;
	if (v.has_hi) v.hi = -a.lo;
	if (v.any () && fits_type_p (v, type))
	  r.intersect (v);
	break;
      }
    case NOP_EXPR:
    case CONVERT_EXPR:
      {
	tree op = TREE_OPERAND (e, 0);
	irng a = range_of (s, fi, op, depth + 1);
	if (a.any () && INTEGRAL_TYPE_P (TREE_TYPE (op)))
	  {
	    a.intersect (type_range (TREE_TYPE (op)));
	    if (fits_type_p (a, type))
	      r.intersect (a);
	  }
	break;
      }
    default:
      break;
    }
  /* Through a relation 'E = B + OFF' with B's range known.  */
  if (depth < 4)
    for (const rel_fact &x : s.f[fi].rels)
      {
	tree other = NULL_TREE;
	widest_int off = x.off;
	int mask = x.mask;
	if (key_eq (x.a, e))
	  other = x.b;
	else if (key_eq (x.b, e))
	  {
	    other = x.a;
	    off = -off;
	    mask = mirror_mask (mask);
	  }
	if (!other)
	  continue;
	irng b = range_of (s, fi, other, depth + 2);
	if (TREE_TYPE (strip (other)))
	  b.intersect (type_range (TREE_TYPE (strip (other))));
	if (!b.any ())
	  continue;
	/* E - OTHER - OFF in MASK.  */
	irng d;
	if (mask == ORD_EQ)
	  d = irng::exact (0);
	else if (mask == ORD_LT)
	  d.has_hi = true, d.hi = -1;
	else if (mask == (ORD_LT | ORD_EQ))
	  d.has_hi = true, d.hi = 0;
	else if (mask == ORD_GT)
	  d.has_lo = true, d.lo = 1;
	else if (mask == (ORD_GT | ORD_EQ))
	  d.has_lo = true, d.lo = 0;
	else
	  continue;
	irng v;
	if (irng_binop (PLUS_EXPR, b, d, &v))
	  {
	    irng o = irng::exact (off);
	    irng w;
	    if (irng_binop (PLUS_EXPR, v, o, &w))
	      r.intersect (w);
	  }
      }
  return r;
}

/* E without integer conversions that S's range of their operand shows
   keep its value ('(size_t) idx' with idx >= 0).  */

static tree
strip_with_range (state &s, int fi, tree e)
{
  e = strip (e);
  for (int depth = 0; e && depth < 4; ++depth)
    {
      if (!(CONVERT_EXPR_P (e) || TREE_CODE (e) == NON_LVALUE_EXPR))
	break;
      tree op = TREE_OPERAND (e, 0);
      if (!INTEGRAL_TYPE_P (TREE_TYPE (e)) || !TREE_TYPE (op)
	  || !INTEGRAL_TYPE_P (TREE_TYPE (op)))
	break;
      irng r = range_of (s, fi, op);
      r.intersect (type_range (TREE_TYPE (op)));
      if (!fits_type_p (r, TREE_TYPE (e)))
	break;
      e = strip (op);
    }
  return e;
}

/* Whether T is an integer constant (possibly a constant variable).  */

static bool
constant_value (tree t, widest_int *out)
{
  t = strip (t);
  if (!t)
    return false;
  if (TREE_CODE (t) == INTEGER_CST)
    {
      *out = wi::to_widest (t);
      return true;
    }
  if (VAR_P (t) && (TREE_READONLY (t) || DECL_DECLARED_CONSTEXPR_P (t))
      && !TREE_THIS_VOLATILE (t) && INTEGRAL_TYPE_P (TREE_TYPE (t)))
    {
      tree init = decl_constant_value (t);
      if (init && init != t)
	{
	  init = strip (init);
	  if (TREE_CODE (init) == INTEGER_CST)
	    {
	      *out = wi::to_widest (init);
	      return true;
	    }
	}
    }
  return false;
}

/* Whether real constant T.  */

static tree
real_constant (tree t)
{
  t = strip (t);
  if (t && TREE_CODE (t) == REAL_CST)
    return t;
  if (t && VAR_P (t) && (TREE_READONLY (t) || DECL_DECLARED_CONSTEXPR_P (t))
      && SCALAR_FLOAT_TYPE_P (TREE_TYPE (t)))
    {
      tree init = decl_constant_value (t);
      if (init && init != t)
	{
	  init = strip (init);
	  if (TREE_CODE (init) == REAL_CST)
	    return init;
	}
    }
  return NULL_TREE;
}

/* The floating-point range of E.  */

static bool
frange_of (state &s, int fi, tree e, frange &fr, int depth = 0)
{
  e = strip (e);
  if (!e || depth > 8 || !TREE_TYPE (e))
    return false;
  tree type = TREE_TYPE (e);
  if (tree c = real_constant (e))
    {
      fr = frange (c, c);
      if (!real_isnan (TREE_REAL_CST_PTR (c)))
	fr.clear_nan ();
      return true;
    }
  if (TREE_CODE (e) == INTEGER_CST && SCALAR_FLOAT_TYPE_P (type))
    return false;
  if (!SCALAR_FLOAT_TYPE_P (type))
    return false;
  fr.set_varying (type);
  for (const frange_fact &x : s.f[fi].franges)
    if (key_eq (x.key, e))
      {
	frange t = x.r;
	if (!t.undefined_p () && (t.type () == type || range_cast (t, type)))
	  fr.intersect (t);
      }
  tree_code code = TREE_CODE (e);
  frange a, b, r;
  if (code == PLUS_EXPR || code == MINUS_EXPR || code == MULT_EXPR
      || code == RDIV_EXPR)
    {
      if (frange_of (s, fi, TREE_OPERAND (e, 0), a, depth + 1)
	  && frange_of (s, fi, TREE_OPERAND (e, 1), b, depth + 1)
	  && !a.undefined_p () && !b.undefined_p ()
	  && (a.type () == type || range_cast (a, type))
	  && (b.type () == type || range_cast (b, type)))
	{
	  range_op_handler op (code);
	  if (op && op.fold_range (r, type, a, b) && !r.undefined_p ())
	    fr.intersect (r);
	}
    }
  else if (code == NEGATE_EXPR)
    {
      if (frange_of (s, fi, TREE_OPERAND (e, 0), a, depth + 1)
	  && !a.undefined_p ()
	  && (a.type () == type || range_cast (a, type)))
	{
	  frange unused (type);
	  range_op_handler op (code);
	  if (op && op.fold_range (r, type, a, unused) && !r.undefined_p ())
	    fr.intersect (r);
	}
    }
  else if (code == FLOAT_EXPR)
    {
      irng ir = range_of (s, fi, TREE_OPERAND (e, 0), depth + 1);
      ir.intersect (type_range (TREE_TYPE (TREE_OPERAND (e, 0))));
      if (ir.has_lo && ir.has_hi)
	{
	  REAL_VALUE_TYPE lo, hi;
	  real_from_integer (&lo, TYPE_MODE (type), ir.lo, SIGNED);
	  real_from_integer (&hi, TYPE_MODE (type), ir.hi, SIGNED);
	  frange t (type, lo, hi);
	  t.clear_nan ();
	  fr.intersect (t);
	}
    }
  return !fr.varying_p () && !fr.undefined_p ();
}

/* The values of TYPE satisfying 'V CODE CST' (NaN excluded).  */

static bool
frange_for_comparison (tree type, tree_code code, tree cst, frange *out)
{
  REAL_VALUE_TYPE c = TREE_REAL_CST (cst);
  if (real_isnan (&c))
    return false;
  frange r;
  switch (code)
    {
    case LT_EXPR: case LE_EXPR:
      r = frange (type, dconstninf, c);
      break;
    case GT_EXPR: case GE_EXPR:
      r = frange (type, c, dconstinf);
      break;
    case EQ_EXPR:
      r = frange (type, c, c);
      break;
    default:
      return false;
    }
  r.clear_nan ();
  if (code == LT_EXPR || code == GT_EXPR)
    {
      /* Open bound: the next representable value.  */
      REAL_VALUE_TYPE n = c;
      if (code == LT_EXPR)
	real_nextafter (&n, REAL_MODE_FORMAT (TYPE_MODE (type)), &c, &dconstninf);
      else
	real_nextafter (&n, REAL_MODE_FORMAT (TYPE_MODE (type)), &c, &dconstinf);
      if (code == LT_EXPR)
	r = frange (type, dconstninf, n);
      else
	r = frange (type, n, dconstinf);
      r.clear_nan ();
    }
  *out = r;
  return true;
}

/* Whether every value of FR satisfies 'v CODE CST' (TRUE), none does
   (FALSE).  A possible NaN makes every ordered comparison possibly
   false.  */

static oa_proof_result
frange_verdict (const frange &fr, tree_code code, tree cst)
{
  if (fr.undefined_p ())
    return OA_UNKNOWN;
  REAL_VALUE_TYPE c = TREE_REAL_CST (cst);
  if (fr.known_isnan ())
    return code == NE_EXPR ? OA_PROVEN_TRUE : OA_PROVEN_FALSE;
  bool maybe_nan = fr.maybe_isnan ();
  REAL_VALUE_TYPE lo = fr.lower_bound (), hi = fr.upper_bound ();
  bool all = false, none = false;
  switch (code)
    {
    case LT_EXPR:
      all = real_less (&hi, &c);
      none = !real_less (&lo, &c);
      break;
    case LE_EXPR:
      all = real_less (&hi, &c) || real_equal (&hi, &c);
      none = real_less (&c, &lo);
      break;
    case GT_EXPR:
      all = real_less (&c, &lo);
      none = !real_less (&c, &hi);
      break;
    case GE_EXPR:
      all = real_less (&c, &lo) || real_equal (&lo, &c);
      none = real_less (&hi, &c);
      break;
    case EQ_EXPR:
      all = real_equal (&lo, &c) && real_equal (&hi, &c);
      none = real_less (&c, &lo) || real_less (&hi, &c);
      break;
    case NE_EXPR:
      all = real_less (&c, &lo) || real_less (&hi, &c);
      none = real_equal (&lo, &c) && real_equal (&hi, &c) && !maybe_nan;
      if (maybe_nan && all)
	return OA_PROVEN_TRUE;
      break;
    default:
      return OA_UNKNOWN;
    }
  if (all && !maybe_nan)
    return OA_PROVEN_TRUE;
  if (none)
    return OA_PROVEN_FALSE;
  return OA_UNKNOWN;
}

/* 'A - B' compared with OFF: the verdict of 'A - B - OFF' having an
   ordering in MASK, from S's relations and ranges.  */

static oa_proof_result
rel_verdict_1 (state &s, int fi, tree a, tree b, const widest_int &off,
	       int mask, int depth);

/* The orderings 'X - D' has relative to zero for X with orderings HAVE
   relative to zero: MASK-relative verdict.  */

static oa_proof_result
shifted_mask_verdict (int have, const widest_int &d, int mask)
{
  /* X in HAVE (relative to 0); the question is X - D in MASK, i.e. X
     relative to D.  */
  irng r;
  /* HAVE as an interval (only contiguous masks).  */
  switch (have)
    {
    case ORD_LT: r.has_hi = true; r.hi = -1; break;
    case ORD_LT | ORD_EQ: r.has_hi = true; r.hi = 0; break;
    case ORD_EQ: r = irng::exact (0); break;
    case ORD_GT | ORD_EQ: r.has_lo = true; r.lo = 0; break;
    case ORD_GT: r.has_lo = true; r.lo = 1; break;
    case ORD_LT | ORD_GT:
      {
	/* X != 0: X - D in MASK for D == 0.  */
	if (d == 0)
	  {
	    if ((have & ~mask) == 0)
	      return OA_PROVEN_TRUE;
	    if ((have & mask) == 0)
	      return OA_PROVEN_FALSE;
	  }
	return OA_UNKNOWN;
      }
    default:
      return OA_UNKNOWN;
    }
  return range_mask_verdict (r, mask, d);
}

/* The interval of 'A - OFF' with A's orderings MASK relative to zero
   (rel_fact's meaning), as bounds of the difference.  */

static irng
mask_interval (int mask, const widest_int &off)
{
  irng r;
  switch (mask)
    {
    case ORD_LT: r.has_hi = true; r.hi = off - 1; break;
    case ORD_LT | ORD_EQ: r.has_hi = true; r.hi = off; break;
    case ORD_EQ: r = irng::exact (off); break;
    case ORD_GT | ORD_EQ: r.has_lo = true; r.lo = off; break;
    case ORD_GT: r.has_lo = true; r.lo = off + 1; break;
    default: break;
    }
  return r;
}

static irng
negate (const irng &x)
{
  irng r;
  r.has_lo = x.has_hi;
  r.has_hi = x.has_lo;
  if (r.has_lo) r.lo = -x.hi;
  if (r.has_hi) r.hi = -x.lo;
  return r;
}

/* The bounds of 'A - B' S knows: from relations (chained through other
   keys) and the ranges.  */

static irng
diff_bounds (state &s, int fi, tree a, tree b, int depth, tree from = NULL_TREE)
{
  if (key_eq (a, b))
    return irng::exact (0);
  irng r;
  irng ra = range_of (s, fi, a), rb = range_of (s, fi, b);
  irng d;
  if (ra.any () && rb.any () && irng_binop (MINUS_EXPR, ra, rb, &d))
    r.intersect (d);
  for (const rel_fact &x : s.f[fi].rels)
    {
      if (key_eq (x.a, a) && key_eq (x.b, b))
	r.intersect (mask_interval (x.mask, x.off));
      else if (key_eq (x.a, b) && key_eq (x.b, a))
	r.intersect (negate (mask_interval (x.mask, x.off)));
    }
  if (depth < 2)
    for (const rel_fact &x : s.f[fi].rels)
      {
	tree c = NULL_TREE;
	irng ac;
	if (key_eq (x.a, a) && !key_eq (x.b, b))
	  c = x.b, ac = mask_interval (x.mask, x.off);
	else if (key_eq (x.b, a) && !key_eq (x.a, b))
	  c = x.a, ac = negate (mask_interval (x.mask, x.off));
	if (!c || !ac.any () || (from && key_eq (c, from)))
	  continue;
	irng cb = diff_bounds (s, fi, c, b, depth + 1, a);
	irng sum;
	if (cb.any () && irng_binop (PLUS_EXPR, ac, cb, &sum))
	  r.intersect (sum);
      }
  return r;
}

static oa_proof_result
rel_verdict_1 (state &s, int fi, tree a, tree b, const widest_int &off,
	       int mask, int)
{
  if (key_eq (a, b))
    return range_mask_verdict (irng::exact (0), mask, off);
  /* 'A != B + K' facts.  */
  for (const rel_fact &x : s.f[fi].rels)
    if (x.mask == (ORD_LT | ORD_GT))
      {
	if (key_eq (x.a, a) && key_eq (x.b, b) && x.off == off)
	  {
	    if ((mask & ~(ORD_LT | ORD_GT)) == 0 && mask == (ORD_LT | ORD_GT))
	      return OA_PROVEN_TRUE;
	    if (mask == ORD_EQ)
	      return OA_PROVEN_FALSE;
	  }
	if (key_eq (x.a, b) && key_eq (x.b, a) && x.off == -off)
	  {
	    if (mask == (ORD_LT | ORD_GT))
	      return OA_PROVEN_TRUE;
	    if (mask == ORD_EQ)
	      return OA_PROVEN_FALSE;
	  }
      }
  irng d = diff_bounds (s, fi, a, b, 0);
  if (!d.any ())
    return OA_UNKNOWN;
  return range_mask_verdict (d, mask, off);
}

/* Decompose integer expression E into 'BASE + K', K a constant.  */

static tree
split_offset (tree e, widest_int *k)
{
  *k = 0;
  for (int depth = 0; depth < 8; ++depth)
    {
      e = strip (e);
      widest_int c;
      if ((TREE_CODE (e) == PLUS_EXPR || TREE_CODE (e) == MINUS_EXPR)
	  && INTEGRAL_TYPE_P (TREE_TYPE (e))
	  && constant_value (TREE_OPERAND (e, 1), &c))
	{
	  *k += TREE_CODE (e) == PLUS_EXPR ? c : -c;
	  e = TREE_OPERAND (e, 0);
	  continue;
	}
      break;
    }
  return e;
}

/* The verdict of 'A CODE B + OFF' (integers).  */

static oa_proof_result
rel_verdict (state &s, int fi, tree a, tree_code code, tree b,
	     const widest_int &off0 = 0)
{
  int mask = code_mask (code);
  if (!mask)
    return OA_UNKNOWN;
  widest_int ka, kb;
  state *saved0 = resolve_state;
  resolve_state = &s;
  a = split_offset (strip_with_range (s, fi, a), &ka);
  b = split_offset (strip_with_range (s, fi, b), &kb);
  a = strip_with_range (s, fi, a);
  b = strip_with_range (s, fi, b);
  resolve_state = saved0;
  /* A + KA CODE B + KB + OFF: A - B - (KB + OFF - KA).  */
  widest_int off = kb + off0 - ka;
  state *saved = resolve_state;
  resolve_state = &s;
  oa_proof_result v = rel_verdict_1 (s, fi, a, b, off, mask, 0);
  resolve_state = saved;
  return v;
}

/* The verdict of 'E CODE C', C an integer constant.  */

static oa_proof_result
cmp_verdict (state &s, int fi, tree e, tree_code code, const widest_int &c)
{
  state *saved = resolve_state;
  resolve_state = &s;
  irng r = range_of (s, fi, e);
  if (TREE_TYPE (strip (e)))
    r.intersect (type_range (TREE_TYPE (strip (e))));
  oa_proof_result v = OA_UNKNOWN;
  if (code == NE_EXPR && c == 0)
    {
      tree se = strip (e);
      for (tree k : s.f[fi].nonzero)
	if (key_eq (k, se))
	  v = OA_PROVEN_TRUE;
    }
  if (v == OA_UNKNOWN && r.any ())
    v = range_mask_verdict (r, code_mask (code), c);
  resolve_state = saved;
  return v;
}

/* The atom fact about boolean expression E: OA_PROVEN_TRUE if known
   true, OA_PROVEN_FALSE if known false.  */

static oa_proof_result
atom_verdict (state &s, int fi, tree e)
{
  state *saved = resolve_state;
  resolve_state = &s;
  oa_proof_result v = OA_UNKNOWN;
  for (const atom_fact &x : s.f[fi].atoms)
    if (key_eq (x.expr, e))
      {
	v = x.polarity ? OA_PROVEN_TRUE : OA_PROVEN_FALSE;
	break;
      }
  resolve_state = saved;
  return v;
}

/* ---------------------------------------------------------------------- */
/* Establishing facts.  */

static void add_range (state &s, bool conveyor, tree key, const irng &r);
static void assume (state &s, tree cond, bool polarity, bool conveyor,
		    int depth = 0);

static void
add_range_1 (factset &f, tree key, const irng &r)
{
  for (range_fact &x : f.ranges)
    if (key_eq (x.key, key))
      {
	x.r.intersect (r);
	return;
      }
  f.ranges.push_back ({ key, r });
}

static void
add_range (state &s, bool conveyor, tree key, const irng &r)
{
  if (!r.any () || !key)
    return;
  key = strip (key);
  for (int i = conveyor ? 0 : 1; i < 2; ++i)
    add_range_1 (s.f[i], key, r);
}

static void
add_frange (state &s, bool conveyor, tree key, const frange &r)
{
  key = strip (key);
  for (int i = conveyor ? 0 : 1; i < 2; ++i)
    {
      bool hit = false;
      for (frange_fact &x : s.f[i].franges)
	if (key_eq (x.key, key))
	  {
	    frange t = r;
	    if (!t.undefined_p () && !x.r.undefined_p ()
		&& (t.type () == x.r.type () || range_cast (t, x.r.type ())))
	      x.r.intersect (t);
	    hit = true;
	    break;
	  }
      if (!hit && !r.undefined_p ())
	s.f[i].franges.push_back ({ key, r });
      std::vector<frange_fact> &v = s.f[i].franges;
      v.erase (std::remove_if (v.begin (), v.end (),
			       [] (const frange_fact &x)
			       { return x.r.undefined_p (); }), v.end ());
    }
}

static void
add_rel (state &s, bool conveyor, tree a, tree b, const widest_int &off,
	 int mask)
{
  if (!mask || mask == ORD_ALL)
    return;
  a = strip (a);
  b = strip (b);
  for (int i = conveyor ? 0 : 1; i < 2; ++i)
    {
      bool hit = false;
      for (rel_fact &x : s.f[i].rels)
	if (key_eq (x.a, a) && key_eq (x.b, b) && x.off == off)
	  {
	    x.mask &= mask;
	    hit = true;
	    break;
	  }
      if (!hit)
	s.f[i].rels.push_back ({ a, b, off, mask });
    }
}

static void
add_atom (state &s, bool conveyor, tree e, bool polarity)
{
  e = strip (e);
  for (int i = conveyor ? 0 : 1; i < 2; ++i)
    {
      factset &f = s.f[i];
      f.atoms.erase (std::remove_if (f.atoms.begin (), f.atoms.end (),
				     [&] (const atom_fact &x)
				     { return key_eq (x.expr, e); }),
		     f.atoms.end ());
      f.atoms.push_back ({ e, polarity, current_deps });
    }
}

static void
add_oaddr (state &s, bool conveyor, tree p)
{
  p = strip (p);
  for (int i = conveyor ? 0 : 1; i < 2; ++i)
    s.f[i].oaddr.push_back (p);
}

static void
add_nonzero (state &s, bool conveyor, tree p)
{
  p = strip (p);
  for (int i = conveyor ? 0 : 1; i < 2; ++i)
    s.f[i].nonzero.push_back (p);
}

static tree_code
invert_code (tree_code code, bool floating)
{
  return invert_tree_comparison (code, floating);
}

/* COND (a boolean expression) is known to have value POLARITY in S: its
   facts, in the conveyor flavor too if CONVEYOR.  */

static void
assume (state &s, tree cond, bool polarity, bool conveyor, int depth)
{
  if (!s.reachable || depth > 16)
    return;
  cond = strip (cond);
  if (!cond)
    return;
  tree_code code = TREE_CODE (cond);
  switch (code)
    {
    case TRUTH_ANDIF_EXPR:
    case TRUTH_AND_EXPR:
    case TRUTH_ORIF_EXPR:
    case TRUTH_OR_EXPR:
      {
	bool conj = (code == TRUTH_ANDIF_EXPR || code == TRUTH_AND_EXPR);
	if (conj == polarity)
	  {
	    assume (s, TREE_OPERAND (cond, 0), polarity, conveyor, depth + 1);
	    assume (s, TREE_OPERAND (cond, 1), polarity, conveyor, depth + 1);
	  }
	else
	  {
	    /* Either: what holds in both cases.  */
	    state a = s, b = s;
	    assume (a, TREE_OPERAND (cond, 0), polarity, conveyor, depth + 1);
	    assume (b, TREE_OPERAND (cond, 1), polarity, conveyor, depth + 1);
	    meet (a, b);
	    s = a;
	  }
	return;
      }
    case TRUTH_NOT_EXPR:
      assume (s, TREE_OPERAND (cond, 0), !polarity, conveyor, depth + 1);
      return;
    case LT_EXPR: case LE_EXPR: case GT_EXPR: case GE_EXPR:
    case EQ_EXPR: case NE_EXPR:
      {
	tree a = strip (TREE_OPERAND (cond, 0));
	tree b = strip (TREE_OPERAND (cond, 1));
	tree type = TREE_TYPE (a);
	bool floating = type && FLOAT_TYPE_P (type);
	if (!polarity)
	  {
	    code = invert_code (code, floating);
	    if (code == ERROR_MARK)
	      return;
	    if (floating)
	      {
		/* '!(x < c)' is 'x >= c' or NaN: only ordered results are
		   facts.  */
		if (code == UNGE_EXPR || code == UNGT_EXPR || code == UNLE_EXPR
		    || code == UNLT_EXPR || code == UNEQ_EXPR)
		  return;
	      }
	  }
	if (floating)
	  {
	    tree c = real_constant (b);
	    tree k = a;
	    if (!c && (c = real_constant (a)))
	      {
		k = b;
		code = swap_tree_comparison (code);
	      }
	    frange r;
	    if (c && frange_for_comparison (TREE_TYPE (k), code, c, &r))
	      add_frange (s, conveyor, k, r);
	    return;
	  }
	if (type && POINTER_TYPE_P (type))
	  {
	    widest_int c;
	    if (code == NE_EXPR && constant_value (b, &c) && c == 0)
	      add_nonzero (s, conveyor, a);
	    else if (code == NE_EXPR && constant_value (a, &c) && c == 0)
	      add_nonzero (s, conveyor, b);
	    return;
	  }
	if (!type || !(INTEGRAL_TYPE_P (type) || CLASS_TYPE_P (type)))
	  return;
	widest_int ka, kb, c;
	int afi = set_index (conveyor);
	tree ba = strip_with_range (s, afi, split_offset (strip_with_range (s, afi, a), &ka));
	tree bb = strip_with_range (s, afi, split_offset (strip_with_range (s, afi, b), &kb));
	if (constant_value (bb, &c))
	  {
	    /* BA + KA CODE C + KB.  */
	    if (code == NE_EXPR)
	      {
		if (c + kb - ka == 0
		    && (POINTER_TYPE_P (TREE_TYPE (ba))
			|| INTEGRAL_TYPE_P (TREE_TYPE (ba))
			|| CLASS_TYPE_P (TREE_TYPE (ba))))
		  {
		    add_nonzero (s, conveyor, ba);
		    irng t = type_range (TREE_TYPE (ba));
		    if (t.has_lo && t.lo == 0)
		      {
			irng r;
			r.has_lo = true;
			r.lo = 1;
			add_range (s, conveyor, ba, r);
		      }
		  }
		return;
	      }
	    /* 'X - Y CODE C': a relation of X and Y.  */
	    if (TREE_CODE (ba) == MINUS_EXPR)
	      {
		widest_int k2;
		/* In wrapping arithmetic, only with the subtraction known not
		   to wrap ('X >= Y' established).  */
		bool nowrap = !TYPE_OVERFLOW_WRAPS (TREE_TYPE (ba));
		if (!nowrap)
		  nowrap = (rel_verdict (s, afi, TREE_OPERAND (ba, 0), GE_EXPR,
					 TREE_OPERAND (ba, 1))
			    == OA_PROVEN_TRUE);
		if (nowrap && !constant_value (TREE_OPERAND (ba, 1), &k2))
		  add_rel (s, conveyor,
			   strip_with_range (s, afi, TREE_OPERAND (ba, 0)),
			   strip_with_range (s, afi, TREE_OPERAND (ba, 1)),
			   c + kb - ka, code_mask (code));
	      }
	    add_range (s, conveyor, ba, range_for_comparison (code, c + kb - ka));
	    return;
	  }
	if (constant_value (ba, &c))
	  {
	    code = swap_tree_comparison (code);
	    if (code == NE_EXPR)
	      return;
	    add_range (s, conveyor, bb, range_for_comparison (code, c + ka - kb));
	    return;
	  }
	/* BA + KA CODE BB + KB: BA - BB - (KB - KA) in mask.  */
	add_rel (s, conveyor, ba, bb, kb - ka, code_mask (code));
	return;
      }
    case INTEGER_CST:
      if (integer_zerop (cond) == polarity)
	s = state::unreachable ();
      return;
    case VAR_DECL:
    case PARM_DECL:
      if (TREE_CODE (TREE_TYPE (cond)) == BOOLEAN_TYPE)
	add_range (s, conveyor, cond, irng::exact (polarity ? 1 : 0));
      else if (polarity && POINTER_TYPE_P (TREE_TYPE (cond)))
	add_nonzero (s, conveyor, cond);
      else if (!polarity)
	add_range (s, conveyor, cond, irng::exact (0));
      else
	add_nonzero (s, conveyor, cond);
      return;
    case CALL_EXPR:
      {
	tree arg;
	if (polarity && is_object_address_call_p (cond, &arg))
	  {
	    add_oaddr (s, conveyor, arg);
	    return;
	  }
	add_atom (s, conveyor, cond, polarity);
	return;
      }
    default:
      add_atom (s, conveyor, cond, polarity);
      return;
    }
}

/* ---------------------------------------------------------------------- */
/* Substitution: EXPR with parameters of a callee replaced.  */

struct subst_map { std::vector<std::pair<tree, tree>> m; };

/* EXPR with M's replacements, copied where it changes (never changing
   EXPR itself: it may be a callee's contract condition).  */

static tree
substitute_1 (tree t, subst_map &m, int depth)
{
  if (!t || depth > 200)
    return t;
  for (auto &p : m.m)
    if (t == p.first)
      return p.second;
  if (TREE_CODE (t) == TREE_LIST)
    {
      tree v = substitute_1 (TREE_VALUE (t), m, depth + 1);
      tree c = substitute_1 (TREE_CHAIN (t), m, depth + 1);
      if (v == TREE_VALUE (t) && c == TREE_CHAIN (t))
	return t;
      return tree_cons (TREE_PURPOSE (t), v, c);
    }
  if (!EXPR_P (t) || TREE_CODE (t) == STATEMENT_LIST
      || TREE_CODE (t) == BIND_EXPR)
    return t;
  int n = TREE_OPERAND_LENGTH (t);
  tree copy = NULL_TREE;
  for (int i = 0; i < n; ++i)
    {
      tree op = TREE_OPERAND (t, i);
      tree o = substitute_1 (op, m, depth + 1);
      if (o != op)
	{
	  if (!copy)
	    copy = copy_node (t);
	  TREE_OPERAND (copy, i) = o;
	}
    }
  return copy ? copy : t;
}

static tree
substitute (tree expr, subst_map &m)
{
  return substitute_1 (expr, m, 0);
}

/* EXPR with each subexpression equal to key FROM replaced by TO.  */

static tree
replace_key (tree t, tree from, tree to, int depth = 0)
{
  if (!t || depth > 200 || TYPE_P (t))
    return t;
  if (key_eq (t, from))
    return to;
  if (!EXPR_P (t) || TREE_CODE (t) == STATEMENT_LIST
      || TREE_CODE (t) == BIND_EXPR)
    return t;
  int n = TREE_OPERAND_LENGTH (t);
  tree copy = NULL_TREE;
  for (int i = 0; i < n; ++i)
    {
      tree op = TREE_OPERAND (t, i);
      tree o = replace_key (op, from, to, depth + 1);
      if (o != op)
	{
	  if (!copy)
	    copy = copy_node (t);
	  TREE_OPERAND (copy, i) = o;
	}
    }
  return copy ? copy : t;
}

/* ---------------------------------------------------------------------- */
/* The walk.  */

struct hooks
{
  /* Check the obligations of CALL (to CALLEE) in S.  */
  void (*check_call) (tree call, tree callee, state *s);
  /* Check contract_assert STMT in S.  */
  void (*check_assert) (tree stmt, state *s);
  /* Check postcondition CONTRACT of the function in S, at its exit.  */
  void (*check_post) (tree contract, state *s);
};

class walker
{
public:
  walker (tree fn, const hooks &h) : m_fn (fn), m_hooks (h) {}
  void run ();

private:
  struct jump_ctx { std::vector<state> breaks, continues; bool is_switch; };

  tree m_fn;
  const hooks &m_hooks;
  bool m_report = true;
  state m_cur;
  std::vector<jump_ctx *> m_jumps;
  std::vector<state> m_returns;
  /* Cleanups pending to the end of the current full-expression.  */
  std::vector<tree> m_cleanups;

  void stmt (tree t);
  void expr (tree t);
  void cond (tree t, state &st_true, state &st_false);
  void assign (tree lhs, tree rhs, bool atoms = true);
  void call (tree t, tree result);
  void loop (tree init, tree cnd, tree body, tree incr, bool do_while);
  void self_trust ();
  void establish_post (tree call, tree fn, tree result);
  void effects (tree call, tree fn);
};

/* Whether CONTRACT, a postcondition of FN, establishes its condition where
   a call to FN returns; *CONVEYOR: in the conveyor flavor.  */

static bool
post_establishes_p (tree contract, tree fn, bool *conveyor)
{
  if (!POSTCONDITION_P (contract)
      || !contract_failure_terminates_p (contract, fn))
    return false;
  bool c = oa_contract_conveyor_active_public (contract, fn);
  bool s = oa_contract_symbolic_active_public (contract, fn);
  *conveyor = c;
  return c || s;
}

void
walker::self_trust ()
{
  for (tree contract : fn_contracts (m_fn))
    {
      if (!PRECONDITION_P (contract))
	continue;
      tree c = CONTRACT_CONDITION (contract);
      if (!c || c == error_mark_node)
	continue;
      bool conveyor = oa_contract_conveyor_active_public (contract, m_fn);
      if (!conveyor && !oa_contract_symbolic_active_public (contract, m_fn))
	continue;
      assume (m_cur, c, true, conveyor);
    }
  /* 'this' and reference parameters are object addresses.  */
  for (tree p = DECL_ARGUMENTS (m_fn); p; p = DECL_CHAIN (p))
    if (is_this_parameter (p) || TYPE_REF_P (TREE_TYPE (p)))
      add_oaddr (m_cur, true, p);
}

/* Effects of CALL to FN on S: what it may write.  */

void
walker::effects (tree c, tree fn)
{
  /* Routes: pointer/reference arguments to non-const objects.  */
  unsigned n = call_nargs (c);
  tree parm = fn ? DECL_ARGUMENTS (fn) : NULL_TREE;
  std::vector<tree> objects;
  bool unknown = false;
  bool reach = false;
  /* Whether the callee may write objects whose address escaped earlier:
     not a conveyor callee nor a const conversion operator.  */
  bool writes_escaped
    = !(fn && (DECL_DECLARED_CONVEYOR_P (fn)
	       || (DECL_CONV_FN_P (fn) && DECL_CONST_MEMFUNC_P (fn))));
  for (unsigned i = 0; i < n; ++i, parm = parm ? DECL_CHAIN (parm) : NULL_TREE)
    {
      tree arg = call_arg (c, i);
      tree type = parm ? TREE_TYPE (parm) : TREE_TYPE (arg);
      if (!type || !(POINTER_TYPE_P (type)))
	continue;
      tree pointed = TREE_TYPE (type);
      bool is_const = pointed && TYPE_READONLY (pointed);
      tree o = pointee (arg);
      /* A string literal can't be written.  */
      {
	tree b = o;
	while (b && (TREE_CODE (b) == ARRAY_REF || TREE_CODE (b) == COMPONENT_REF))
	  b = strip (TREE_OPERAND (b, 0));
	if (b && TREE_CODE (b) == STRING_CST)
	  continue;
      }
      bool deref;
      tree root = o ? root_object (o, &deref) : NULL_TREE;
      /* Values holding pointers may point into the object, unless it is a
	 local whose address hadn't escaped.  */
      if (!(root && !deref && local_object_p (root)
	    && !escaped_p (m_cur, root)))
	reach = true;
      if (root && !deref && local_object_p (root))
	{
	  /* The address of a local escapes to the callee, unless it can't
	     keep it (a const route to a conveyor callee).  */
	  if (!escaped_p (m_cur, root)
	      && !(fn && is_const
		   && (DECL_DECLARED_CONVEYOR_P (fn) || DECL_CONV_FN_P (fn))))
	    m_cur.escaped.push_back (root);
	}
      /* A const route doesn't stop a non-conveyor callee's const_cast;
	 a conveyor callee writes nothing through one.  */
      if (is_const && fn
	  && (DECL_DECLARED_CONVEYOR_P (fn) || DECL_CONV_FN_P (fn)))
	continue;
      if (o)
	objects.push_back (o);
      else
	unknown = true;
    }
  /* A non-conveyor callee given a route to an object may end its
     lifetime (any route, const or not, or the member call's object).  */
  if (!(fn && DECL_DECLARED_CONVEYOR_P (fn)))
    {
      unsigned i = 0;
      tree parm2 = fn ? DECL_ARGUMENTS (fn) : NULL_TREE;
      if (fn && DECL_CONSTRUCTOR_P (fn))
	i = 1, parm2 = parm2 ? DECL_CHAIN (parm2) : NULL_TREE;
      for (; i < n; ++i, parm2 = parm2 ? DECL_CHAIN (parm2) : NULL_TREE)
	{
	  tree arg = call_arg (c, i);
	  tree type = parm2 ? TREE_TYPE (parm2) : TREE_TYPE (arg);
	  if (!type || !POINTER_TYPE_P (type))
	    continue;
	  tree o = pointee (arg);
	  bool d;
	  tree root = o ? root_object (o, &d) : NULL_TREE;
	  if (!root)
	    continue;
	  if (std::find (m_cur.ended.begin (), m_cur.ended.end (), root)
	      == m_cur.ended.end ())
	    m_cur.ended.push_back (root);
	  /* What was known of pointers to it.  */
	  for (int j = 0; j < 2; ++j)
	    {
	      std::vector<tree> &v = m_cur.f[j].oaddr;
	      v.erase (std::remove_if (v.begin (), v.end (),
				       [&] (tree k)
				       {
					 tree ko = pointee (k);
					 bool kd;
					 return ko && root_object (ko, &kd) == root;
				       }), v.end ());
	    }
	}
    }
  /* Kill memory facts about those objects and escaped locals.  */
  state *saved = resolve_state;
  resolve_state = &m_cur;
  auto touched = [&] (tree k) -> bool
    {
      if (!walk_tree_without_duplicates (&k, reads_memory_r, NULL)
	  && !(VAR_P (strip (k)) && escaped_p (m_cur, strip (k))))
	{
	  /* A plain variable: only if escaped.  */
	  bool any = false;
	  if (writes_escaped)
	    for (tree e : m_cur.escaped)
	      if (reads_decl_p (k, e))
		any = true;
	  return any;
	}
      if (writes_escaped)
	for (tree e : m_cur.escaped)
	  if (reads_decl_p (k, e))
	    return true;
      if (unknown)
	return true;
      /* Any memory read in K rooted at one of OBJECTS.  */
      bool hit = false;
      auto check = [&] (tree sub) -> void
	{
	  bool deref1, deref2;
	  tree r1 = root_object (sub, &deref1);
	  for (tree o : objects)
	    {
	      tree r2 = root_object (o, &deref2);
	      if (r1 && r1 == r2 && deref1 == deref2
		  && access_disjoint_p (TREE_CODE (sub) == CALL_EXPR
					? pointee (CALL_EXPR_ARG (sub, 0)) : sub,
					o))
		continue;
	      if (roots_may_overlap (m_cur, r1, deref1, r2, deref2))
		{
		  hit = true;
		  return;
		}
	    }
	};
      for_each_read (k, check);
      return hit;
    };
  kill_if (m_cur, touched);
  m_cur.aliases.erase (std::remove_if (m_cur.aliases.begin (),
				       m_cur.aliases.end (),
				       [&] (const alias_fact &a)
				       { return ((!DECL_P (a.ptr) && touched (a.ptr))
						 || touched (a.target)); }),
		       m_cur.aliases.end ());
  /* Values holding pointers given to a callee that may keep them: what
     they point to may change from now on.  */
  if (!(fn && DECL_DECLARED_CONVEYOR_P (fn)))
    {
      for (unsigned i = 0; i < n; ++i)
	if (tree rep = reach_of (m_cur, call_arg (c, i)))
	  if (std::find (m_cur.reach_escaped.begin (), m_cur.reach_escaped.end (),
			 rep) == m_cur.reach_escaped.end ())
	    m_cur.reach_escaped.push_back (rep);
      for (int i = 0; i < 2; ++i)
	{
	  std::vector<atom_fact> &v = m_cur.f[i].atoms;
	  v.erase (std::remove_if (v.begin (), v.end (),
				   [&] (const atom_fact &x)
				   { return reach_escaped_atom_p (m_cur, x.expr); }),
		   v.end ());
	}
    }
  /* Relations of values holding pointers (iterators): what they point
     into may have changed.  */
  if (reach)
    for (int i = 0; i < 2; ++i)
      {
	std::vector<atom_fact> &v = m_cur.f[i].atoms;
	v.erase (std::remove_if (v.begin (), v.end (),
				 [] (const atom_fact &x)
				 { return reach_atom_p (x.expr); }), v.end ());
      }
  /* What a call's postcondition said of objects given to it ends with
     them.  */
  kill_deps (m_cur, [&] (tree dep)
	     {
	       if (unknown)
		 return true;
	       bool d1, d2;
	       tree r1 = root_object (dep, &d1);
	       bool routed = false;
	       for (tree o : objects)
		 {
		   tree r2 = root_object (o, &d2);
		   if (r1 && r1 == r2 && d1 == d2)
		     routed = true;
		   if (r1 && r1 == r2 && d1 == d2 && access_disjoint_p (dep, o))
		     continue;
		   if (roots_may_overlap (m_cur, r1, d1, r2, d2))
		     return true;
		 }
	       /* An escaped object may change in any call, except one given a
		  route to another part of it (which says what it changes).  */
	       return (writes_escaped && !routed && r1 && !d1
		       && escaped_p (m_cur, r1));
	     });
  resolve_state = saved;
}

void
walker::establish_post (tree c, tree fn, tree result)
{
  if (!fn)
    return;
  for (tree contract : fn_contracts (fn))
    {
      bool conveyor;
      if (!post_establishes_p (contract, fn, &conveyor))
	continue;
      tree cond = CONTRACT_CONDITION (contract);
      if (!cond || cond == error_mark_node)
	continue;
      subst_map m;
      unsigned n = call_nargs (c);
      unsigned i = 0;
      tree parm = DECL_ARGUMENTS (fn);
      if (TREE_CODE (c) == AGGR_INIT_EXPR && DECL_CONSTRUCTOR_P (fn))
	{
	  /* The object constructed is the slot.  */
	  if (parm && result)
	    m.m.push_back ({ parm, build_fold_addr_expr (result) });
	  if (parm)
	    parm = DECL_CHAIN (parm);
	  i = 1;
	}
      for (; parm && i < n; parm = DECL_CHAIN (parm), ++i)
	m.m.push_back ({ parm, call_arg (c, i) });
      if (tree r = POSTCONDITION_IDENTIFIER (contract))
	if (r != error_mark_node)
	  {
	    if (result)
	      m.m.push_back ({ r, result });
	    else
	      m.m.push_back ({ r, c });
	  }
      tree sub = substitute (cond, m);
      resolve_state = &m_cur;
      current_deps.clear ();
      for (unsigned j = 0; j < n; ++j)
	{
	  tree a = call_arg (c, j);
	  if (!TREE_TYPE (a) || !POINTER_TYPE_P (TREE_TYPE (a)))
	    continue;
	  if (j == 0 && DECL_CONSTRUCTOR_P (fn))
	    continue;
	  tree o = pointee (a);
	  bool d;
	  if (o && root_object (o, &d))
	    current_deps.push_back (o);
	}
      if (dump_p)
	{
	  fprintf (stderr, "apf: post ");
	  print_generic_expr (stderr, sub);
	  fprintf (stderr, "\n");
	}
      assume (m_cur, sub, true, conveyor);
      current_deps.clear ();
      resolve_state = &m_cur;
    }
}

/* A call: arguments, the obligations, the effects, the postconditions.
   RESULT: where the result goes (a variable or slot), or NULL_TREE.  */

void
walker::call (tree c, tree result)
{
  unsigned n = call_nargs (c);
  if (TREE_CODE (c) == CALL_EXPR)
    expr (CALL_EXPR_FN (c));
  for (unsigned i = 0; i < n; ++i)
    expr (call_arg (c, i));
  if (!m_cur.reachable)
    return;
  tree fn = call_fn (c);
  /* A constructor or destructor clone: its contracts are the abstract
     function's, when the parameters correspond.  */
  if (fn && DECL_CLONED_FUNCTION_P (fn))
    {
      tree origin = DECL_CLONED_FUNCTION (fn);
      if (list_length (DECL_ARGUMENTS (origin)) == (int) call_nargs (c))
	fn = origin;
    }
  /* A constructor called on its object directly: the object is the
     result.  */
  if (fn && !result && TREE_CODE (c) == CALL_EXPR && DECL_CONSTRUCTOR_P (fn)
      && call_nargs (c) > 0)
    result = pointee (call_arg (c, 0));
  if (fn && m_report && m_hooks.check_call)
    {
      if (dump_p)
	fprintf (stderr, "apf: call %s\n", IDENTIFIER_POINTER (DECL_NAME (fn)));
      dump_state ("call", m_cur);
      m_hooks.check_call (c, fn, &m_cur);
    }
  effects (c, fn);
  /* What an earlier evaluation of this call gave is no longer its
     result.  */
  if (!pure_callee_p (fn))
    kill_if (m_cur, [&] (tree k) { return mentions_decl_p (k, c); });
  establish_post (c, fn, result);
  if (dump_p && fn)
    dump_state (lang_hooks.decl_printable_name (fn, 2), m_cur);
  /* A conveyor copy or move: the copy's values are the source's.  */
  if (fn && result && DECL_DECLARED_CONVEYOR_P (fn) && call_nargs (c) == 2
      && (DECL_COPY_CONSTRUCTOR_P (fn) || DECL_MOVE_CONSTRUCTOR_P (fn)))
    if (tree src = pointee (call_arg (c, 1)))
      assign (result, src, false);
  if (fn && TREE_THIS_VOLATILE (fn))
    m_cur = state::unreachable ();
}

/* LHS = RHS (RHS already evaluated).  */

void
walker::assign (tree lhs, tree rhs, bool with_atoms)
{
  lhs = strip (lhs);
  /* A store through a pointer known to point to a variable: what was
     known of it ends (its new value isn't tracked, as the engine).  */
  if (lhs && TREE_CODE (lhs) == INDIRECT_REF)
    {
      tree v = strip (resolve (lhs));
      if (v != lhs && (VAR_P (v) || TREE_CODE (v) == PARM_DECL))
	{
	  kill_decl (m_cur, v);
	  return;
	}
      lhs = v;
    }
  else if (lhs && TREE_CODE (lhs) == COMPONENT_REF
	   && TREE_CODE (strip (TREE_OPERAND (lhs, 0))) == INDIRECT_REF)
    {
      tree b = strip (resolve (strip (TREE_OPERAND (lhs, 0))));
      if (b != strip (TREE_OPERAND (lhs, 0)))
	lhs = build3 (COMPONENT_REF, TREE_TYPE (lhs), b,
		      TREE_OPERAND (lhs, 1), NULL_TREE);
    }
  tree srhs = strip (rhs);
  if (!lhs)
    return;
  bool is_decl = VAR_P (lhs) || TREE_CODE (lhs) == PARM_DECL
		 || TREE_CODE (lhs) == RESULT_DECL;
  tree type = TREE_TYPE (lhs);
  /* What is known of the value assigned, before LHS changes.  */
  irng r[2];
  frange fr[2];
  bool has_fr[2] = { false, false };
  widest_int k = 0;
  tree base = NULL_TREE;
  state *saved = resolve_state;
  resolve_state = &m_cur;
  for (int i = 0; i < 2; ++i)
    {
      if (type && CLASS_TYPE_P (type))
	r[i] = range_of (m_cur, i, srhs);
      else if (type && INTEGRAL_TYPE_P (type))
	{
	  r[i] = range_of (m_cur, i, srhs);
	  /* A conversion that may change the value: nothing known.  */
	  if (r[i].any () && TREE_TYPE (srhs)
	      && INTEGRAL_TYPE_P (TREE_TYPE (srhs)))
	    {
	      irng v = r[i];
	      v.intersect (type_range (TREE_TYPE (srhs)));
	      if (!fits_type_p (v, type))
		r[i] = irng ();
	      else
		r[i] = v;
	    }
	}
      if (type && SCALAR_FLOAT_TYPE_P (type))
	has_fr[i] = (frange_of (m_cur, i, srhs, fr[i])
		     && !fr[i].undefined_p ()
		     && (fr[i].type () == type || range_cast (fr[i], type)));
    }
  if (type && INTEGRAL_TYPE_P (type))
    base = split_offset (srhs, &k);
  /* Relations of the old value, shifted: 'x = x + K'.  */
  std::vector<rel_fact> shifted[2];
  bool nowrap = (type && INTEGRAL_TYPE_P (type)
		 && (!TYPE_OVERFLOW_WRAPS (type)
		     || (r[1].any () && fits_type_p (r[1], type))));
  if (base && key_eq (base, lhs) && k != 0 && nowrap)
    for (int i = 0; i < 2; ++i)
      for (const rel_fact &x : m_cur.f[i].rels)
	{
	  if (key_eq (x.a, lhs) && !mentions_p (x.b, lhs))
	    shifted[i].push_back ({ x.a, x.b, x.off + k, x.mask });
	  else if (key_eq (x.b, lhs) && !mentions_p (x.a, lhs))
	    shifted[i].push_back ({ x.a, x.b, x.off - k, x.mask });
	}
  /* Facts about RHS's own key carry to LHS (a call's result, a copy).  */
  std::vector<atom_fact> carried_atoms[2];
  std::vector<tree> carried_oaddr[2], carried_nz[2];
  std::vector<rel_fact> carried_rels[2];
  if (srhs && !key_eq (srhs, lhs))
    for (int i = 0; i < 2; ++i)
      {
	for (const rel_fact &x : m_cur.f[i].rels)
	  {
	    if (key_eq (x.a, srhs) && !mentions_p (x.b, lhs))
	      carried_rels[i].push_back ({ lhs, x.b, x.off, x.mask });
	    else if (key_eq (x.b, srhs) && !mentions_p (x.a, lhs))
	      carried_rels[i].push_back ({ x.a, lhs, x.off, x.mask });
	  }
	for (tree x : m_cur.f[i].oaddr)
	  if (key_eq (x, srhs))
	    carried_oaddr[i].push_back (lhs);
	for (tree x : m_cur.f[i].nonzero)
	  if (key_eq (x, srhs))
	    carried_nz[i].push_back (lhs);
	for (const atom_fact &x : m_cur.f[i].atoms)
	  if (with_atoms
	      && mentions_p (x.expr, srhs) && !mentions_p (x.expr, lhs)
	      /* A copy is another object: predicates of the object (by
		 address) aren't the copy's.  */
	      && !mentions_by_address_p (x.expr, srhs))
	    {
	      tree e = replace_key (x.expr, srhs, lhs);
	      if (e != x.expr && !key_eq (e, x.expr))
		carried_atoms[i].push_back ({ e, x.polarity, x.deps });
	    }
      }
  /* The new alias of a pointer variable.  */
  tree alias_target = NULL_TREE;
  if ((is_decl || TREE_CODE (lhs) == COMPONENT_REF
       || TREE_CODE (lhs) == ARRAY_REF)
      && type && POINTER_TYPE_P (type) && srhs
      && (TREE_CODE (srhs) == ADDR_EXPR || VAR_P (srhs)
	  || TREE_CODE (srhs) == PARM_DECL
	  || TREE_CODE (srhs) == COMPONENT_REF))
    alias_target = resolve (srhs);
  resolve_state = saved;

  tree rhs_reach = srhs ? reach_of (m_cur, srhs) : NULL_TREE;
  /* LHS changes.  */
  if (is_decl)
    {
      kill_decl (m_cur, lhs);
      m_cur.reach.erase (std::remove_if (m_cur.reach.begin (),
					 m_cur.reach.end (),
					 [&] (const std::pair<tree, tree> &r)
					 { return r.first == lhs; }),
			 m_cur.reach.end ());
      if (rhs_reach && rhs_reach != lhs)
	m_cur.reach.push_back ({ lhs, rhs_reach });
      if (rhs_reach && !local_object_p (lhs)
	  && std::find (m_cur.reach_escaped.begin (), m_cur.reach_escaped.end (),
			rhs_reach) == m_cur.reach_escaped.end ())
	m_cur.reach_escaped.push_back (rhs_reach);
    }
  else
    {
      /* A store to memory: what may read it.  */
      state *saved2 = resolve_state;
      resolve_state = &m_cur;
      bool d1;
      tree r1 = root_object (lhs, &d1);
      tree field = TREE_CODE (lhs) == COMPONENT_REF ? TREE_OPERAND (lhs, 1)
						     : NULL_TREE;
      auto touched = [&] (tree key) -> bool
	{
	  bool hit = false;
	  auto check = [&] (tree sub)
	    {
	      if (hit)
		return;
	      tree sf = TREE_CODE (sub) == COMPONENT_REF ? TREE_OPERAND (sub, 1)
							 : NULL_TREE;
	      bool d2;
	      tree r2 = root_object (sub, &d2);
	      if (!roots_may_overlap (m_cur, r1, d1, r2, d2))
		return;
	      /* Different members or elements of an object don't overlap
		 (no unions).  */
	      if (field && sf && field != sf
		  && TREE_CODE (DECL_CONTEXT (field)) != UNION_TYPE)
		return;
	      if (r1 == r2 && d1 == d2 && access_disjoint_p (lhs, sub))
		return;
	      hit = true;
	    };
	  for_each_read (key, check);
	  if (!hit && d1)
	    walk_tree_without_duplicates (&key,
					  [] (tree *tp, int *ws, void *d) -> tree
					  {
					    if (TYPE_P (*tp))
					      *ws = 0;
					    else if (VAR_P (*tp)
						     || TREE_CODE (*tp) == PARM_DECL)
					      (*(decltype (check) *) d) (*tp);
					    return NULL_TREE;
					  }, &check);
	  return hit;
	};
      kill_deps (m_cur, [&] (tree dep)
		 {
		   bool dd;
		   tree rd = root_object (dep, &dd);
		   if (rd && rd == r1 && dd == d1 && access_disjoint_p (dep, lhs))
		     return false;
		   return roots_may_overlap (m_cur, rd, dd, r1, d1);
		 });
      kill_if (m_cur, touched);
  m_cur.aliases.erase (std::remove_if (m_cur.aliases.begin (),
				       m_cur.aliases.end (),
				       [&] (const alias_fact &a)
				       { return ((!DECL_P (a.ptr) && touched (a.ptr))
						 || touched (a.target)); }),
		       m_cur.aliases.end ());
      resolve_state = saved2;
    }

  /* What LHS now is.  */
  for (int i = 0; i < 2; ++i)
    {
      bool conv = i == 0;
      factset &f = m_cur.f[i];
      if (r[i].any ())
	add_range_1 (f, lhs, r[i]);
      if (has_fr[i])
	f.franges.push_back ({ lhs, fr[i] });
      for (const rel_fact &x : shifted[i])
	f.rels.push_back (x);
      for (const rel_fact &x : carried_rels[i])
	f.rels.push_back (x);
      for (const atom_fact &x : carried_atoms[i])
	f.atoms.push_back (x);
      for (tree x : carried_oaddr[i])
	f.oaddr.push_back (x);
      for (tree x : carried_nz[i])
	f.nonzero.push_back (x);
      (void) conv;
    }
  if (base && !key_eq (base, lhs) && !mentions_p (base, lhs)
      && type && INTEGRAL_TYPE_P (type) && TREE_TYPE (base)
      && INTEGRAL_TYPE_P (TREE_TYPE (base)))
    {
      widest_int c;
      if (!constant_value (base, &c) && (k == 0 || nowrap))
	add_rel (m_cur, true, lhs, base, k, ORD_EQ);
      /* 'X + Y' with Y's range known: LHS - X within it.  */
      if (nowrap && (TREE_CODE (base) == PLUS_EXPR
		     || TREE_CODE (base) == MINUS_EXPR))
	for (int side = 0; side < 2; ++side)
	  {
	    if (side == 1 && TREE_CODE (base) == MINUS_EXPR)
	      break;
	    tree x = strip (TREE_OPERAND (base, side));
	    tree y = TREE_OPERAND (base, 1 - side);
	    if (mentions_p (x, lhs) || constant_value (x, &c))
	      continue;
	    for (int i = 0; i < 2; ++i)
	      {
		irng ry = range_of (m_cur, i, y);
		if (TREE_CODE (base) == MINUS_EXPR)
		  ry = negate (ry);
		if (ry.has_hi)
		  add_rel (m_cur, i == 0, lhs, x, ry.hi + k, ORD_LT | ORD_EQ);
		if (ry.has_lo)
		  add_rel (m_cur, i == 0, lhs, x, ry.lo + k, ORD_GT | ORD_EQ);
	      }
	  }
    }
  if (alias_target && !mentions_decl_p (alias_target, lhs))
    {
      m_cur.aliases.push_back ({ lhs, alias_target });
      tree t = strip (alias_target);
      if (TREE_CODE (t) == ADDR_EXPR)
	{
	  add_oaddr (m_cur, true, lhs);
	  add_nonzero (m_cur, true, lhs);
	}
    }
}

/* Evaluate condition T from m_cur: the states where it is true and false.  */

void
walker::cond (tree t, state &st_true, state &st_false)
{
  tree s = tree_strip_any_location_wrapper (t);
  if (s && (TREE_CODE (s) == TRUTH_ANDIF_EXPR
	    || TREE_CODE (s) == TRUTH_ORIF_EXPR))
    {
      bool andif = TREE_CODE (s) == TRUTH_ANDIF_EXPR;
      state a_t, a_f;
      cond (TREE_OPERAND (s, 0), a_t, a_f);
      state b_t, b_f;
      m_cur = andif ? a_t : a_f;
      cond (TREE_OPERAND (s, 1), b_t, b_f);
      if (andif)
	{
	  st_true = b_t;
	  st_false = a_f;
	  meet (st_false, b_f);
	}
      else
	{
	  st_true = a_t;
	  meet (st_true, b_t);
	  st_false = b_f;
	}
      return;
    }
  if (s && TREE_CODE (s) == TRUTH_NOT_EXPR)
    {
      cond (TREE_OPERAND (s, 0), st_false, st_true);
      return;
    }
  if (s && (CONVERT_EXPR_P (s) || TREE_CODE (s) == NON_LVALUE_EXPR)
      && TREE_CODE (TREE_TYPE (s)) == BOOLEAN_TYPE
      && TREE_CODE (TREE_TYPE (TREE_OPERAND (s, 0))) == BOOLEAN_TYPE)
    {
      cond (TREE_OPERAND (s, 0), st_true, st_false);
      return;
    }
  if (s && TREE_CODE (s) == CLEANUP_POINT_EXPR)
    {
      cond (TREE_OPERAND (s, 0), st_true, st_false);
      return;
    }
  if (s && TREE_CODE (s) == COMPOUND_EXPR)
    {
      expr (TREE_OPERAND (s, 0));
      cond (TREE_OPERAND (s, 1), st_true, st_false);
      return;
    }
  expr (t);
  st_true = m_cur;
  st_false = m_cur;
  state *saved = resolve_state;
  resolve_state = &m_cur;
  assume (st_true, t, true, true);
  assume (st_false, t, false, true);
  resolve_state = saved;
}

/* Evaluate expression T for its effects, in order.  */

void
walker::expr (tree t)
{
  if (!t || !m_cur.reachable)
    return;
  t = tree_strip_any_location_wrapper (t);
  if (dump_p)
    fprintf (stderr, "apf: expr %s\n", get_tree_code_name (TREE_CODE (t)));
  switch (TREE_CODE (t))
    {
    case INTEGER_CST: case REAL_CST: case STRING_CST: case VAR_DECL:
    case PARM_DECL: case RESULT_DECL: case FUNCTION_DECL: case CONST_DECL:
    case FIELD_DECL: case LABEL_DECL: case TYPE_DECL: case NAMESPACE_DECL:
    case LAMBDA_EXPR: case PTRMEM_CST: case OVERLOAD: case BASELINK:
    case TEMPLATE_DECL: case COMPLEX_CST: case VECTOR_CST:
      return;
    case MODIFY_EXPR:
    case INIT_EXPR:
      {
	tree lhs = TREE_OPERAND (t, 0), rhs = TREE_OPERAND (t, 1);
	tree srhs = tree_strip_any_location_wrapper (rhs);
	/* 'x = f (...)': the call's result is x.  */
	if (TREE_CODE (srhs) == AGGR_INIT_EXPR)
	  {
	    expr (lhs);
	    call (srhs, strip (lhs));
	    return;
	  }
	if (TREE_CODE (srhs) == CALL_EXPR && TREE_CODE (t) == INIT_EXPR)
	  {
	    expr (lhs);
	    tree sl = strip (lhs);
	    if (sl && (VAR_P (sl) || TREE_CODE (sl) == RESULT_DECL))
	      {
		kill_decl (m_cur, sl);
		call (srhs, sl);
		return;
	      }
	    call (srhs, NULL_TREE);
	    assign (lhs, srhs);
	    return;
	  }
	if (TREE_CODE (srhs) == TARGET_EXPR)
	  {
	    expr (lhs);
	    tree init = TARGET_EXPR_INITIAL (srhs);
	    tree sinit = tree_strip_any_location_wrapper (init);
	    if (sinit && TREE_CODE (sinit) == AGGR_INIT_EXPR)
	      call (sinit, strip (lhs));
	    else
	      {
		expr (init);
		assign (lhs, init);
	      }
	    return;
	  }
	if (TREE_CODE (srhs) == CONSTRUCTOR)
	  {
	    expr (lhs);
	    unsigned HOST_WIDE_INT ix;
	    tree f, v;
	    FOR_EACH_CONSTRUCTOR_ELT (CONSTRUCTOR_ELTS (srhs), ix, f, v)
	      expr (v);
	    if (VAR_P (strip (lhs)))
	      kill_decl (m_cur, strip (lhs));
	    tree base = strip (lhs);
	    tree next = (CLASS_TYPE_P (TREE_TYPE (srhs))
			 ? next_aggregate_field (TYPE_FIELDS (TREE_TYPE (srhs)))
			 : NULL_TREE);
	    FOR_EACH_CONSTRUCTOR_ELT (CONSTRUCTOR_ELTS (srhs), ix, f, v)
	      {
		/* A positional element: the next member.  */
		if (!f && next)
		  f = next;
		if (f && TREE_CODE (f) == FIELD_DECL)
		  next = next_aggregate_field (DECL_CHAIN (f));
		if (f && TREE_CODE (f) == FIELD_DECL)
		{
		  tree ref = build3 (COMPONENT_REF, TREE_TYPE (f), base, f,
				     NULL_TREE);
		  assign (ref, v);
		}
	      }
	    return;
	  }
	expr (rhs);
	expr (lhs);
	assign (lhs, rhs);
	return;
      }
    case PREINCREMENT_EXPR:
    case PREDECREMENT_EXPR:
    case POSTINCREMENT_EXPR:
    case POSTDECREMENT_EXPR:
      {
	tree op = TREE_OPERAND (t, 0);
	expr (op);
	tree type = TREE_TYPE (op);
	if (type && INTEGRAL_TYPE_P (type))
	  {
	    bool inc = (TREE_CODE (t) == PREINCREMENT_EXPR
			|| TREE_CODE (t) == POSTINCREMENT_EXPR);
	    tree one = build_int_cst (type, 1);
	    tree v = build2 (inc ? PLUS_EXPR : MINUS_EXPR, type, op, one);
	    assign (op, v);
	  }
	else
	  assign (op, error_mark_node);
	return;
      }
    case CALL_EXPR:
    case AGGR_INIT_EXPR:
      call (t, TREE_CODE (t) == AGGR_INIT_EXPR ? AGGR_INIT_EXPR_SLOT (t)
						: NULL_TREE);
      return;
    case TARGET_EXPR:
      {
	tree slot = TARGET_EXPR_SLOT (t);
	tree init = TARGET_EXPR_INITIAL (t);
	tree sinit = tree_strip_any_location_wrapper (init);
	if (sinit && TREE_CODE (sinit) == AGGR_INIT_EXPR)
	  call (sinit, slot);
	else if (sinit && TREE_CODE (sinit) == CALL_EXPR)
	  {
	    /* The call's result is the temporary (elided).  */
	    kill_decl (m_cur, slot);
	    call (sinit, slot);
	  }
	else if (sinit && VOID_TYPE_P (TREE_TYPE (sinit)))
	  {
	    /* Initialized in place (a constructor call on the slot).  */
	    kill_decl (m_cur, slot);
	    expr (init);
	  }
	else if (sinit)
	  {
	    expr (init);
	    if (TREE_CODE (sinit) != CONSTRUCTOR)
	      assign (slot, init);
	    else
	      kill_decl (m_cur, slot);
	  }
	if (TARGET_EXPR_CLEANUP (t) && !CLEANUP_EH_ONLY (t))
	  m_cleanups.push_back (TARGET_EXPR_CLEANUP (t));
	return;
      }
    case CLEANUP_POINT_EXPR:
      {
	size_t mark = m_cleanups.size ();
	expr (TREE_OPERAND (t, 0));
	while (m_cleanups.size () > mark)
	  {
	    tree c = m_cleanups.back ();
	    m_cleanups.pop_back ();
	    expr (c);
	  }
	return;
      }
    case TRUTH_ANDIF_EXPR:
    case TRUTH_ORIF_EXPR:
    case TRUTH_NOT_EXPR:
      {
	state a, b;
	cond (t, a, b);
	meet (a, b);
	m_cur = a;
	return;
      }
    case COND_EXPR:
      {
	state a, b;
	cond (TREE_OPERAND (t, 0), a, b);
	m_cur = a;
	expr (TREE_OPERAND (t, 1));
	a = m_cur;
	m_cur = b;
	expr (TREE_OPERAND (t, 2));
	meet (m_cur, a);
	return;
      }
    case STATEMENT_LIST:
    case BIND_EXPR:
    case DECL_EXPR:
    case RETURN_EXPR:
    case IF_STMT:
    case EXPR_STMT:
      stmt (t);
      return;
    case STMT_EXPR:
      stmt (STMT_EXPR_STMT (t));
      return;
    case PRECONDITION_STMT:
    case POSTCONDITION_STMT:
    case ASSERTION_STMT:
    case EH_ELSE_EXPR:
    case TRY_FINALLY_EXPR:
    case CLEANUP_STMT:
    case TRY_BLOCK:
      stmt (t);
      return;
    case CONSTRUCTOR:
      {
	unsigned HOST_WIDE_INT ix;
	tree f, v;
	FOR_EACH_CONSTRUCTOR_ELT (CONSTRUCTOR_ELTS (t), ix, f, v)
	  expr (v);
	return;
      }
    case ADDR_EXPR:
      expr (TREE_OPERAND (t, 0));
      return;
    case THROW_EXPR:
      expr (TREE_OPERAND (t, 0));
      m_cur = state::unreachable ();
      return;
    default:
      break;
    }
  if (EXPR_P (t) || TREE_CODE (t) == TREE_LIST)
    {
      if (TREE_CODE (t) == TREE_LIST)
	{
	  for (tree l = t; l; l = TREE_CHAIN (l))
	    expr (TREE_VALUE (l));
	  return;
	}
      int n = TREE_OPERAND_LENGTH (t);
      for (int i = 0; i < n; ++i)
	expr (TREE_OPERAND (t, i));
    }
}

/* A loop: INIT; while (CND) { BODY; INCR; } (DO_WHILE: body first).  */

void
walker::loop (tree init, tree cnd, tree body, tree incr, bool do_while)
{
  if (init)
    stmt (init);
  if (!m_cur.reachable)
    return;
  state entry = m_cur;
  state head = entry;
  bool saved_report = m_report;
  jump_ctx ctx;
  ctx.is_switch = false;
  const int max_iter = 6;
  state exit;
  for (int iter = 0; ; ++iter)
    {
      bool final = (iter == max_iter);
      m_report = final ? saved_report : false;
      ctx.breaks.clear ();
      ctx.continues.clear ();
      m_cur = head;
      state st_t, st_f = state::unreachable ();
      if (!do_while)
	{
	  if (cnd)
	    cond (cnd, st_t, st_f);
	  else
	    st_t = m_cur;
	  m_cur = st_t;
	}
      m_jumps.push_back (&ctx);
      stmt (body);
      m_jumps.pop_back ();
      for (const state &c : ctx.continues)
	meet (m_cur, c);
      if (do_while)
	{
	  if (cnd)
	    {
	      state t2, f2;
	      cond (cnd, t2, f2);
	      m_cur = t2;
	      st_f = f2;
	    }
	}
      else if (incr)
	expr (incr);
      state back = m_cur;
      exit = st_f;
      for (const state &b : ctx.breaks)
	meet (exit, b);
      if (final)
	break;
      state next = entry;
      meet (next, back);
      if (iter >= 3)
	{
	  /* Widen: keep only what HEAD and NEXT agree on exactly.  */
	  for (int i = 0; i < 2; ++i)
	    {
	      std::vector<range_fact> keep;
	      for (const range_fact &x : next.f[i].ranges)
		for (const range_fact &y : head.f[i].ranges)
		  if (key_eq (x.key, y.key))
		    {
		      range_fact w = x;
		      if (!(y.r.has_lo && x.r.has_lo && y.r.lo == x.r.lo))
			w.r.has_lo = false;
		      if (!(y.r.has_hi && x.r.has_hi && y.r.hi == x.r.hi))
			w.r.has_hi = false;
		      if (w.r.any ())
			keep.push_back (w);
		      break;
		    }
	      next.f[i].ranges = keep;
	      std::vector<frange_fact> fkeep;
	      for (const frange_fact &x : next.f[i].franges)
		for (const frange_fact &y : head.f[i].franges)
		  if (key_eq (x.key, y.key) && x.r == y.r)
		    fkeep.push_back (x);
	      next.f[i].franges = fkeep;
	      std::vector<rel_fact> rkeep;
	      for (const rel_fact &x : next.f[i].rels)
		for (const rel_fact &y : head.f[i].rels)
		  if (rel_eq (x, y))
		    rkeep.push_back (x);
	      next.f[i].rels = rkeep;
	    }
	}
      if (same_state (next, head))
	{
	  /* One more, reporting.  */
	  iter = max_iter - 1;
	}
      head = next;
    }
  m_report = saved_report;
  m_cur = exit;
}

/* Walk statement T.  */

void
walker::stmt (tree t)
{
  if (!t)
    return;
  t = tree_strip_any_location_wrapper (t);
  if (dump_p)
    fprintf (stderr, "apf: stmt %s%s\n", get_tree_code_name (TREE_CODE (t)),
	     m_cur.reachable ? "" : " (unreachable)");
  /* A label makes code reachable again from somewhere unknown.  */
  if (TREE_CODE (t) == LABEL_EXPR)
    {
      m_cur = state ();
      return;
    }
  if (TREE_CODE (t) == CASE_LABEL_EXPR)
    return;
  if (!m_cur.reachable && TREE_CODE (t) != STATEMENT_LIST
      && TREE_CODE (t) != BIND_EXPR)
    return;
  switch (TREE_CODE (t))
    {
    case STATEMENT_LIST:
      for (tree_stmt_iterator i = tsi_start (t); !tsi_end_p (i); tsi_next (&i))
	{
	  tree s = tsi_stmt (i);
	  if (TREE_CODE (s) == CASE_LABEL_EXPR && !m_jumps.empty ())
	    {
	      /* Reached from the switch, or by falling through.  */
	      jump_ctx *j = NULL;
	      for (auto it = m_jumps.rbegin (); it != m_jumps.rend (); ++it)
		if ((*it)->is_switch)
		  {
		    j = *it;
		    break;
		  }
	      if (j && !j->continues.empty ())
		meet (m_cur, j->continues[0]);
	      continue;
	    }
	  stmt (s);
	}
      return;
    case BIND_EXPR:
      stmt (BIND_EXPR_BODY (t));
      return;
    case DECL_EXPR:
      {
	tree d = DECL_EXPR_DECL (t);
	if (VAR_P (d) && !TREE_STATIC (d))
	  {
	    kill_decl (m_cur, d);
	    tree init = DECL_INITIAL (d);
	    if (init && init != error_mark_node)
	      {
		tree s = tree_strip_any_location_wrapper (init);
		if (TREE_CODE (s) == AGGR_INIT_EXPR)
		  call (s, d);
		else if (TREE_CODE (s) == TARGET_EXPR
			 || TREE_CODE (s) == CONSTRUCTOR)
		  {
		    tree e = build2 (INIT_EXPR, TREE_TYPE (d), d, s);
		    expr (e);
		  }
		else
		  {
		    expr (init);
		    assign (d, init);
		  }
	      }
	  }
	return;
      }
    case EXPR_STMT:
      expr (EXPR_STMT_EXPR (t));
      return;
    case CLEANUP_POINT_EXPR:
      expr (t);
      return;
    case IF_STMT:
      {
	state st_t, st_f;
	cond (IF_COND (t), st_t, st_f);
	m_cur = st_t;
	stmt (THEN_CLAUSE (t));
	state after_then = m_cur;
	m_cur = st_f;
	stmt (ELSE_CLAUSE (t));
	meet (m_cur, after_then);
	return;
      }
    case FOR_STMT:
      loop (FOR_INIT_STMT (t), FOR_COND (t), FOR_BODY (t), FOR_EXPR (t),
	    false);
      return;
    case WHILE_STMT:
      loop (NULL_TREE, WHILE_COND (t), WHILE_BODY (t), NULL_TREE, false);
      return;
    case DO_STMT:
      loop (NULL_TREE, DO_COND (t), DO_BODY (t), NULL_TREE, true);
      return;
    case SWITCH_STMT:
      {
	expr (SWITCH_STMT_COND (t));
	jump_ctx ctx;
	ctx.is_switch = true;
	/* The state at the switch: in continues[0] (case labels meet it).  */
	ctx.continues.push_back (m_cur);
	state entry = m_cur;
	m_cur = state::unreachable ();
	m_jumps.push_back (&ctx);
	stmt (SWITCH_STMT_BODY (t));
	m_jumps.pop_back ();
	state exit = m_cur;
	for (const state &b : ctx.breaks)
	  meet (exit, b);
	/* Without a default label, the switch may skip its body.  */
	meet (exit, entry);
	m_cur = exit;
	return;
      }
    case BREAK_STMT:
      if (!m_jumps.empty ())
	m_jumps.back ()->breaks.push_back (m_cur);
      m_cur = state::unreachable ();
      return;
    case CONTINUE_STMT:
      for (auto it = m_jumps.rbegin (); it != m_jumps.rend (); ++it)
	if (!(*it)->is_switch)
	  {
	    (*it)->continues.push_back (m_cur);
	    break;
	  }
      m_cur = state::unreachable ();
      return;
    case RETURN_EXPR:
      {
	tree v = TREE_OPERAND (t, 0);
	if (v)
	  expr (v);
	if (m_cur.reachable)
	  m_returns.push_back (m_cur);
	m_cur = state::unreachable ();
	return;
      }
    case CLEANUP_STMT:
      {
	stmt (CLEANUP_BODY (t));
	if (!CLEANUP_EH_ONLY (t))
	  expr (CLEANUP_EXPR (t));
	return;
      }
    case TRY_BLOCK:
      {
	state entry = m_cur;
	stmt (TRY_STMTS (t));
	state after = m_cur;
	/* A handler: reached from anywhere in the try block; what held
	   at its start and was not changed is not tracked: no facts.  */
	for (tree h = TRY_HANDLERS (t); h; )
	  {
	    tree hs = h;
	    if (TREE_CODE (h) == STATEMENT_LIST)
	      {
		for (tree_stmt_iterator i = tsi_start (h); !tsi_end_p (i);
		     tsi_next (&i))
		  {
		    m_cur = state ();
		    stmt (HANDLER_BODY (tsi_stmt (i)));
		    meet (after, m_cur);
		  }
		break;
	      }
	    m_cur = state ();
	    if (TREE_CODE (hs) == HANDLER)
	      stmt (HANDLER_BODY (hs));
	    meet (after, m_cur);
	    break;
	  }
	(void) entry;
	m_cur = after;
	return;
      }
    case HANDLER:
      stmt (HANDLER_BODY (t));
      return;
    case EH_SPEC_BLOCK:
      stmt (EH_SPEC_STMTS (t));
      return;
    case MUST_NOT_THROW_EXPR:
      stmt (TREE_OPERAND (t, 0));
      return;
    case TRY_FINALLY_EXPR:
      stmt (TREE_OPERAND (t, 0));
      stmt (TREE_OPERAND (t, 1));
      return;
    case EH_ELSE_EXPR:
      /* The normal path's cleanup.  */
      stmt (TREE_OPERAND (t, 0));
      return;
    case ASSERTION_STMT:
      {
	dump_state ("assert", m_cur);
	if (m_report && m_hooks.check_assert)
	  m_hooks.check_assert (t, &m_cur);
	tree c = CONTRACT_CONDITION (t);
	if (c && c != error_mark_node
	    && contract_failure_terminates_p (t, m_fn))
	  {
	    bool conveyor = oa_contract_conveyor_active_public (t, m_fn);
	    if (conveyor || oa_contract_symbolic_active_public (t, m_fn))
	      {
		resolve_state = &m_cur;
		assume (m_cur, c, true, conveyor);
		resolve_state = &m_cur;
	      }
	  }
	return;
      }
    case PRECONDITION_STMT:
    case POSTCONDITION_STMT:
    case USING_STMT:
    case GOTO_EXPR:
      if (TREE_CODE (t) == GOTO_EXPR)
	m_cur = state::unreachable ();
      return;
    default:
      expr (t);
      return;
    }
}

void
walker::run ()
{
  tree body = DECL_SAVED_TREE (m_fn);
  if (!body)
    return;
  resolve_state = &m_cur;
  self_trust ();
  stmt (body);
  if (m_cur.reachable)
    m_returns.push_back (m_cur);
  if (!m_hooks.check_post)
    return;
  state exit = state::unreachable ();
  for (const state &r : m_returns)
    meet (exit, r);
  if (!exit.reachable)
    return;
  for (tree contract : fn_contracts (m_fn))
    if (POSTCONDITION_P (contract))
      {
	/* The postcondition's result name is the function's result.  */
	if (tree r = POSTCONDITION_IDENTIFIER (contract))
	  if (r != error_mark_node && DECL_RESULT (m_fn))
	    exit.aliases.push_back ({ r, DECL_RESULT (m_fn) });
	dump_state ("exit", exit);
	m_hooks.check_post (contract, &exit);
      }
}

/* Walk FNDECL's body with H.  */

static void
walk_function (tree fndecl, const hooks &h)
{
  const char *dv = getenv ("APF_DUMP");
  dump_p = dv && (!*dv || strstr (lang_hooks.decl_printable_name (fndecl, 2), dv));
  if (TREE_CODE (fndecl) != FUNCTION_DECL || !DECL_SAVED_TREE (fndecl)
      || DECL_CLONED_FUNCTION_P (fndecl))
    return;
  if (dump_p)
    fprintf (stderr, "apf: function %s\n",
	     lang_hooks.decl_printable_name (fndecl, 2));
  walker w (fndecl, h);
  tree saved = current_function_decl;
  current_function_decl = fndecl;
  w.run ();
  current_function_decl = saved;
  resolve_state = NULL;
}

/* ---------------------------------------------------------------------- */
/* The checks: a conjunct's verdict in S's facts, in the conveyor flavor
   (CONVEYOR: only what conveyor obligations may use) or any.  */

static bool oaddr_p (state &s, int fi, tree p, oa_unprovable_reason *why,
		     int depth = 0);

/* Whether pointer P is provably an object address.  */

static bool
oaddr_p (state &s, int fi, tree p, oa_unprovable_reason *why, int depth)
{
  p = strip (p);
  if (!p || depth > 8)
    return false;
  for (tree k : s.f[fi].oaddr)
    if (key_eq (k, p))
      return true;
  if (TREE_CODE (p) == PARM_DECL
      && (is_this_parameter (p) || TYPE_REF_P (TREE_TYPE (p)))
      && std::find (s.ended.begin (), s.ended.end (), p) == s.ended.end ())
    return true;
  tree r = resolve (p);
  if (r != p)
    return oaddr_p (s, fi, r, why, depth + 1);
  if (TREE_CODE (p) == ADDR_EXPR)
    {
      bool deref;
      tree root = root_object (TREE_OPERAND (p, 0), &deref);
      if (!root)
	return false;
      if (std::find (s.ended.begin (), s.ended.end (), root) != s.ended.end ())
	return false;
      if (!deref)
	return (VAR_P (root) || TREE_CODE (root) == PARM_DECL
		|| TREE_CODE (root) == RESULT_DECL);

      /* '&p->f': P an object address.  */
      return oaddr_p (s, fi, root, why, depth + 1);
    }
  if (why && *why == OA_UNPROVABLE_NONE)
    *why = OA_UNPROVABLE_NO_FACT;
  return false;
}

/* Whether pointer or integer P is provably nonzero (TRUE) or zero.  */

static oa_proof_result
nonzero_verdict (state &s, int fi, tree p)
{
  p = strip (p);
  if (!p)
    return OA_UNKNOWN;
  if (TREE_TYPE (p) && POINTER_TYPE_P (TREE_TYPE (p)))
    {
      if (integer_zerop (p))
	return OA_PROVEN_FALSE;
      if (oaddr_p (s, fi, p, NULL))
	return OA_PROVEN_TRUE;
      for (tree k : s.f[fi].nonzero)
	if (key_eq (k, p))
	  return OA_PROVEN_TRUE;
      if (TREE_CODE (p) == ADDR_EXPR)
	return OA_PROVEN_TRUE;
      return OA_UNKNOWN;
    }
  return cmp_verdict (s, fi, p, NE_EXPR, 0);
}

static oa_proof_result eval_bool (state &s, int fi, tree e, int depth = 0);

static oa_proof_result
invert (oa_proof_result v)
{
  return (v == OA_PROVEN_TRUE ? OA_PROVEN_FALSE
	  : v == OA_PROVEN_FALSE ? OA_PROVEN_TRUE : OA_UNKNOWN);
}

/* The verdict of comparison 'A CODE B'.  */

static oa_proof_result
compare_verdict (state &s, int fi, tree a, tree_code code, tree b)
{
  a = strip (a);
  b = strip (b);
  /* A class object passed by invisible reference: its value.  */
  auto by_value = [] (tree &x)
    {
      if (x && TREE_CODE (x) == ADDR_EXPR
	  && CLASS_TYPE_P (TREE_TYPE (TREE_OPERAND (x, 0))))
	x = strip (TREE_OPERAND (x, 0));
    };
  by_value (a);
  by_value (b);
  if (dump_p)
    {
      fprintf (stderr, "apf: compare %s ", get_tree_code_name (TREE_CODE (a)));
      print_generic_expr (stderr, a);
      fprintf (stderr, " %s ", get_tree_code_name (code));
      print_generic_expr (stderr, b);
      fprintf (stderr, "\n");
    }
  tree type = TREE_TYPE (a);
  if (!type)
    return OA_UNKNOWN;
  if (FLOAT_TYPE_P (type) || (TREE_TYPE (b) && FLOAT_TYPE_P (TREE_TYPE (b))))
    {
      tree c = real_constant (b), k = a;
      if (!c && (c = real_constant (a)))
	{
	  k = b;
	  code = swap_tree_comparison (code);
	}
      frange fr;
      if (c && frange_of (s, fi, k, fr))
	return frange_verdict (fr, code, c);
      return OA_UNKNOWN;
    }
  if (POINTER_TYPE_P (type))
    {
      widest_int c;
      if ((code == NE_EXPR || code == EQ_EXPR)
	  && constant_value (b, &c) && c == 0)
	{
	  oa_proof_result v = nonzero_verdict (s, fi, a);
	  return code == NE_EXPR ? v : invert (v);
	}
      if ((code == NE_EXPR || code == EQ_EXPR)
	  && constant_value (a, &c) && c == 0)
	{
	  oa_proof_result v = nonzero_verdict (s, fi, b);
	  return code == NE_EXPR ? v : invert (v);
	}
      return OA_UNKNOWN;
    }
  if (!INTEGRAL_TYPE_P (type) && !CLASS_TYPE_P (type))
    return OA_UNKNOWN;
  widest_int c, k;
  tree base = split_offset (b, &k);
  if (constant_value (base, &c))
    {
      widest_int ka;
      tree ba = split_offset (a, &ka);
      if (TREE_CODE (ba) == MINUS_EXPR)
	{
	  widest_int k2;
	  if (!constant_value (TREE_OPERAND (ba, 1), &k2))
	    {
	      oa_proof_result v = rel_verdict (s, fi, TREE_OPERAND (ba, 0), code,
					       TREE_OPERAND (ba, 1), c + k - ka);
	      if (v != OA_UNKNOWN)
		return v;
	    }
	}
      return cmp_verdict (s, fi, ba, code, c + k - ka);
    }
  base = split_offset (a, &k);
  if (constant_value (base, &c))
    {
      widest_int kb;
      tree bb = split_offset (b, &kb);
      return cmp_verdict (s, fi, bb, swap_tree_comparison (code), c + k - kb);
    }
  oa_proof_result v = rel_verdict (s, fi, a, code, b);
  if (v == OA_UNKNOWN)
    {
      /* From the range of 'A - B' as an expression ('x + y < 10').  */
      state *saved = resolve_state;
      resolve_state = &s;
      irng ra = range_of (s, fi, a), rb = range_of (s, fi, b);
      resolve_state = saved;
      irng d;
      if (ra.any () && rb.any () && irng_binop (MINUS_EXPR, ra, rb, &d))
	v = range_mask_verdict (d, code_mask (code), 0);
    }
  return v;
}

/* The verdict of boolean expression E.  */

static oa_proof_result
eval_bool (state &s, int fi, tree e, int depth)
{
  e = strip (e);
  if (!e || depth > 16)
    return OA_UNKNOWN;
  while (TREE_CODE (e) == CLEANUP_POINT_EXPR)
    e = strip (TREE_OPERAND (e, 0));
  state *saved = resolve_state;
  resolve_state = &s;
  oa_proof_result v = OA_UNKNOWN;
  switch (TREE_CODE (e))
    {
    case TRUTH_NOT_EXPR:
      v = invert (eval_bool (s, fi, TREE_OPERAND (e, 0), depth + 1));
      break;
    case TRUTH_ANDIF_EXPR:
    case TRUTH_AND_EXPR:
      {
	oa_proof_result a = eval_bool (s, fi, TREE_OPERAND (e, 0), depth + 1);
	oa_proof_result b = eval_bool (s, fi, TREE_OPERAND (e, 1), depth + 1);
	if (a == OA_PROVEN_FALSE || b == OA_PROVEN_FALSE)
	  v = OA_PROVEN_FALSE;
	else if (a == OA_PROVEN_TRUE && b == OA_PROVEN_TRUE)
	  v = OA_PROVEN_TRUE;
	break;
      }
    case TRUTH_ORIF_EXPR:
    case TRUTH_OR_EXPR:
      {
	oa_proof_result a = eval_bool (s, fi, TREE_OPERAND (e, 0), depth + 1);
	oa_proof_result b = eval_bool (s, fi, TREE_OPERAND (e, 1), depth + 1);
	if (a == OA_PROVEN_TRUE || b == OA_PROVEN_TRUE)
	  v = OA_PROVEN_TRUE;
	else if (a == OA_PROVEN_FALSE && b == OA_PROVEN_FALSE)
	  v = OA_PROVEN_FALSE;
	break;
      }
    case LT_EXPR: case LE_EXPR: case GT_EXPR: case GE_EXPR:
    case EQ_EXPR: case NE_EXPR:
      v = compare_verdict (s, fi, TREE_OPERAND (e, 0), TREE_CODE (e),
			   TREE_OPERAND (e, 1));
      if (v == OA_UNKNOWN)
	v = atom_verdict (s, fi, e);
      break;
    case INTEGER_CST:
      v = integer_zerop (e) ? OA_PROVEN_FALSE : OA_PROVEN_TRUE;
      break;
    case CALL_EXPR:
      {
	tree arg;
	if (is_object_address_call_p (e, &arg))
	  {
	    v = oaddr_p (s, fi, arg, NULL) ? OA_PROVEN_TRUE : OA_UNKNOWN;
	    break;
	  }
	v = atom_verdict (s, fi, e);
	break;
      }
    case VAR_DECL:
    case PARM_DECL:
      if (TREE_TYPE (e) && POINTER_TYPE_P (TREE_TYPE (e)))
	v = nonzero_verdict (s, fi, e);
      else
	v = cmp_verdict (s, fi, e, NE_EXPR, 0);
      if (v == OA_UNKNOWN)
	v = atom_verdict (s, fi, e);
      break;
    default:
      if (TREE_TYPE (e) && INTEGRAL_TYPE_P (TREE_TYPE (e)))
	v = cmp_verdict (s, fi, e, NE_EXPR, 0);
      if (v == OA_UNKNOWN)
	v = atom_verdict (s, fi, e);
      break;
    }
  resolve_state = saved;
  return v;
}

/* Keys built from a contract's shapes.  */

/* The object pointer P points to, as a key.  */

static tree
deref (tree p)
{
  p = tree_strip_any_location_wrapper (p);
  tree type = TREE_TYPE (p);
  if (type && POINTER_TYPE_P (type))
    return build1 (INDIRECT_REF, TREE_TYPE (type), p);
  return p;
}

/* The call 'FN (RECEIVER)', a key.  */

static tree
call_key (tree fn, tree receiver)
{
  tree r = tree_strip_any_location_wrapper (receiver);
  if (r && !POINTER_TYPE_P (TREE_TYPE (r)) && !TYPE_REF_P (TREE_TYPE (r)))
    r = build_fold_addr_expr (r);
  return build_call_nary (TREE_TYPE (TREE_TYPE (fn)),
			  build_fold_addr_expr (fn), 1, r);
}

/* Range verdict: E within [LO, HI].  */

static oa_proof_result
in_range_verdict (state &s, int fi, tree e, bool has_lo, tree lo, bool has_hi,
		  tree hi, oa_unprovable_reason *why = NULL)
{
  oa_proof_result a = OA_PROVEN_TRUE, b = OA_PROVEN_TRUE;
  if (has_lo)
    a = (TREE_CODE (lo) == REAL_CST ? compare_verdict (s, fi, e, GE_EXPR, lo)
	 : cmp_verdict (s, fi, e, GE_EXPR, wi::to_widest (lo)));
  if (has_hi)
    b = (TREE_CODE (hi) == REAL_CST ? compare_verdict (s, fi, e, LE_EXPR, hi)
	 : cmp_verdict (s, fi, e, LE_EXPR, wi::to_widest (hi)));
  if (a == OA_PROVEN_FALSE || b == OA_PROVEN_FALSE)
    return OA_PROVEN_FALSE;
  if (a == OA_PROVEN_TRUE && b == OA_PROVEN_TRUE)
    return OA_PROVEN_TRUE;
  if (why)
    *why = OA_UNPROVABLE_NO_FACT;
  return OA_UNKNOWN;
}

/* The checks the plugins make, one per conjunct shape.  */

static oa_proof_result
check_comparison (state *s, bool conveyor, tree expr, tree_code code,
		  tree cst)
{
  return compare_verdict (*s, set_index (conveyor), expr, code, cst);
}

static bool
check_object_address (state *s, bool conveyor, tree expr,
		      oa_unprovable_reason *why)
{
  state *saved = resolve_state;
  resolve_state = s;
  bool ok = oaddr_p (*s, set_index (conveyor), expr, why);
  resolve_state = saved;
  return ok;
}

static oa_proof_result
check_nonzero (state *s, bool conveyor, tree expr, oa_unprovable_reason *why)
{
  state *saved = resolve_state;
  resolve_state = s;
  oa_proof_result v = nonzero_verdict (*s, set_index (conveyor), expr);
  resolve_state = saved;
  if (v == OA_UNKNOWN && why)
    *why = OA_UNPROVABLE_NO_FACT;
  return v;
}

static oa_proof_result
check_ne_constant (state *s, bool conveyor, tree expr, tree cst,
		   oa_unprovable_reason *why)
{
  oa_proof_result v = compare_verdict (*s, set_index (conveyor), expr, NE_EXPR,
				       cst);
  if (v == OA_UNKNOWN && why)
    *why = OA_UNPROVABLE_NO_FACT;
  return v;
}

static oa_proof_result
check_relational (state *s, bool conveyor, tree a, tree_code code, tree b)
{
  if (!a || !b)
    return OA_UNKNOWN;
  return compare_verdict (*s, set_index (conveyor), a, code, b);
}

/* 'A - OFFSET CODE RECEIVER.ACCESSOR ()'.  */

static oa_proof_result
check_call_relational (state *s, bool conveyor, tree a, tree_code code,
		       tree receiver, tree accessor,
		       const widest_int &offset = 0)
{
  if (!a || !receiver)
    return OA_UNKNOWN;
  tree k = call_key (accessor, receiver);
  /* A - OFFSET CODE K: A CODE K + OFFSET.  */
  return rel_verdict (*s, set_index (conveyor), a, code, k, offset);
}

static oa_proof_result
check_call_call_relational (state *s, bool conveyor, tree r1, tree acc1,
			    tree_code code, tree r2, tree acc2)
{
  if (!r1 || !r2)
    return OA_UNKNOWN;
  return rel_verdict (*s, set_index (conveyor), call_key (acc1, r1), code,
		      call_key (acc2, r2));
}

static oa_proof_result
check_predicate (state *s, bool conveyor, tree obj, tree pred_fn,
		 bool polarity)
{
  oa_proof_result v = OA_UNKNOWN;
  /* A by-value class argument copied from an object: the copy has none of
     its facts (an elided call result is the object itself).  */
  {
    tree so = tree_strip_any_location_wrapper (obj);
    if (so && TREE_CODE (so) == TARGET_EXPR)
      {
	tree init = tree_strip_any_location_wrapper (TARGET_EXPR_INITIAL (so));
	if (init && TREE_CODE (init) != CALL_EXPR
	    && TREE_CODE (init) != AGGR_INIT_EXPR
	    && TREE_CODE (init) != TARGET_EXPR)
	  return OA_UNKNOWN;
      }
  }
  if (DECL_IOBJ_MEMBER_FUNCTION_P (pred_fn))
    {
      /* 'obj->pred (obj)': the explicit argument is the object.  */
      state *saved = resolve_state;
      resolve_state = s;
      for (const atom_fact &x : s->f[set_index (conveyor)].atoms)
	{
	  tree e = strip (x.expr);
	  if (TREE_CODE (e) == CALL_EXPR && call_fn (e) == pred_fn
	      && call_expr_nargs (e) >= 2
	      && key_eq (CALL_EXPR_ARG (e, call_expr_nargs (e) - 1), obj))
	    {
	      v = x.polarity ? OA_PROVEN_TRUE : OA_PROVEN_FALSE;
	      break;
	    }
	}
      resolve_state = saved;
    }
  else
    {
      /* The predicate takes the object by address or reference.  */
      tree parm = TYPE_ARG_TYPES (TREE_TYPE (pred_fn));
      tree so = tree_strip_any_location_wrapper (obj);
      if (parm && POINTER_TYPE_P (TREE_VALUE (parm)) && TREE_TYPE (so)
	  && !POINTER_TYPE_P (TREE_TYPE (so)))
	obj = build1 (ADDR_EXPR, build_pointer_type (TREE_TYPE (so)), so);
      tree k = build_call_nary (TREE_TYPE (TREE_TYPE (pred_fn)),
				build_fold_addr_expr (pred_fn), 1, obj);
      v = atom_verdict (*s, set_index (conveyor), k);
    }
  return polarity ? v : invert (v);
}

static oa_proof_result
check_scalar_range (state *s, bool conveyor, tree expr, bool has_lo, tree lo,
		    bool has_hi, tree hi)
{
  return in_range_verdict (*s, set_index (conveyor), expr, has_lo, lo, has_hi,
			   hi);
}

static oa_proof_result
check_field_range (state *s, bool conveyor, tree base, tree field,
		   bool has_lo, tree lo, bool has_hi, tree hi)
{
  tree k = build3 (COMPONENT_REF, TREE_TYPE (field), deref (base), field,
		   NULL_TREE);
  return in_range_verdict (*s, set_index (conveyor), k, has_lo, lo, has_hi,
			   hi);
}

static oa_proof_result
check_call_range (state *s, bool conveyor, tree receiver, tree fn,
		  bool has_lo, tree lo, bool has_hi, tree hi)
{
  return in_range_verdict (*s, set_index (conveyor), call_key (fn, receiver),
			   has_lo, lo, has_hi, hi);
}

/* A substituted relation or predicate conjunct E.  */

static oa_proof_result
check_conjunct (state *s, bool conveyor, tree e)
{
  return eval_bool (*s, set_index (conveyor), e);
}

/* Callee CALLEE's conjunct COND with CALL's arguments for its
   parameters.  */

static tree
substitute_at_call (tree callee, tree call, tree cond)
{
  subst_map m;
  unsigned n = call_nargs (call);
  unsigned i = 0;
  tree parm = DECL_ARGUMENTS (callee);
  for (; parm && i < n; parm = DECL_CHAIN (parm), ++i)
    m.m.push_back ({ parm, call_arg (call, i) });
  if (parm)
    return NULL_TREE;
  return substitute (cond, m);
}

/* CALLEE's precondition conjunct PRED_FN (ARGS), a relation, possibly
   NEGATED, at CALL.  */

static oa_proof_result
check_call_relation (state *s, tree call, tree callee, tree pred_fn,
		     const vec<tree> &args, bool negated)
{
  vec<tree, va_gc> *v = NULL;
  for (tree a : args)
    {
      tree sa = substitute_at_call (callee, call, a);
      if (!sa)
	return OA_UNKNOWN;
      vec_safe_push (v, sa);
    }
  tree k = build_call_vec (TREE_TYPE (TREE_TYPE (pred_fn)),
			   build_fold_addr_expr (pred_fn), v);
  oa_proof_result r = atom_verdict (*s, set_index (false), k);
  if (dump_p)
    {
      fprintf (stderr, "apf: relation ");
      print_generic_expr (stderr, k);
      fprintf (stderr, " -> %d\n", (int) r);
    }
  return negated ? invert (r) : r;
}

} // namespace apf

#endif
