#ifndef	_SYS_FS_ZFS_UGID_MAP_H
#define	_SYS_FS_ZFS_UGID_MAP_H

#define ZFS_UGID_MAP_SIZE 10

struct zfs_ugid_map_entry {
	uint64_t        e_ns_id;
	uint64_t        e_host_id;
	uint64_t        e_count;
};

struct zfs_ugid_map {
	struct zfs_ugid_map_entry **m_map;
	uint64_t        m_size;
	uint64_t        m_entries;
};

struct zfs_ugid_map* zfs_create_ugid_map(objset_t *os, zfs_prop_t prop);
void zfs_free_ugid_map(struct zfs_ugid_map *ugid_map);
uint64_t zfs_ugid_map_ns_to_host(struct zfs_ugid_map *ugid_map, uint64_t id);
uint64_t zfs_ugid_map_host_to_ns(struct zfs_ugid_map *ugid_map, uint64_t id);

#endif	/* _SYS_FS_ZFS_UGID_MAP_H */
