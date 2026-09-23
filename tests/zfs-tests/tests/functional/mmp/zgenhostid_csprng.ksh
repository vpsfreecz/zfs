#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# Copyright (c) 2026 vpsAdminOS contributors. All rights reserved.

. $STF_SUITE/include/libtest.shlib

verify_runnable "global"

typeset -r PREFIX=$TEST_BASE_DIR/zgenhostid-csprng

function cleanup
{
	rm -f $PREFIX.*
}

log_onexit cleanup

log_assert "Generated hostids use the full 32-bit range; explicit IDs stay fixed"

# The old lrand48() branch can only return 31 bits. Each generated output
# is a native-endian uint32_t; od -tu4 reads that value without byte-order
# assumptions. Sampling 64 times makes the chance of a false result from a
# working CSPRNG less than one in 2^64, while the old branch always fails.
typeset -i high=0
typeset -i i=0
while ((i < 64)); do
	log_must zgenhostid -o $PREFIX.$i
	log_must test "$(wc -c < $PREFIX.$i)" -eq 4
	typeset -i id=$(od -An -tu4 $PREFIX.$i)
	((id != 0)) || log_fail "generated zero hostid"
	((id >= 2147483648)) && high=1
	((i = i + 1))
done
((high == 1)) || log_fail "no generated hostid used the high bit"

log_must zgenhostid -o $PREFIX.explicit 89abcdef
typeset -i explicit=$(od -An -tu4 $PREFIX.explicit)
((explicit == 2309737967)) || log_fail "explicit hostid changed"

log_pass "Generated and explicitly selected hostid contracts preserved"
