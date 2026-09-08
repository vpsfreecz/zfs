#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0

. $STF_SUITE/include/libtest.shlib

verify_runnable "global"
command -v mmap_write_truncate >/dev/null ||
    log_unsupported "Requires Linux userfaultfd test helper"

function cleanup
{
	rm -f "$TESTDIR/truncate-source" "$TESTDIR/truncate-destination"
}
log_onexit cleanup

log_assert "An invalidated mmap write source cannot strand its range lock"
mmap_write_truncate "$TESTDIR/truncate-source" "$TESTDIR/truncate-destination"
status=$?
[[ $status -eq 77 ]] && log_unsupported "Kernel userfaultfd unavailable"
[[ $status -eq 0 ]] || log_fail "mmap write/truncate regression: $status"
log_pass "Invalidated sources preserve completed data and release locks"
