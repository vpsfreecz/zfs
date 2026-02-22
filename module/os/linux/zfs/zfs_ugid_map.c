// SPDX-License-Identifier: CDDL-1.0

#include <sys/types.h>
#include <sys/fs/zfs.h>
#include <sys/dsl_prop.h>
#include <sys/dsl_dataset.h>
#include <sys/zap.h>
#include <sys/dmu_objset.h>
#include <sys/zfs_ugid_map.h>
#include <linux/vfs_compat.h>

int
zfs_create_ugid_map(objset_t *os, zfs_prop_t prop,
    struct zfs_ugid_map **ugid_mapp)
{
	char *value = kmem_alloc(ZAP_MAXVALUELEN, KM_SLEEP);
	char source[ZFS_MAX_DATASET_NAME_LEN] =
	    "Internal error - setpoint not determined";
	struct zfs_ugid_map_entry entries[ZFS_UGID_MAP_SIZE];
	uint64_t entry_count = 0;
	int error;
	boolean_t config_held = dsl_pool_config_held(dmu_objset_pool(os));
	struct zfs_ugid_map *ugid_map = NULL;
	struct zfs_ugid_map_entry *entry;

	*ugid_mapp = NULL;

	/*
	 * dsl_sync_task() callbacks run with dp_config_rwlock held as writer.
	 * Re-entering as reader from this context can block in rrwlock and
	 * deadlock txg_sync (e.g. userquota property reads).
	 */
	if (!config_held)
		dsl_pool_config_enter(dmu_objset_pool(os), FTAG);

	error = dsl_prop_get_ds(os->os_dsl_dataset, zfs_prop_to_name(prop), 1,
	    ZAP_MAXVALUELEN, value, source);

	if (!config_held)
		dsl_pool_config_exit(dmu_objset_pool(os), FTAG);

	if (error != 0)
		goto out;

	error = zfs_ugid_map_parse(value, entries, &entry_count);
	if (error != 0 || entry_count == 0)
		goto out;

	ugid_map = vmem_zalloc(sizeof (struct zfs_ugid_map), KM_SLEEP);
	ugid_map->m_size = ZFS_UGID_MAP_SIZE;
	ugid_map->m_entries = 0;
	ugid_map->m_map = vmem_zalloc(
	    sizeof (struct zfs_ugid_map_entry *) * ugid_map->m_size, KM_SLEEP);

	for (uint64_t i = 0; i < entry_count; i++) {
		entry = vmem_zalloc(sizeof (struct zfs_ugid_map_entry),
		    KM_SLEEP);
		*entry = entries[i];

		ugid_map->m_map[ugid_map->m_entries] = entry;
		ugid_map->m_entries += 1;
	}

	*ugid_mapp = ugid_map;

out:
	kmem_free(value, ZAP_MAXVALUELEN);
	return (error);
}

void
zfs_free_ugid_map(struct zfs_ugid_map *ugid_map)
{
	int i;

	if (ugid_map == NULL)
		return;

	for (i = 0; i < ugid_map->m_entries; i++) {
		vmem_free(ugid_map->m_map[i],
		    sizeof (struct zfs_ugid_map_entry));
	}

	vmem_free(ugid_map->m_map,
	    sizeof (struct zfs_ugid_map_entry *) * ugid_map->m_size);
	vmem_free(ugid_map, sizeof (struct zfs_ugid_map));
}

int
zfs_ugid_map_ns_to_host(struct zfs_ugid_map *ugid_map, uint64_t id,
    uint64_t *mapped_id)
{
	int i;
	struct zfs_ugid_map_entry *entry;

	if (ugid_map == NULL) {
		*mapped_id = id;
		return (0);
	}

	/* Look for a matching mapping. */
	for (i = 0; i < ugid_map->m_entries; i++) {
		entry = ugid_map->m_map[i];

		/* Check if we can map the entry. */
		if (id >= entry->e_ns_id &&
		    id - entry->e_ns_id < entry->e_count) {
			*mapped_id = entry->e_host_id + (id - entry->e_ns_id);
			return (0);
		}
	}

	return (SET_ERROR(EOVERFLOW));
}

int
zfs_ugid_map_host_to_ns_strict(struct zfs_ugid_map *ugid_map, uint64_t id,
    uint64_t *mapped_id)
{
	int i;
	struct zfs_ugid_map_entry *entry;

	if (ugid_map == NULL) {
		*mapped_id = id;
		return (0);
	}

	/* Look for a matching mapping. */
	for (i = 0; i < ugid_map->m_entries; i++) {
		entry = ugid_map->m_map[i];

		/* Check if we can map the entry. */
		if (id >= entry->e_host_id &&
		    id - entry->e_host_id < entry->e_count) {
			*mapped_id = (id - entry->e_host_id) + entry->e_ns_id;
			return (0);
		}
	}

	return (SET_ERROR(EOVERFLOW));
}

int
zfs_ugid_map_host_to_ns(struct zfs_ugid_map *ugid_map, uint64_t id,
    uint64_t *mapped_id)
{
	int error;
	int i;
	struct zfs_ugid_map_entry *entry;

	error = zfs_ugid_map_host_to_ns_strict(ugid_map, id, mapped_id);
	if (error == 0 || ugid_map == NULL)
		return (error);

	/*
	 * Initial-user-namespace root manages mapped filesystems directly.
	 * Keep the host-domain lookup authoritative above, then allow its
	 * otherwise unmapped ID 0 to select namespace root when the map
	 * contains it.  This is needed by host chown(2) and by libzfs creating
	 * mountpoints below a mapped dataset.  Do not extend the exception to
	 * other namespace IDs: those must arrive through mapped host IDs.
	 */
	if (id == 0) {
		for (i = 0; i < ugid_map->m_entries; i++) {
			entry = ugid_map->m_map[i];

			if (entry->e_ns_id == 0) {
				*mapped_id = 0;
				return (0);
			}
		}
	}

	return (SET_ERROR(EOVERFLOW));
}

struct posix_acl *
zfs_ugid_map_acl_from_xattr(struct zfs_ugid_map *uid_map,
    struct zfs_ugid_map *gid_map, struct posix_acl *acl)
{
	struct posix_acl_entry *pa, *pe;
	uint64_t id;
	int error;

	if (acl == NULL || IS_ERR(acl))
		return (acl);

	if (uid_map == NULL && gid_map == NULL)
		return (acl);

	FOREACH_ACL_ENTRY(pa, acl, pe) {
		switch (pa->e_tag) {
		case ACL_USER:
			error = zfs_ugid_map_ns_to_host(uid_map,
			    KUID_TO_SUID(pa->e_uid), &id);
			break;
		case ACL_GROUP:
			error = zfs_ugid_map_ns_to_host(gid_map,
			    KGID_TO_SGID(pa->e_gid), &id);
			break;
		default:
			continue;
		}
		if (error != 0) {
			zpl_posix_acl_release(acl);
			return (ERR_PTR(-error));
		}
	}

	FOREACH_ACL_ENTRY(pa, acl, pe) {
		switch (pa->e_tag) {
		case ACL_USER:
			VERIFY0(zfs_ugid_map_ns_to_host(uid_map,
			    KUID_TO_SUID(pa->e_uid), &id));
			pa->e_uid = SUID_TO_KUID(id);
			break;
		case ACL_GROUP:
			VERIFY0(zfs_ugid_map_ns_to_host(gid_map,
			    KGID_TO_SGID(pa->e_gid), &id));
			pa->e_gid = SGID_TO_KGID(id);
			break;
		default:
			continue;
		}
	}

	return (acl);
}

int
zfs_ugid_map_acl_to_xattr(struct zfs_ugid_map *uid_map,
    struct zfs_ugid_map *gid_map, struct posix_acl *acl, void *value, int size)
{
	struct posix_acl *acl_map;
	struct posix_acl_entry *pa, *pe;
	uint64_t id;
	int error, ret;

	if (uid_map == NULL && gid_map == NULL)
		return (posix_acl_to_xattr(kcred->user_ns, acl, value, size));

	acl_map = posix_acl_clone(acl, GFP_KERNEL);
	if (acl_map == NULL)
		return (-ENOMEM);
	if (IS_ERR(acl_map))
		return (PTR_ERR(acl_map));

	/* Validate the complete ACL before changing the clone. */
	FOREACH_ACL_ENTRY(pa, acl_map, pe) {
		switch (pa->e_tag) {
		case ACL_USER:
			error = zfs_ugid_map_host_to_ns(uid_map,
			    KUID_TO_SUID(pa->e_uid), &id);
			break;
		case ACL_GROUP:
			error = zfs_ugid_map_host_to_ns(gid_map,
			    KGID_TO_SGID(pa->e_gid), &id);
			break;
		default:
			continue;
		}
		if (error != 0) {
			zpl_posix_acl_release(acl_map);
			return (-error);
		}
	}

	FOREACH_ACL_ENTRY(pa, acl_map, pe) {
		switch (pa->e_tag) {
		case ACL_USER:
			VERIFY0(zfs_ugid_map_host_to_ns(uid_map,
			    KUID_TO_SUID(pa->e_uid), &id));
			pa->e_uid = SUID_TO_KUID(id);
			break;
		case ACL_GROUP:
			VERIFY0(zfs_ugid_map_host_to_ns(gid_map,
			    KGID_TO_SGID(pa->e_gid), &id));
			pa->e_gid = SGID_TO_KGID(id);
			break;
		default:
			continue;
		}
	}

	/* Make the xattr with mapped entries. */
	ret = posix_acl_to_xattr(kcred->user_ns, acl_map, value, size);
	zpl_posix_acl_release(acl_map);

	return (ret);
}
