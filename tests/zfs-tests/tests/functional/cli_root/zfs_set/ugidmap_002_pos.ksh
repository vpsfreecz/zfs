#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0

. $STF_SUITE/include/libtest.shlib

verify_runnable "global"

if ! is_linux; then
	log_unsupported "UID/GID dataset maps are a Linux-only feature"
fi

typeset base="$TESTPOOL/ugidmap-live"
typeset direct="$base/direct"
typeset parent="$base/parent"
typeset child="$parent/child"
typeset override="$parent/override"
typeset snapshot_parent="$base/snapshot-parent"
typeset snapshot_head="$snapshot_parent/fs"
typeset snapshot="$snapshot_head@mounted"
typeset source="$base/source"
typeset received="$base/received"
typeset direct_mnt="$TESTDIR/ugidmap-live-direct"
typeset child_mnt="$TESTDIR/ugidmap-live-child"
typeset override_mnt="$TESTDIR/ugidmap-live-override"
typeset snapshot_mnt="$TESTDIR/ugidmap-live-snapshot"
typeset received_mnt="$TESTDIR/ugidmap-live-received"
typeset map_a="0:100000:1000"
typeset map_b="0:200000:1000"
typeset map_c="0:300000:1000"

function cleanup
{
	if ismounted "$snapshot"; then
		log_must umount "$snapshot_mnt"
	fi
	[[ -d "$snapshot_mnt" ]] && log_must rmdir "$snapshot_mnt"
	datasetexists "$base" && destroy_dataset "$base" -Rf
}

function check_map
{
	typeset dataset="$1"
	typeset property="$2"
	typeset expected="$3"

	log_must test "$(get_prop "$property" "$dataset")" = "$expected"
}

function check_received_map
{
	typeset dataset="$1"
	typeset property="$2"
	typeset expected="$3"
	typeset value

	value=$(zfs get -H -o received "$property" "$dataset") || \
	    log_fail "cannot read received $property on $dataset"
	log_must test "$value" = "$expected"
}

log_assert "effective UID/GID maps cannot change under a mounted filesystem"
log_onexit cleanup

log_must zfs create -u "$base"

# Direct set and inherit must not stale a mounted target's cached tables.
log_must zfs create -u -o mountpoint="$direct_mnt" \
	-o uidmap="$map_a" -o gidmap="$map_a" "$direct"
log_must zfs mount "$direct"
log_mustnot zfs set uidmap="$map_b" "$direct"
log_mustnot zfs set gidmap="$map_b" "$direct"
log_mustnot zfs inherit uidmap "$direct"
log_mustnot zfs inherit gidmap "$direct"
check_map "$direct" uidmap "$map_a"
check_map "$direct" gidmap "$map_a"
log_must zfs unmount "$direct"

# An ancestor update is equally unsafe while an inheriting child is mounted.
log_must zfs create -u -o uidmap="$map_a" -o gidmap="$map_a" "$parent"
log_must zfs create -u -o mountpoint="$child_mnt" "$child"
log_must zfs mount "$child"
log_must zfs set uidmap="$map_a" "$parent"
log_must zfs set gidmap="$map_a" "$parent"
log_mustnot zfs set uidmap="$map_b" "$parent"
log_mustnot zfs set gidmap="$map_b" "$parent"
log_mustnot zfs inherit uidmap "$parent"
log_mustnot zfs inherit gidmap "$parent"
check_map "$parent" uidmap "$map_a"
check_map "$parent" gidmap "$map_a"
check_map "$child" uidmap "$map_a"
check_map "$child" gidmap "$map_a"
log_must zfs unmount "$child"

# A local override shields this mounted branch and must not block its parent.
log_must zfs create -u -o mountpoint="$override_mnt" \
	-o uidmap="$map_b" -o gidmap="$map_b" "$override"
log_must zfs mount "$override"
log_must zfs set uidmap="$map_c" "$parent"
log_must zfs set gidmap="$map_c" "$parent"
check_map "$override" uidmap "$map_b"
check_map "$override" gidmap "$map_b"

# A directly mounted snapshot caches inherited maps even when its head is
# unmounted.  Changes on either the head or an ancestor must be rejected.
log_must zfs create -u -o uidmap="$map_a" -o gidmap="$map_a" \
	"$snapshot_parent"
log_must zfs create -u "$snapshot_head"
log_must zfs snapshot "$snapshot"
log_must mkdir -p "$snapshot_mnt"
log_must mount -t zfs "$snapshot" "$snapshot_mnt"
log_must zfs set uidmap="$map_a" "$snapshot_parent"
log_must zfs set gidmap="$map_a" "$snapshot_parent"
log_mustnot zfs set uidmap="$map_b" "$snapshot_head"
log_mustnot zfs set gidmap="$map_b" "$snapshot_head"
log_mustnot zfs set uidmap="$map_b" "$snapshot_parent"
log_mustnot zfs set gidmap="$map_b" "$snapshot_parent"
log_mustnot zfs inherit uidmap "$snapshot_parent"
log_mustnot zfs inherit gidmap "$snapshot_parent"
check_map "$snapshot_head" uidmap "$map_a"
check_map "$snapshot_head" gidmap "$map_a"
log_must test "$(stat -c %u:%g "$snapshot_mnt")" = "100000:100000"
log_must umount "$snapshot_mnt"
log_must rmdir "$snapshot_mnt"

# Establish received values, then verify that inherit -S cannot expose them
# under a mounted target with different local maps.
log_must zfs create -u -o uidmap="$map_a" -o gidmap="$map_a" "$source"
log_must zfs snapshot "$source@withmaps"
log_must eval "zfs send -p '$source@withmaps' | " \
	"zfs receive -u -o mountpoint='$received_mnt' '$received'"
check_received_map "$received" uidmap "$map_a"
check_received_map "$received" gidmap "$map_a"
log_must zfs set uidmap="$map_b" "$received"
log_must zfs set gidmap="$map_b" "$received"
log_must zfs mount "$received"
log_mustnot zfs inherit -S uidmap "$received"
log_mustnot zfs inherit -S gidmap "$received"
check_map "$received" uidmap "$map_b"
check_map "$received" gidmap "$map_b"
check_received_map "$received" uidmap "$map_a"
check_received_map "$received" gidmap "$map_a"

# A received-value replacement is safe behind the mounted local override.
log_must zfs set uidmap="$map_c" "$source"
log_must zfs set gidmap="$map_c" "$source"
log_must zfs snapshot "$source@changedmaps"
log_must eval "zfs send -p -i '$source@withmaps' " \
	"'$source@changedmaps' | zfs receive -F '$received'"
check_map "$received" uidmap "$map_b"
check_map "$received" gidmap "$map_b"
check_received_map "$received" uidmap "$map_c"
check_received_map "$received" gidmap "$map_c"
log_must test "$(get_prop mounted "$received")" = "yes"

# Make the received maps effective, then attempt to replace them while the
# dataset remains mounted.  The stream may be received, but the effective and
# received map values must remain unchanged.
log_must zfs unmount "$received"
log_must zfs inherit -S uidmap "$received"
log_must zfs inherit -S gidmap "$received"
log_must zfs mount "$received"
log_must zfs set uidmap="$map_a" "$source"
log_must zfs set gidmap="$map_a" "$source"
log_must zfs snapshot "$source@replacement"
log_must eval "zfs send -p -i '$source@changedmaps' " \
	"'$source@replacement' | zfs receive -F '$received'"
check_map "$received" uidmap "$map_c"
check_map "$received" gidmap "$map_c"
check_received_map "$received" uidmap "$map_c"
check_received_map "$received" gidmap "$map_c"

# Clearing received maps is the same effective-map change and must also be
# rejected while the receiver remains mounted.
log_must zfs inherit uidmap "$source"
log_must zfs inherit gidmap "$source"
log_must zfs snapshot "$source@withoutmaps"
log_must eval "zfs send -p -i '$source@replacement' " \
	"'$source@withoutmaps' | zfs receive -F '$received'"
check_map "$received" uidmap "$map_c"
check_map "$received" gidmap "$map_c"
check_received_map "$received" uidmap "$map_c"
check_received_map "$received" gidmap "$map_c"
log_must test "$(get_prop mounted "$received")" = "yes"

log_pass "mounted UID/GID map tables remain consistent with properties"
