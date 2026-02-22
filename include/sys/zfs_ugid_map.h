// SPDX-License-Identifier: CDDL-1.0

#ifndef	_SYS_FS_ZFS_UGID_MAP_H
#define	_SYS_FS_ZFS_UGID_MAP_H

#include <sys/types.h>

#define	ZFS_UGID_MAP_SIZE	10
#define	ZFS_UGID_MAP_MAX_ID	((uint64_t)UINT32_MAX - 1)

struct zfs_ugid_map_entry {
	uint64_t		e_ns_id;
	uint64_t		e_host_id;
	uint64_t		e_count;
};

int zfs_ugid_map_parse(const char *value,
    struct zfs_ugid_map_entry entries[ZFS_UGID_MAP_SIZE],
    uint64_t *entry_count);

#if defined(_KERNEL) && defined(__linux__)

#include <sys/dmu.h>
#include <sys/fs/zfs.h>
#include <linux/posix_acl_xattr.h>

struct zfs_ugid_map {
	struct zfs_ugid_map_entry **m_map;
	uint64_t		m_size;
	uint64_t		m_entries;
};

int zfs_create_ugid_map(objset_t *os, zfs_prop_t prop,
    struct zfs_ugid_map **ugid_mapp);

void
zfs_free_ugid_map(struct zfs_ugid_map *ugid_map);

int zfs_ugid_map_ns_to_host(struct zfs_ugid_map *ugid_map, uint64_t id,
    uint64_t *mapped_id);

int zfs_ugid_map_host_to_ns(struct zfs_ugid_map *ugid_map, uint64_t id,
    uint64_t *mapped_id);

int zfs_ugid_map_host_to_ns_strict(struct zfs_ugid_map *ugid_map, uint64_t id,
    uint64_t *mapped_id);

struct posix_acl *
zfs_ugid_map_acl_from_xattr(
	struct zfs_ugid_map *uid_map,
	struct zfs_ugid_map *gid_map,
	struct posix_acl *acl);

int
zfs_ugid_map_acl_to_xattr(
	struct zfs_ugid_map *uid_map,
	struct zfs_ugid_map *gid_map,
	struct posix_acl *acl,
	void *value,
	int size);

#endif /* defined(_KERNEL) && defined(__linux__) */

#endif	/* _SYS_FS_ZFS_UGID_MAP_H */
