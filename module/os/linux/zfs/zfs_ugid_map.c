#include <sys/types.h>
#include <sys/fs/zfs.h>
#include <sys/dsl_prop.h>
#include <sys/dsl_dataset.h>
#include <sys/zap.h>
#include <sys/dmu_objset.h>
#include <sys/zfs_ugid_map.h>


struct zfs_ugid_map*
zfs_create_ugid_map(objset_t *os, zfs_prop_t prop)
{
	char *value = kmem_alloc(ZAP_MAXVALUELEN, KM_SLEEP);
        char source[ZFS_MAX_DATASET_NAME_LEN] = "Internal error - setpoint not determined";
	uint32_t ns_id, host_id, count;
	int pos = 0, i = 0, error;
	struct zfs_ugid_map *ugid_map;
	struct zfs_ugid_map_entry *entry;

	dsl_pool_config_enter(dmu_objset_pool(os), FTAG);

	error = dsl_prop_get_ds(os->os_dsl_dataset, zfs_prop_to_name(prop), 1,
	    ZAP_MAXVALUELEN, value, source);

	dsl_pool_config_exit(dmu_objset_pool(os), FTAG);

	if (error != 0) {
		kmem_free(value, ZAP_MAXVALUELEN);
		/*
		 * TODO: should we report error? we'd have to pass the return
		 * value through function argument to be able to report errors
		 */
		return NULL;
		//return (error);
	}

	if (strcmp(value, "none") == 0)
		return NULL;

	ugid_map = vmem_zalloc(sizeof(struct zfs_ugid_map), KM_SLEEP);
	ugid_map->m_size = ZFS_UGID_MAP_SIZE;
	ugid_map->m_entries = 0;
	ugid_map->m_map = vmem_zalloc(sizeof(struct zfs_ugid_map_entry*) * ugid_map->m_size,
			KM_SLEEP);

	while (1) {
		error = sscanf(value + pos, "%u:%u:%u%n", &ns_id, &host_id, &count, &i);
		pos += i;

		if (error == 0) {
			break;

		} else if (error != 3) {
#ifdef DEBUG
			printk("invalid ugid map format");
#endif
			return NULL;
			//return (error);
		}
#ifdef DEBUG
		printk("got map: ns_id=%u, host_id=%u,, count=%u for %s", ns_id, host_id, count, source);
#endif

		entry = vmem_zalloc(sizeof(struct zfs_ugid_map_entry), KM_SLEEP);
		entry->e_ns_id = ns_id;
		entry->e_host_id = host_id;
		entry->e_count = count;

		ugid_map->m_map[ugid_map->m_entries] = entry;
		ugid_map->m_entries += 1;

		// TODO: make map size dynamic
		if (ugid_map->m_entries == ZFS_UGID_MAP_SIZE)
			break;
		else if (value[pos] == ',')
			pos += 1;
		else
			break;
	}

	if (ugid_map->m_entries == 0) {
		vmem_free(ugid_map->m_map, sizeof(struct zfs_ugid_map_entry*) * ugid_map->m_size);
		vmem_free(ugid_map, sizeof(struct zfs_ugid_map));
		return NULL;
	}

	kmem_free(value, ZAP_MAXVALUELEN);
	return ugid_map;
}

void
zfs_free_ugid_map(struct zfs_ugid_map *ugid_map)
{
	int i;

	if (ugid_map == NULL)
		return;

	for (i = 0; i < ugid_map->m_size; i++) {
		vmem_free(ugid_map->m_map[i], sizeof(struct zfs_ugid_map_entry));
	}

	vmem_free(ugid_map->m_map, sizeof(struct zfs_ugid_map_entry*) * ugid_map->m_size);
	vmem_free(ugid_map, sizeof(struct zfs_ugid_map));
}

uint64_t
zfs_ugid_map_ns_to_host(struct zfs_ugid_map *ugid_map, uint64_t id)
{
	uint64_t res;
	int i;
	struct zfs_ugid_map_entry *entry;

	if (ugid_map == NULL)
		return id;

	/* look for a matching mapping */
	for (i = 0; i < ugid_map->m_entries; i++) {
		entry = ugid_map->m_map[i];

		/* check if we're already mapped into the entry */
		if (id >= entry->e_host_id && id < (entry->e_host_id + entry->e_count)) {
#ifdef DEBUG
			printk("zfs_ugid_map_ns_to_host: %lld already mapped via mapping %lld:%lld:%lld",
				id, entry->e_ns_id, entry->e_host_id, entry->e_count);
#endif
			return id;
		}

		/* check if we can map the entry */
		if (id >= entry->e_ns_id && id < (entry->e_ns_id + entry->e_count)) {
			res = entry->e_host_id + (id - entry->e_ns_id);
#ifdef DEBUG
			printk("zfs_ugid_map_ns_to_host: %lld -> %lld via mapping %lld:%lld:%lld",
				id, res, entry->e_ns_id, entry->e_host_id, entry->e_count);
#endif
			VERIFY3U(0, <=, res);
			return res;
		}
	}

	/* id not mapped, return nobody */
	return 65534;
}

uint64_t
zfs_ugid_map_host_to_ns(struct zfs_ugid_map *ugid_map, uint64_t id)
{
	uint64_t res;
	int i;
	struct zfs_ugid_map_entry *entry;

	if (ugid_map == NULL)
		return id;

	/* look for a matching mapping */
	for (i = 0; i < ugid_map->m_entries; i++) {
		entry = ugid_map->m_map[i];

		/* check if we're already mapped into the entry */
		if (id >= entry->e_ns_id && id < (entry->e_ns_id + entry->e_count)) {
#ifdef DEBUG
			printk("zfs_ugid_map_host_to_ns: %lld already mapped via mapping %lld:%lld:%lld",
				id, entry->e_ns_id, entry->e_host_id, entry->e_count);
#endif
			return id;
		}

		/* check if we can map the entry */
		if (id >= entry->e_host_id && id < (entry->e_host_id + entry->e_count)) {
			res = (id - entry->e_host_id) + entry->e_ns_id;
#ifdef DEBUG
			printk("zfs_ugid_map_host_to_ns: %lld -> %lld via mapping %lld:%lld:%lld",
				id, res, entry->e_ns_id, entry->e_host_id, entry->e_count);
#endif
			VERIFY3U(0, <=, res);
			return res;
		}
	}

	/* id not mapped, return nobody */
	return 65534;
}
