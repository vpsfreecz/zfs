#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0

. $STF_SUITE/include/libtest.shlib

verify_runnable "global"

if ! is_linux; then
	log_unsupported "UID/GID dataset maps are a Linux-only feature"
fi

typeset dataset="$TESTPOOL/ugidmap"
typeset child="$dataset/child"
typeset mountpoint="$TESTDIR/ugidmap"
typeset valid_ten="0:100000:1,1:100001:1,2:100002:1,3:100003:1,4:100004:1,5:100005:1,6:100006:1,7:100007:1,8:100008:1,9:100009:1"
typeset cap_prefix="0x0100000301000000000000000000000000000000"
typeset invalid

function check_filecap
{
	typeset file="$1"
	typeset root="$2"

	log_must eval "getfattr -n security.capability -e hex \"$file\" " \
	    "2>/dev/null | grep -qx 'security.capability=$cap_prefix$root'"
}

function check_mapped_filecap
{
	typeset fs="$1"
	typeset mnt="$2"
	typeset file="$mnt/filecap"

	log_must zfs mount "$fs"
	log_must touch "$file"
	log_must setfattr -n security.capability \
	    -v "${cap_prefix}a0860100" "$file"
	check_filecap "$file" a0860100

	log_must zfs unmount "$fs"
	log_must zfs set uidmap=0:200000:1000 "$fs"
	log_must zfs set gidmap=0:200000:1000 "$fs"
	log_must zfs mount "$fs"
	check_filecap "$file" 400d0300

	log_must setfattr -x security.capability "$file"
	log_mustnot getfattr -n security.capability "$file"
	log_must zfs unmount "$fs"
	log_must zfs set uidmap=0:300000:1000 "$fs"
	log_must zfs set gidmap=0:300000:1000 "$fs"
	log_must zfs mount "$fs"
	log_mustnot getfattr -n security.capability "$file"
	log_must zfs unmount "$fs"
}

function cleanup
{
	datasetexists "$dataset" && destroy_dataset "$dataset" -Rf
}

log_assert "UID/GID maps use one exact, unambiguous checked grammar"
log_onexit cleanup

log_must zfs create -u -o acltype=posix -o mountpoint="$mountpoint" \
	"$dataset"
log_must zfs set uidmap="$valid_ten" "$dataset"
log_must test "$(get_prop uidmap "$dataset")" = "$valid_ten"
log_must zfs set uidmap=0:0:1 "$dataset"

for invalid in \
	"0:100000:1,1:100001:1,2:100002:1,3:100003:1,4:100004:1,5:100005:1,6:100006:1,7:100007:1,8:100008:1,9:100009:1,10:100010:1" \
	"0:100000:1," \
	"0:100000:1,garbage" \
	"0:100000:1x" \
	"18446744073709551616:100000:1" \
	"18446744073709551615:100000:2" \
	"4294967295:0:1" \
	"0:4294967295:1" \
	"4294967294:0:2" \
	"0:100000:10,5:200000:10" \
	"0:100000:10,20:100005:10"; do
	log_mustnot zfs set uidmap="$invalid" "$dataset"
done

log_must zfs set uidmap=4294967294:0:1 "$dataset"
log_must zfs set uidmap=0:500000:1048576 "$dataset"
log_must zfs set uidmap=0:50000:1048576 "$dataset"
log_must zfs set gidmap=0:50000:1048576 "$dataset"
log_must zfs mount "$dataset"
log_must chmod 0777 "$mountpoint"
log_must user_ns_exec touch "$mountpoint/overlap"
log_must test "$(stat -c %u:%g "$mountpoint/overlap")" = \
	"100000:100000"
log_must rm "$mountpoint/overlap"
log_must chmod 0755 "$mountpoint"
log_must zfs unmount "$dataset"

log_must zfs set uidmap=0:100000:1000 "$dataset"
log_must zfs set gidmap=0:100000:1000 "$dataset"
log_mustnot zfs set gidmap="0:100000:1," "$dataset"
log_must zfs mount "$dataset"

# Host root must remain able to manage namespace-root objects.  In
# particular, libzfs has to create a child mountpoint below the mapped parent.
log_must mkdir "$mountpoint/host-root"
log_must chown 0:0 "$mountpoint/host-root"
log_must test "$(stat -c %u:%g "$mountpoint/host-root")" = \
	"100000:100000"
log_must zfs create -u -o canmount=noauto -o uidmap=0:100000:1000 \
	-o gidmap=0:100000:1000 "$child"
log_must zfs mount "$child"
log_must touch "$mountpoint/child/host-root-file"
log_must test "$(stat -c %u:%g \
	"$mountpoint/child/host-root-file")" = "100000:100000"
log_must zfs unmount "$child"

log_must user_ns_exec touch "$mountpoint/file"
log_must user_ns_exec chown 999:999 "$mountpoint/file"
log_mustnot user_ns_exec chown 5000:5000 "$mountpoint/file"
log_must user_ns_exec setfacl -m u:999:r-- "$mountpoint/file"
log_must eval "user_ns_exec getfacl -n \"$mountpoint/file\" 2>/dev/null | " \
	"grep -qx 'user:999:r--'"
log_mustnot user_ns_exec setfacl -m u:5000:r-- "$mountpoint/file"

# A v3 capability written in the current host-ID range is stored with an
# internal namespace-domain binding.  Reads follow later map changes without
# changing the ordinary on-disk capability ABI.  Removal leaves a tombstone,
# so neither xattr backend can resurrect the old capability.
log_must zfs set xattr=sa "$child"
check_mapped_filecap "$child" "$mountpoint/child"

log_must zfs unmount "$dataset"
log_must zfs set xattr=on "$dataset"
check_mapped_filecap "$dataset" "$mountpoint"

log_pass "UID/GID map validation and unmapped-ID failures are consistent"
