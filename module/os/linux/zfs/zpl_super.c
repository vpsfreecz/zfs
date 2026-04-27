// SPDX-License-Identifier: CDDL-1.0
/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or https://opensource.org/licenses/CDDL-1.0.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright (c) 2011, Lawrence Livermore National Security, LLC.
 * Copyright (c) 2023, Datto Inc. All rights reserved.
 * Copyright (c) 2025, Klara, Inc.
 * Copyright (c) 2025, Rob Norris <robn@despairlabs.com>
 */


#include <sys/zfs_znode.h>
#include <sys/zfs_vfsops.h>
#include <sys/zfs_vnops.h>
#include <sys/zfs_ctldir.h>
#include <sys/zpl.h>
#include <sys/mntent.h>
#include <linux/iversion.h>
#include <linux/version.h>
#include <linux/vfs_compat.h>
#ifdef HAVE_FILE_SYSTEM_TYPE_INIT_FS_CONTEXT
#include <linux/fs_context.h>
#include <linux/fs_parser.h>
#include <linux/string.h>
#endif

/*
 * What to do when the last reference to an inode is released. If 0, the kernel
 * will cache it on the superblock. If 1, the inode will be freed immediately.
 * See zpl_drop_inode().
 */
int zfs_delete_inode = 0;

static struct inode *
zpl_inode_alloc(struct super_block *sb)
{
	struct inode *ip;

	if (zfs_inode_alloc(sb, &ip) != 0)
		return (NULL);

	inode_set_iversion(ip, 1);

	return (ip);
}

static void __maybe_unused
zpl_inode_free(struct inode *ip)
{
	zfs_inode_free(ip);
}

static void __maybe_unused
zpl_inode_destroy(struct inode *ip)
{
	ASSERT(atomic_read(&ip->i_count) == 0);
	zfs_inode_destroy(ip);
}

/*
 * Called from __mark_inode_dirty() to reflect that something in the
 * inode has changed.  We use it to ensure the znode system attributes
 * are always strictly update to date with respect to the inode.
 */
static void
zpl_dirty_inode(struct inode *ip, int flags)
{
	fstrans_cookie_t cookie;

	cookie = spl_fstrans_mark();
	zfs_dirty_inode(ip, flags);
	spl_fstrans_unmark(cookie);
}

/*
 * ->drop_inode() is called when the last reference to an inode is released.
 * Its return value indicates if the inode should be destroyed immediately, or
 * cached on the superblock structure.
 *
 * By default (zfs_delete_inode=0), we call generic_drop_inode(), which returns
 * "destroy immediately" if the inode is unhashed and has no links (roughly: no
 * longer exists on disk). On datasets with millions of rarely-accessed files,
 * this can cause a large amount of memory to be "pinned" by cached inodes,
 * which in turn pin their associated dnodes and dbufs, until the kernel starts
 * reporting memory pressure and requests OpenZFS release some memory (see
 * zfs_prune()).
 *
 * When set to 1, we call generic_delete_inode(), which always returns "destroy
 * immediately", resulting in inodes being destroyed immediately, releasing
 * their associated dnodes and dbufs to the dbuf cache and the ARC to be
 * evicted as normal.
 *
 * Note that the "last reference" doesn't always mean the last _userspace_
 * reference; the dentry cache also holds a reference, so "busy" inodes will
 * still be kept alive that way (subject to dcache tuning).
 */
static int
zpl_drop_inode(struct inode *ip)
{
	if (zfs_delete_inode)
		return (generic_delete_inode(ip));
	return (generic_drop_inode(ip));
}

/*
 * The ->evict_inode() callback must minimally truncate the inode pages,
 * and call clear_inode().  For 2.6.35 and later kernels this will
 * simply update the inode state, with the sync occurring before the
 * truncate in evict().  For earlier kernels clear_inode() maps to
 * end_writeback() which is responsible for completing all outstanding
 * write back.  In either case, once this is done it is safe to cleanup
 * any remaining inode specific data via zfs_inactive().
 * remaining filesystem specific data.
 */
static void
zpl_evict_inode(struct inode *ip)
{
	fstrans_cookie_t cookie;

	cookie = spl_fstrans_mark();
	truncate_inode_pages_final(&ip->i_data);
	clear_inode(ip);
	zfs_inactive(ip);
#ifdef HAVE_INODE_FREE
	zfs_inode_destroy(ip);
#endif
	spl_fstrans_unmark(cookie);
}

static void
zpl_put_super(struct super_block *sb)
{
	fstrans_cookie_t cookie;
	int error;

	cookie = spl_fstrans_mark();
	error = -zfs_umount(sb);
	spl_fstrans_unmark(cookie);
	ASSERT3S(error, <=, 0);
}

/*
 * zfs_sync() is the underlying implementation for the sync(2) and syncfs(2)
 * syscalls, via sb->s_op->sync_fs().
 *
 * Before kernel 5.17 (torvalds/linux@5679897eb104), syncfs() ->
 * sync_filesystem() would ignore the return from sync_fs(), instead only
 * considing the error from syncing the underlying block device (sb->s_dev).
 * Since OpenZFS doesn't _have_ an underlying block device, there's no way for
 * us to report a sync directly.
 *
 * However, in 5.8 (torvalds/linux@735e4ae5ba28) the superblock gained an extra
 * error store `s_wb_err`, to carry errors seen on page writeback since the
 * last call to syncfs(). If sync_filesystem() does not return an error, any
 * existing writeback error on the superblock will be used instead (and cleared
 * either way). We don't use this (page writeback is a different thing for us),
 * so for 5.8-5.17 we can use that instead to get syncfs() to return the error.
 *
 * Before 5.8, we have no other good options - no matter what happens, the
 * userspace program will be told the call has succeeded, and so we must make
 * it so, Therefore, when we are asked to wait for sync to complete (wait ==
 * 1), if zfs_sync() has returned an error we have no choice but to block,
 * regardless of the reason.
 *
 * The 5.17 change was backported to the 5.10, 5.15 and 5.16 series, and likely
 * to some vendor kernels. Meanwhile, s_wb_err is still in use in 6.15 (the
 * mainline Linux series at time of writing), and has likely been backported to
 * vendor kernels before 5.8. We don't really want to use a workaround when we
 * don't have to, but we can't really detect whether or not sync_filesystem()
 * will return our errors (without a difficult runtime test anyway). So, we use
 * a static version check: any kernel reporting its version as 5.17+ will use a
 * direct error return, otherwise, we'll either use s_wb_err if it was detected
 * at configure (5.8-5.16 + vendor backports). If it's unavailable, we will
 * block to ensure the correct semantics.
 *
 * See https://github.com/openzfs/zfs/issues/17416 for further discussion.
 */
static int
zpl_sync_fs(struct super_block *sb, int wait)
{
	fstrans_cookie_t cookie;
	cred_t *cr = CRED();
	int error;

	crhold(cr);
	cookie = spl_fstrans_mark();
	error = -zfs_sync(sb, wait, cr);

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 17, 0)
#ifdef HAVE_SUPER_BLOCK_S_WB_ERR
	if (error && wait)
		errseq_set(&sb->s_wb_err, error);
#else
	if (error && wait) {
		zfsvfs_t *zfsvfs = sb->s_fs_info;
		ASSERT3P(zfsvfs, !=, NULL);
		if (zfs_enter(zfsvfs, FTAG) == 0) {
			txg_wait_synced(dmu_objset_pool(zfsvfs->z_os), 0);
			zfs_exit(zfsvfs, FTAG);
			error = 0;
		}
	}
#endif
#endif /* < 5.17.0 */

	spl_fstrans_unmark(cookie);
	crfree(cr);

	ASSERT3S(error, <=, 0);
	return (error);
}

static void
zpl_statfs_cap_files_32bit(struct kstatfs *statp)
{
	uint64_t usedobjs;
	uint64_t capped_usedobjs;
	uint64_t headroom;

	usedobjs = statp->f_files - statp->f_ffree;
	capped_usedobjs = MIN(usedobjs, (uint64_t)UINT32_MAX);
	headroom = (uint64_t)UINT32_MAX - capped_usedobjs;

	statp->f_ffree = MIN(statp->f_ffree, headroom);
	statp->f_files = statp->f_ffree + capped_usedobjs;
}

static int
zpl_statfs(struct dentry *dentry, struct kstatfs *statp)
{
	fstrans_cookie_t cookie;
	int error;

	cookie = spl_fstrans_mark();
	error = -zfs_statvfs(dentry->d_inode, statp);
	spl_fstrans_unmark(cookie);
	ASSERT3S(error, <=, 0);

	/*
	 * If required by a 32-bit system call, dynamically scale the
	 * block size up to 16MiB and decrease the block counts.  This
	 * allows for a maximum size of 64EiB to be reported.  The file
	 * counts must be artificially capped at 2^32-1.
	 */
	if (unlikely(zpl_is_32bit_api())) {
		while (statp->f_blocks > UINT32_MAX &&
		    statp->f_bsize < SPA_MAXBLOCKSIZE) {
			statp->f_frsize <<= 1;
			statp->f_bsize <<= 1;

			statp->f_blocks >>= 1;
			statp->f_bfree >>= 1;
			statp->f_bavail >>= 1;
		}

		zpl_statfs_cap_files_32bit(statp);
	}

	return (error);
}

static int
zpl_remount_fs(struct super_block *sb, int *flags, char *data)
{
	zfs_mnt_t zm = { .mnt_osname = NULL, .mnt_data = data };
	fstrans_cookie_t cookie;
	int error;

	cookie = spl_fstrans_mark();
	error = -zfs_remount(sb, flags, &zm);
	spl_fstrans_unmark(cookie);
	ASSERT3S(error, <=, 0);

	return (error);
}

static int
__zpl_show_devname(struct seq_file *seq, zfsvfs_t *zfsvfs)
{
	int error;
	if ((error = zpl_enter(zfsvfs, FTAG)) != 0)
		return (error);

	char *fsname = kmem_alloc(ZFS_MAX_DATASET_NAME_LEN, KM_SLEEP);
	dmu_objset_name(zfsvfs->z_os, fsname);

	for (int i = 0; fsname[i] != 0; i++) {
		/*
		 * Spaces in the dataset name must be converted to their
		 * octal escape sequence for getmntent(3) to correctly
		 * parse then fsname portion of /proc/self/mounts.
		 */
		if (fsname[i] == ' ') {
			seq_puts(seq, "\\040");
		} else {
			seq_putc(seq, fsname[i]);
		}
	}

	kmem_free(fsname, ZFS_MAX_DATASET_NAME_LEN);

	zpl_exit(zfsvfs, FTAG);

	return (0);
}

static int
zpl_show_devname(struct seq_file *seq, struct dentry *root)
{
	return (__zpl_show_devname(seq, root->d_sb->s_fs_info));
}

static int
__zpl_show_options(struct seq_file *seq, zfsvfs_t *zfsvfs)
{
	seq_printf(seq, ",%s",
	    zfsvfs->z_flags & ZSB_XATTR ? "xattr" : "noxattr");

#ifdef CONFIG_FS_POSIX_ACL
	switch (zfsvfs->z_acl_type) {
	case ZFS_ACLTYPE_POSIX:
		seq_puts(seq, ",posixacl");
		break;
	default:
		seq_puts(seq, ",noacl");
		break;
	}
#endif /* CONFIG_FS_POSIX_ACL */

	switch (zfsvfs->z_case) {
	case ZFS_CASE_SENSITIVE:
		seq_puts(seq, ",casesensitive");
		break;
	case ZFS_CASE_INSENSITIVE:
		seq_puts(seq, ",caseinsensitive");
		break;
	default:
		seq_puts(seq, ",casemixed");
		break;
	}

	return (0);
}

static int
zpl_show_options(struct seq_file *seq, struct dentry *root)
{
	return (__zpl_show_options(seq, root->d_sb->s_fs_info));
}

static int
zpl_fill_super_common(struct super_block *sb, const char *osname, vfs_t *vfsp,
    int silent)
{
	fstrans_cookie_t cookie;
	int error;

	cookie = spl_fstrans_mark();
	error = -zfs_domount_vfs(sb, osname, vfsp, silent);
	spl_fstrans_unmark(cookie);
	ASSERT3S(error, <=, 0);

	return (error);
}

#ifndef HAVE_FILE_SYSTEM_TYPE_INIT_FS_CONTEXT
static int
zpl_fill_super(struct super_block *sb, void *data, int silent)
{
	zfs_mnt_t *zm = (zfs_mnt_t *)data;
	vfs_t *vfsp;
	int error;

	error = zfsvfs_parse_options(zm->mnt_data, &vfsp);
	if (error)
		return (-error);

	return (zpl_fill_super_common(sb, zm->mnt_osname, vfsp, silent));
}

#endif /* !HAVE_FILE_SYSTEM_TYPE_INIT_FS_CONTEXT */

static int
zpl_test_super(struct super_block *s, void *data)
{
	zfsvfs_t *zfsvfs = s->s_fs_info;
	objset_t *os = data;
	/*
	 * If the os doesn't match the z_os in the super_block, assume it is
	 * not a match. Matching would imply a multimount of a dataset. It is
	 * possible that during a multimount, there is a simultaneous operation
	 * that changes the z_os, e.g., rollback, where the match will be
	 * missed, but in that case the user will get an EBUSY.
	 */
	return (zfsvfs != NULL && os == zfsvfs->z_os);
}

static int
zpl_validate_super_match(struct super_block *s, objset_t *os,
    boolean_t *issnap)
{
	int err = 0;

	if (IS_ERR(s))
		return (0);

	if (s->s_fs_info != NULL) {
		zfsvfs_t *zfsvfs = s->s_fs_info;

		if (zpl_enter(zfsvfs, FTAG) == 0) {
			if (os != zfsvfs->z_os)
				err = -SET_ERROR(EBUSY);
			*issnap = zfsvfs->z_issnap;
			zpl_exit(zfsvfs, FTAG);
		} else {
			err = -SET_ERROR(EBUSY);
		}
	}

	return (err);
}

#ifndef HAVE_FILE_SYSTEM_TYPE_INIT_FS_CONTEXT
static struct super_block *
zpl_mount_impl(struct file_system_type *fs_type, int flags, zfs_mnt_t *zm)
{
	struct super_block *s;
	objset_t *os;
	boolean_t issnap = B_FALSE;
	int err;

	err = dmu_objset_hold(zm->mnt_osname, FTAG, &os);
	if (err)
		return (ERR_PTR(-err));

	/*
	 * The dsl pool lock must be released prior to calling sget().
	 * It is possible sget() may block on the lock in grab_super()
	 * while deactivate_super() holds that same lock and waits for
	 * a txg sync.  If the dsl_pool lock is held over sget()
	 * this can prevent the pool sync and cause a deadlock.
	 */
	dsl_dataset_long_hold(dmu_objset_ds(os), FTAG);
	dsl_pool_rele(dmu_objset_pool(os), FTAG);

	s = sget(fs_type, zpl_test_super, set_anon_super, flags, os);
	err = zpl_validate_super_match(s, os, &issnap);
	dsl_dataset_long_rele(dmu_objset_ds(os), FTAG);
	dsl_dataset_rele(dmu_objset_ds(os), FTAG);

	if (IS_ERR(s))
		return (ERR_CAST(s));

	if (err) {
		deactivate_locked_super(s);
		return (ERR_PTR(err));
	}

	if (s->s_root == NULL) {
		err = zpl_fill_super(s, zm, flags & SB_SILENT ? 1 : 0);
		if (err) {
			deactivate_locked_super(s);
			return (ERR_PTR(err));
		}
		s->s_flags |= SB_ACTIVE;
	} else if (!issnap && ((flags ^ s->s_flags) & SB_RDONLY)) {
		/*
		 * Skip ro check for snap since snap is always ro regardless
		 * ro flag is passed by mount or not.
		 */
		deactivate_locked_super(s);
		return (ERR_PTR(-EBUSY));
	}

	return (s);
}

static struct dentry *
zpl_mount(struct file_system_type *fs_type, int flags,
    const char *osname, void *data)
{
	zfs_mnt_t zm = { .mnt_osname = osname, .mnt_data = data };
	struct super_block *sb = zpl_mount_impl(fs_type, flags, &zm);

	if (IS_ERR(sb))
		return (ERR_CAST(sb));

	return (dget(sb->s_root));
}

#endif /* !HAVE_FILE_SYSTEM_TYPE_INIT_FS_CONTEXT */

#ifdef HAVE_FILE_SYSTEM_TYPE_INIT_FS_CONTEXT
enum zpl_fs_param {
	ZPL_FSPARAM_RO = 0,
	ZPL_FSPARAM_RW,
	ZPL_FSPARAM_SETUID,
	ZPL_FSPARAM_NOSETUID,
	ZPL_FSPARAM_EXEC,
	ZPL_FSPARAM_NOEXEC,
	ZPL_FSPARAM_DEVICES,
	ZPL_FSPARAM_NODEVICES,
	ZPL_FSPARAM_DIRXATTR,
	ZPL_FSPARAM_SAXATTR,
	ZPL_FSPARAM_XATTR,
	ZPL_FSPARAM_NOXATTR,
	ZPL_FSPARAM_ATIME,
	ZPL_FSPARAM_NOATIME,
	ZPL_FSPARAM_RELATIME,
	ZPL_FSPARAM_NORELATIME,
	ZPL_FSPARAM_NBMAND,
	ZPL_FSPARAM_NONBMAND,
	ZPL_FSPARAM_MNTPOINT,
	ZPL_FSPARAM_IGNORE,
};

static const struct fs_parameter_spec zpl_fs_parameters[] = {
	fsparam_flag(MNTOPT_RO, ZPL_FSPARAM_RO),
	fsparam_flag(MNTOPT_RW, ZPL_FSPARAM_RW),
	fsparam_flag(MNTOPT_SETUID, ZPL_FSPARAM_SETUID),
	fsparam_flag(MNTOPT_NOSETUID, ZPL_FSPARAM_NOSETUID),
	fsparam_flag(MNTOPT_EXEC, ZPL_FSPARAM_EXEC),
	fsparam_flag(MNTOPT_NOEXEC, ZPL_FSPARAM_NOEXEC),
	fsparam_flag(MNTOPT_DEVICES, ZPL_FSPARAM_DEVICES),
	fsparam_flag(MNTOPT_NODEVICES, ZPL_FSPARAM_NODEVICES),
	fsparam_flag(MNTOPT_DIRXATTR, ZPL_FSPARAM_DIRXATTR),
	fsparam_flag(MNTOPT_SAXATTR, ZPL_FSPARAM_SAXATTR),
	fsparam_flag(MNTOPT_XATTR, ZPL_FSPARAM_XATTR),
	fsparam_flag(MNTOPT_NOXATTR, ZPL_FSPARAM_NOXATTR),
	fsparam_flag(MNTOPT_ATIME, ZPL_FSPARAM_ATIME),
	fsparam_flag(MNTOPT_NOATIME, ZPL_FSPARAM_NOATIME),
	fsparam_flag(MNTOPT_RELATIME, ZPL_FSPARAM_RELATIME),
	fsparam_flag(MNTOPT_NORELATIME, ZPL_FSPARAM_NORELATIME),
	fsparam_flag(MNTOPT_NBMAND, ZPL_FSPARAM_NBMAND),
	fsparam_flag(MNTOPT_NONBMAND, ZPL_FSPARAM_NONBMAND),
	fsparam_string_empty(MNTOPT_MNTPOINT, ZPL_FSPARAM_MNTPOINT),
	/* Accepted by libzfs/userland but already handled outside ZPL. */
	fsparam_flag(MNTOPT_DEFAULTS, ZPL_FSPARAM_IGNORE),
	fsparam_flag(MNTOPT_AUTO, ZPL_FSPARAM_IGNORE),
	fsparam_flag(MNTOPT_NOAUTO, ZPL_FSPARAM_IGNORE),
	fsparam_string(MNTOPT_CONTEXT, ZPL_FSPARAM_IGNORE),
	fsparam_string(MNTOPT_FSCONTEXT, ZPL_FSPARAM_IGNORE),
	fsparam_string(MNTOPT_DEFCONTEXT, ZPL_FSPARAM_IGNORE),
	fsparam_string(MNTOPT_ROOTCONTEXT, ZPL_FSPARAM_IGNORE),
	fsparam_flag(MNTOPT_DIRATIME, ZPL_FSPARAM_IGNORE),
	fsparam_flag(MNTOPT_NODIRATIME, ZPL_FSPARAM_IGNORE),
	fsparam_flag(MNTOPT_DIRSYNC, ZPL_FSPARAM_IGNORE),
	fsparam_flag(MNTOPT_GROUP, ZPL_FSPARAM_IGNORE),
	fsparam_flag(MNTOPT_IVERSION, ZPL_FSPARAM_IGNORE),
	fsparam_flag(MNTOPT_NOIVERSION, ZPL_FSPARAM_IGNORE),
	fsparam_flag(MNTOPT_OWNER, ZPL_FSPARAM_IGNORE),
	fsparam_flag(MNTOPT_USER, ZPL_FSPARAM_IGNORE),
	fsparam_flag(MNTOPT_USERS, ZPL_FSPARAM_IGNORE),
	fsparam_flag(MNTOPT_NETDEV, ZPL_FSPARAM_IGNORE),
	fsparam_flag(MNTOPT_NOFAIL, ZPL_FSPARAM_IGNORE),
	fsparam_flag(MNTOPT_STRICTATIME, ZPL_FSPARAM_IGNORE),
	fsparam_flag(MNTOPT_NOSTRICTATIME, ZPL_FSPARAM_IGNORE),
	fsparam_flag(MNTOPT_LAZYTIME, ZPL_FSPARAM_IGNORE),
	fsparam_flag(MNTOPT_SYNC, ZPL_FSPARAM_IGNORE),
	fsparam_flag(MNTOPT_ASYNC, ZPL_FSPARAM_IGNORE),
	fsparam_string_empty(MNTOPT_COMMENT, ZPL_FSPARAM_IGNORE),
	fsparam_flag(MNTOPT_ZFSUTIL, ZPL_FSPARAM_IGNORE),
	fsparam_flag(MNTOPT_ACL, ZPL_FSPARAM_IGNORE),
	fsparam_flag(MNTOPT_NOACL, ZPL_FSPARAM_IGNORE),
	fsparam_flag(MNTOPT_POSIXACL, ZPL_FSPARAM_IGNORE),
	fsparam_flag(MNTOPT_SUB, ZPL_FSPARAM_IGNORE),
	fsparam_flag(MNTOPT_NOSUB, ZPL_FSPARAM_IGNORE),
	fsparam_flag(MNTOPT_QUIET, ZPL_FSPARAM_IGNORE),
	fsparam_flag(MNTOPT_LOUD, ZPL_FSPARAM_IGNORE),
	fsparam_flag(MNTOPT_BIND, ZPL_FSPARAM_IGNORE),
	fsparam_flag(MNTOPT_RBIND, ZPL_FSPARAM_IGNORE),
	fsparam_flag(MNTOPT_CASESENSITIVE, ZPL_FSPARAM_IGNORE),
	fsparam_flag(MNTOPT_CASEINSENSITIVE, ZPL_FSPARAM_IGNORE),
	fsparam_flag(MNTOPT_CASEMIXED, ZPL_FSPARAM_IGNORE),
	{}
};

static void
zpl_fs_context_set_sb_flag(struct fs_context *fc, unsigned int flag,
    boolean_t enabled)
{
	fc->sb_flags_mask |= flag;
	if (enabled)
		fc->sb_flags |= flag;
	else
		fc->sb_flags &= ~flag;
}

static void
zpl_fs_context_sync_sb_flags(vfs_t *vfsp, unsigned int sb_flags,
    unsigned int sb_flags_mask)
{
	if (sb_flags_mask & SB_RDONLY)
		(void) zfsvfs_apply_option(vfsp,
		    (sb_flags & SB_RDONLY) ?
		    ZFS_MNTOPT_RO : ZFS_MNTOPT_RW, NULL);
	if (sb_flags_mask & SB_NOSUID)
		(void) zfsvfs_apply_option(vfsp,
		    (sb_flags & SB_NOSUID) ?
		    ZFS_MNTOPT_NOSETUID : ZFS_MNTOPT_SETUID, NULL);
	if (sb_flags_mask & SB_NODEV)
		(void) zfsvfs_apply_option(vfsp,
		    (sb_flags & SB_NODEV) ?
		    ZFS_MNTOPT_NODEVICES : ZFS_MNTOPT_DEVICES, NULL);
	if (sb_flags_mask & SB_NOEXEC)
		(void) zfsvfs_apply_option(vfsp,
		    (sb_flags & SB_NOEXEC) ?
		    ZFS_MNTOPT_NOEXEC : ZFS_MNTOPT_EXEC, NULL);
	if (sb_flags_mask & SB_NOATIME)
		(void) zfsvfs_apply_option(vfsp,
		    (sb_flags & SB_NOATIME) ?
		    ZFS_MNTOPT_NOATIME : ZFS_MNTOPT_ATIME, NULL);
}

static int
zpl_fs_context_parse_param(struct fs_context *fc, struct fs_parameter *param)
{
	struct fs_parse_result result;
	vfs_t *vfsp = fc->fs_private;
	int error;
	int opt;

	if (strcmp(param->key, "source") == 0)
		return (vfs_parse_fs_param_source(fc, param));

	opt = fs_parse(fc, zpl_fs_parameters, param, &result);
	if (opt < 0)
		return (opt);

	switch (opt) {
	case ZPL_FSPARAM_RO:
		zpl_fs_context_set_sb_flag(fc, SB_RDONLY, B_TRUE);
		error = zfsvfs_apply_option(vfsp, ZFS_MNTOPT_RO, NULL);
		break;
	case ZPL_FSPARAM_RW:
		zpl_fs_context_set_sb_flag(fc, SB_RDONLY, B_FALSE);
		error = zfsvfs_apply_option(vfsp, ZFS_MNTOPT_RW, NULL);
		break;
	case ZPL_FSPARAM_SETUID:
		zpl_fs_context_set_sb_flag(fc, SB_NOSUID, B_FALSE);
		error = zfsvfs_apply_option(vfsp, ZFS_MNTOPT_SETUID, NULL);
		break;
	case ZPL_FSPARAM_NOSETUID:
		zpl_fs_context_set_sb_flag(fc, SB_NOSUID, B_TRUE);
		error = zfsvfs_apply_option(vfsp, ZFS_MNTOPT_NOSETUID, NULL);
		break;
	case ZPL_FSPARAM_EXEC:
		zpl_fs_context_set_sb_flag(fc, SB_NOEXEC, B_FALSE);
		error = zfsvfs_apply_option(vfsp, ZFS_MNTOPT_EXEC, NULL);
		break;
	case ZPL_FSPARAM_NOEXEC:
		zpl_fs_context_set_sb_flag(fc, SB_NOEXEC, B_TRUE);
		error = zfsvfs_apply_option(vfsp, ZFS_MNTOPT_NOEXEC, NULL);
		break;
	case ZPL_FSPARAM_DEVICES:
		zpl_fs_context_set_sb_flag(fc, SB_NODEV, B_FALSE);
		error = zfsvfs_apply_option(vfsp, ZFS_MNTOPT_DEVICES, NULL);
		break;
	case ZPL_FSPARAM_NODEVICES:
		zpl_fs_context_set_sb_flag(fc, SB_NODEV, B_TRUE);
		error = zfsvfs_apply_option(vfsp, ZFS_MNTOPT_NODEVICES, NULL);
		break;
	case ZPL_FSPARAM_DIRXATTR:
		error = zfsvfs_apply_option(vfsp, ZFS_MNTOPT_DIRXATTR, NULL);
		break;
	case ZPL_FSPARAM_SAXATTR:
		error = zfsvfs_apply_option(vfsp, ZFS_MNTOPT_SAXATTR, NULL);
		break;
	case ZPL_FSPARAM_XATTR:
		error = zfsvfs_apply_option(vfsp, ZFS_MNTOPT_XATTR, NULL);
		break;
	case ZPL_FSPARAM_NOXATTR:
		error = zfsvfs_apply_option(vfsp, ZFS_MNTOPT_NOXATTR, NULL);
		break;
	case ZPL_FSPARAM_ATIME:
		zpl_fs_context_set_sb_flag(fc, SB_NOATIME, B_FALSE);
		error = zfsvfs_apply_option(vfsp, ZFS_MNTOPT_ATIME, NULL);
		break;
	case ZPL_FSPARAM_NOATIME:
		zpl_fs_context_set_sb_flag(fc, SB_NOATIME, B_TRUE);
		error = zfsvfs_apply_option(vfsp, ZFS_MNTOPT_NOATIME, NULL);
		break;
	case ZPL_FSPARAM_RELATIME:
		error = zfsvfs_apply_option(vfsp, ZFS_MNTOPT_RELATIME, NULL);
		break;
	case ZPL_FSPARAM_NORELATIME:
		error = zfsvfs_apply_option(vfsp, ZFS_MNTOPT_NORELATIME, NULL);
		break;
	case ZPL_FSPARAM_NBMAND:
		error = zfsvfs_apply_option(vfsp, ZFS_MNTOPT_NBMAND, NULL);
		break;
	case ZPL_FSPARAM_NONBMAND:
		error = zfsvfs_apply_option(vfsp, ZFS_MNTOPT_NONBMAND, NULL);
		break;
	case ZPL_FSPARAM_MNTPOINT:
		error = zfsvfs_apply_option(vfsp, ZFS_MNTOPT_MNTPOINT,
		    param->string);
		break;
	case ZPL_FSPARAM_IGNORE:
		error = 0;
		break;
	default:
		return (-EINVAL);
	}

	return (error ? -error : 0);
}

static int
zpl_fs_context_parse_monolithic(struct fs_context *fc, void *data)
{
	if (data == NULL)
		return (0);

	return (vfs_parse_monolithic_sep(fc, data, strsep));
}

static void
zpl_fs_context_free(struct fs_context *fc)
{
	zfsvfs_vfs_free(fc->fs_private);
	fc->fs_private = NULL;
}

static int
zpl_test_super_fc(struct super_block *s, struct fs_context *fc)
{
	return (zpl_test_super(s, fc->sget_key));
}

static int
zpl_get_tree(struct fs_context *fc)
{
	struct super_block *s;
	objset_t *os;
	vfs_t *vfsp = fc->fs_private;
	boolean_t issnap = B_FALSE;
	int err;

	if (fc->source == NULL)
		return (-EINVAL);

	zpl_fs_context_sync_sb_flags(vfsp, fc->sb_flags, fc->sb_flags_mask);
	if ((fc->sb_flags & SB_RDONLY) && !vfsp->vfs_do_readonly)
		(void) zfsvfs_apply_option(vfsp, ZFS_MNTOPT_RO, NULL);

	err = dmu_objset_hold(fc->source, FTAG, &os);
	if (err)
		return (-err);

	/*
	 * The dsl pool lock must be released prior to calling sget_fc().
	 * It is possible sget_fc() may block on the lock in grab_super()
	 * while deactivate_super() holds that same lock and waits for a txg
	 * sync. If the dsl_pool lock is held over sget_fc() this can prevent
	 * the pool sync and cause a deadlock.
	 */
	dsl_dataset_long_hold(dmu_objset_ds(os), FTAG);
	dsl_pool_rele(dmu_objset_pool(os), FTAG);

	fc->sget_key = os;
	s = sget_fc(fc, zpl_test_super_fc, set_anon_super_fc);
	err = zpl_validate_super_match(s, os, &issnap);
	dsl_dataset_long_rele(dmu_objset_ds(os), FTAG);
	dsl_dataset_rele(dmu_objset_ds(os), FTAG);

	if (IS_ERR(s))
		return (PTR_ERR(s));

	if (err) {
		deactivate_locked_super(s);
		return (err);
	}

	if (s->s_root == NULL) {
		err = zpl_fill_super_common(s, fc->source, vfsp,
		    !!(fc->sb_flags & SB_SILENT));
		fc->fs_private = NULL;
		if (err) {
			deactivate_locked_super(s);
			return (err);
		}
		s->s_flags |= SB_ACTIVE;
	} else if (!issnap && ((fc->sb_flags ^ s->s_flags) & SB_RDONLY)) {
		/*
		 * Skip ro check for snap since snap is always ro regardless
		 * ro flag is passed by mount or not.
		 */
		deactivate_locked_super(s);
		return (-EBUSY);
	}

	fc->root = dget(s->s_root);
	return (0);
}

static int
zpl_reconfigure(struct fs_context *fc)
{
	struct super_block *sb = fc->root->d_sb;
	vfs_t *vfsp = fc->fs_private;
	fstrans_cookie_t cookie;
	int flags;
	int error;

	flags = (sb->s_flags & ~fc->sb_flags_mask) |
	    (fc->sb_flags & fc->sb_flags_mask);
	zpl_fs_context_sync_sb_flags(vfsp, fc->sb_flags, fc->sb_flags_mask);

	cookie = spl_fstrans_mark();
	error = -zfs_remount_vfs(sb, &flags, vfsp);
	fc->fs_private = NULL;
	spl_fstrans_unmark(cookie);
	ASSERT3S(error, <=, 0);
	if (error)
		return (error);

	sb->s_flags &= ~fc->sb_flags_mask;
	sb->s_flags |= (flags & fc->sb_flags_mask);
	return (0);
}

static const struct fs_context_operations zpl_fs_context_ops = {
	.free			= zpl_fs_context_free,
	.parse_param		= zpl_fs_context_parse_param,
	.parse_monolithic	= zpl_fs_context_parse_monolithic,
	.get_tree		= zpl_get_tree,
	.reconfigure		= zpl_reconfigure,
};

static int
zpl_init_fs_context(struct fs_context *fc)
{
	fc->fs_private = zfsvfs_vfs_alloc();
	fc->ops = &zpl_fs_context_ops;
	return (0);
}
#endif /* HAVE_FILE_SYSTEM_TYPE_INIT_FS_CONTEXT */

static void
zpl_kill_sb(struct super_block *sb)
{
	zfs_preumount(sb);
	kill_anon_super(sb);
}

void
zpl_prune_sb(uint64_t nr_to_scan, void *arg)
{
	struct super_block *sb = (struct super_block *)arg;
	int objects = 0;

	/*
	 * Ensure the superblock is not in the process of being torn down.
	 */
#ifdef HAVE_SB_DYING
	if (down_read_trylock(&sb->s_umount)) {
		if (!(sb->s_flags & SB_DYING) && sb->s_root &&
		    (sb->s_flags & SB_BORN)) {
			(void) zfs_prune(sb, nr_to_scan, &objects);
		}
		up_read(&sb->s_umount);
	}
#else
	if (down_read_trylock(&sb->s_umount)) {
		if (!hlist_unhashed(&sb->s_instances) &&
		    sb->s_root && (sb->s_flags & SB_BORN)) {
			(void) zfs_prune(sb, nr_to_scan, &objects);
		}
		up_read(&sb->s_umount);
	}
#endif
}

const struct super_operations zpl_super_operations = {
	.alloc_inode		= zpl_inode_alloc,
#ifdef HAVE_INODE_FREE
	.free_inode		= zpl_inode_free,
#else
	.destroy_inode		= zpl_inode_destroy,
#endif
	.dirty_inode		= zpl_dirty_inode,
	.write_inode		= NULL,
	.drop_inode		= zpl_drop_inode,
	.evict_inode		= zpl_evict_inode,
	.put_super		= zpl_put_super,
	.sync_fs		= zpl_sync_fs,
	.statfs			= zpl_statfs,
	.remount_fs		= zpl_remount_fs,
	.show_devname		= zpl_show_devname,
	.show_options		= zpl_show_options,
	.show_stats		= NULL,
};

struct file_system_type zpl_fs_type = {
	.owner			= THIS_MODULE,
	.name			= ZFS_DRIVER,
#if defined(HAVE_IDMAP_MNT_API)
	.fs_flags		= FS_USERNS_MOUNT | FS_ALLOW_IDMAP,
#else
	.fs_flags		= FS_USERNS_MOUNT,
#endif
#ifdef HAVE_FILE_SYSTEM_TYPE_INIT_FS_CONTEXT
	.init_fs_context	= zpl_init_fs_context,
	.parameters		= zpl_fs_parameters,
#else
	.mount			= zpl_mount,
#endif
	.kill_sb		= zpl_kill_sb,
};
ZFS_MODULE_PARAM(zfs, zfs_, delete_inode, INT, ZMOD_RW,
	"Delete inodes as soon as the last reference is released.");
