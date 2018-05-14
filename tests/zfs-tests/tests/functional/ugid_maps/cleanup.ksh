#!/bin/ksh -p
. $STF_SUITE/include/libtest.shlib

log_must destroy_pool $TESTPOOL
log_must userdel zfsugidmap
log_pass
