/* { dg-do compile } */
/* { dg-options "-march=rv64gc -mabi=lp64d -fdump-rtl-expand-details" } */

#include "sat_arith.h"

DEF_SAT_U_ADD_IMM_TYPE_CHECK_FMT_1(uint32_t, 2147483644u)

/* { dg-final { scan-rtl-dump-times ".SAT_ADD " 2 "expand" } } */
