#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or https://opensource.org/licenses/CDDL-1.0.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
#

#
# Copyright (c) 2017, Lawrence Livermore National Security, LLC.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/include/math.shlib

#
# DESCRIPTION:
# Ensure stats presented in the dbufstats kstat are correct based on the
# dbufs kstat.
#
# STRATEGY:
# 1. Generate a file with random data in it
# 2. Store output from dbufs kstat
# 3. Store output from dbufstats kstat
# 4. Compare the regular and metadata cache totals with dbufstat output
# 5. Verify the regular cache level counters sum to the cache total
# 6. Compare the hash total with dbufstat output
#

DBUFSTATS_BEFORE_FILE=$(mktemp -t dbufstats.before.XXXXXX)
DBUFSTATS_AFTER_FILE=$(mktemp -t dbufstats.after.XXXXXX)
DBUFS_FILE=$(mktemp -t dbufs.out.XXXXXX)
DBUFSTATS_RETRIES=5
DBUFSTATS_TOLERANCE=15

function cleanup
{
	log_must rm -f $TESTDIR/file $DBUFS_FILE \
	    $DBUFSTATS_BEFORE_FILE $DBUFSTATS_AFTER_FILE
}

function sumstats # file stat_name_regex
{
	awk -v pattern="$2" \
	    '$1 ~ pattern { total += $2 } END { print total + 0 }' "$1"
}

function comparestats # stat_name before after observed
{
	typeset name=$1
	typeset from_before=$2
	typeset from_after=$3
	typeset observed=$4

	if within_tolerance $from_before $from_after $DBUFSTATS_TOLERANCE && \
	    within_tolerance $from_before $observed $DBUFSTATS_TOLERANCE && \
	    within_tolerance $from_after $observed $DBUFSTATS_TOLERANCE; then
		return 0
	fi

	log_note "$name changed during capture: " \
	    "before=$from_before observed=$observed after=$from_after"
	return 1
}

function testdbufstat # stat_name stat_name_regex dbufstat_filter
{
	typeset name=$1
	typeset filter=""
	typeset from_before from_after from_dbufs

	[[ -n "$3" ]] && filter="-F $3"

	from_before=$(sumstats "$DBUFSTATS_BEFORE_FILE" "$2")
	from_after=$(sumstats "$DBUFSTATS_AFTER_FILE" "$2")
	from_dbufs=$(dbufstat -bxn -i "$DBUFS_FILE" "$filter" | wc -l)

	comparestats "$name" "$from_before" "$from_after" "$from_dbufs"
}

function testcachelevels
{
	typeset cache_before cache_after levels_before levels_after

	cache_before=$(sumstats "$DBUFSTATS_BEFORE_FILE" '^cache_count$')
	cache_after=$(sumstats "$DBUFSTATS_AFTER_FILE" '^cache_count$')
	levels_before=$(sumstats "$DBUFSTATS_BEFORE_FILE" \
	    '^cache_level_[0-9]+$')
	levels_after=$(sumstats "$DBUFSTATS_AFTER_FILE" \
	    '^cache_level_[0-9]+$')

	comparestats "cache level total" "$cache_before" "$cache_after" \
	    "$levels_before" || return 1
	comparestats "cache level sample" "$levels_before" "$levels_after" \
	    "$cache_after"
}

function verify_dbufstats
{
	testdbufstat "cached dbufs" \
	    '^(cache_count|metadata_cache_count)$' "dbc=1" || return 1
	testcachelevels || return 1
	testdbufstat "hash elements" '^hash_elements$' "" || return 1

	return 0
}

verify_runnable "both"

log_assert "dbufstats produces correct statistics"

log_onexit cleanup

log_must file_write -o create -f "$TESTDIR/file" -b 1048576 -c 20 -d R
sync_all_pools

typeset -i attempt=1
while (( attempt <= DBUFSTATS_RETRIES )); do
	log_must eval "kstat -g dbufstats > $DBUFSTATS_BEFORE_FILE"
	log_must eval "kstat dbufs > $DBUFS_FILE"
	log_must eval "kstat -g dbufstats > $DBUFSTATS_AFTER_FILE"

	verify_dbufstats && break

	if (( attempt == DBUFSTATS_RETRIES )); then
		log_fail "Unable to capture coherent dbuf statistics after " \
		    "$DBUFSTATS_RETRIES attempts"
	fi

	log_note "Dbuf statistics changed during sample $attempt; retrying"
	((attempt += 1))
done

log_pass "dbufstats produces correct statistics passed"
