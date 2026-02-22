#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0

. $STF_SUITE/include/libtest.shlib

verify_runnable "global"

if ! is_linux; then
	log_unsupported "UID/GID dataset maps are a Linux-only feature"
fi

typeset base="$TESTPOOL/ugidmap-resume"
typeset source="$base/source"
typeset target="$base/target"
typeset source_mnt="$TESTDIR/ugidmap-resume-source"
typeset target_mnt="$TESTDIR/ugidmap-resume-target"
typeset map="0:100000:1000"
typeset previous="base"
typeset -i i

function cleanup
{
	datasetexists "$base" && destroy_dataset "$base" -Rf
	[[ -d "$source_mnt" ]] && log_must rmdir "$source_mnt"
	[[ -d "$target_mnt" ]] && log_must rmdir "$target_mnt"
}

function exercise_map
{
	typeset tag="$1"
	typeset file="$target_mnt/mapped-$tag"

	log_must user_ns_exec touch "$file"
	log_must user_ns_exec chown 999:999 "$file"
	log_must eval "user_ns_exec stat -c '%u:%g' '$file' | " \
	    "grep -qx '999:999'"
	log_must user_ns_exec setfacl -m u:999:r-- "$file"
	log_must eval "user_ns_exec getfacl -n '$file' 2>/dev/null | " \
	    "grep -qx 'user:999:r--'"
}

log_assert "configured UID/GID maps survive repeated online resume cycles"
log_onexit cleanup

log_must zfs create -u "$base"
log_must zfs create -u -o acltype=posix -o mountpoint="$source_mnt" \
	-o uidmap="$map" -o gidmap="$map" "$source"
log_must zfs mount "$source"
log_must user_ns_exec touch "$source_mnt/payload-base"
log_must zfs snapshot "$source@base"
log_must eval "zfs send -p '$source@base' | " \
	"zfs receive -u -o mountpoint='$target_mnt' '$target'"

# Keep local maps effective so received-property refreshes remain safe while
# each online receive suspends and resumes the mounted target.
log_must zfs set uidmap="$map" "$target"
log_must zfs set gidmap="$map" "$target"
log_must zfs mount "$target"
exercise_map initial

i=1
while ((i <= 4)); do
	log_must user_ns_exec touch "$source_mnt/payload-$i"
	log_must zfs snapshot "$source@receive-$i"
	log_must eval "zfs send -p -i '$source@$previous' " \
	    "'$source@receive-$i' | zfs receive -F '$target'"
	log_must test "$(get_prop mounted "$target")" = "yes"
	log_must test "$(get_prop uidmap "$target")" = "$map"
	log_must test "$(get_prop gidmap "$target")" = "$map"
	exercise_map "receive-$i"
	previous="receive-$i"
	((i = i + 1))
done

# Rollback uses the same suspend/resume path.  Repeat it and exercise mapped
# inode and ACL conversions after every table replacement.
i=1
while ((i <= 4)); do
	log_must zfs snapshot "$target@rollback-$i"
	log_must user_ns_exec touch "$target_mnt/remove-$i"
	log_must zfs rollback "$target@rollback-$i"
	log_mustnot test -e "$target_mnt/remove-$i"
	log_must test "$(get_prop mounted "$target")" = "yes"
	exercise_map "rollback-$i"
	((i = i + 1))
done

log_pass "UID/GID map replacement is stable across repeated resumes"
