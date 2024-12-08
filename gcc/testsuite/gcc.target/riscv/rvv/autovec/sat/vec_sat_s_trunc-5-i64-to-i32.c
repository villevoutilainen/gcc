/* { dg-do compile } */
/* { dg-options "-march=rv64gcv -mabi=lp64d -fdump-rtl-expand-details" } */

#include "vec_sat_arith.h"

DEF_VEC_SAT_S_TRUNC_FMT_5(int32_t, int64_t, INT32_MIN, INT32_MAX)

/* { dg-final { scan-rtl-dump-times ".SAT_TRUNC " 2 "expand" { target { any-opts
     "-O3"
   } } } } */
/* { dg-final { scan-rtl-dump-times ".SAT_TRUNC " 4 "expand" { target { any-opts
     "-O2"
   } } } } */
/* { dg-final { scan-assembler-times {vnclip\.wi} 1 } } */
