#!/bin/ksh -p

. $STF_SUITE/include/libtest.shlib

DISK=${DISKS%% *}
create_pool $TESTPOOL "$DISK"

log_must zfs create $TESTPOOL/$TESTFS
log_must zfs create $TESTPOOL/$TESTFS/both
log_must zfs create $TESTPOOL/$TESTFS/both/child
log_must zfs create $TESTPOOL/$TESTFS/uid
log_must zfs create $TESTPOOL/$TESTFS/uid/child
log_must zfs create $TESTPOOL/$TESTFS/gid
log_must zfs create $TESTPOOL/$TESTFS/gid/child
log_must zfs create $TESTPOOL/$TESTFS/multimap

ZFS_USER=zfsugidmap
TEST_UID=100000
TEST_GID=200000

log_must groupadd -g $TEST_GID $ZFS_USER
log_must useradd -c "ZFS UID/GID Mapping Test User" -u $TEST_UID -g $TEST_GID $ZFS_USER

echo $ZFS_USER > /tmp/zfs-ugid-map-test-user.txt
echo $TEST_UID > /tmp/zfs-ugid-map-test-uid.txt
echo $TEST_GID > /tmp/zfs-ugid-map-test-gid.txt

log_pass
