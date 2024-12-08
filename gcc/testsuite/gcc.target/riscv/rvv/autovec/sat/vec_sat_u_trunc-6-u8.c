/* { dg-do compile } */
/* { dg-options "-march=rv64gcv -mabi=lp64d -ftree-vectorize -fdump-rtl-expand-details" } */

#include "vec_sat_arith.h"

DEF_VEC_SAT_U_TRUNC_FMT_4 (uint8_t, uint64_t)

/* { dg-final { scan-rtl-dump-times ".SAT_TRUNC " 4 "expand" } } */
/* { dg-final { scan-assembler-times {vnclipu\.wi} 3 } } */
