(set-logic QF_UFLIA)

; obligation: call to consume at d4324-conveyor-proof-predicate-bad.C:42
(push 1)
(declare-const check_it Bool)
(assert (not check_it))
(assert check_it)
(check-sat)
; expect: unsat
(pop 1)

; obligation: call to consume at d4324-conveyor-proof-predicate-bad.C:42
(push 1)
(declare-const check_it Bool)
(assert (not check_it))
(assert check_it)
(check-sat)
; expect: unsat
(pop 1)

; obligation: call to consume at d4324-conveyor-proof-predicate-bad.C:42
(push 1)
(declare-const check_it Bool)
(assert (not check_it))
(assert check_it)
(check-sat)
; expect: unsat
(pop 1)

