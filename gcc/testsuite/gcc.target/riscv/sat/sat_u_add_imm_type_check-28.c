/* { dg-do compile } */
/* { dg-options "-march=rv64gc -mabi=lp64d -fdump-rtl-expand-details" } */

#include "sat_arith.h"

DEF_SAT_U_ADD_IMM_TYPE_CHECK_FMT_2(uint8_t, 126u)

/* { dg-final { scan-rtl-dump-times ".SAT_ADD " 2 "expand" } } */
