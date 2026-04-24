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
 */

#ifndef	_SYS_ZPL_H
#define	_SYS_ZPL_H

#include <sys/mntent.h>
#include <sys/vfs.h>
#include <linux/aio.h>
#include <linux/dcache_compat.h>
#include <linux/exportfs.h>
#include <linux/falloc.h>
#include <linux/mm_compat.h>
#include <linux/pagemap_compat.h>
#include <linux/parser.h>
#include <linux/vfs_compat.h>
#include <linux/writeback.h>
#include <linux/xattr_compat.h>

#if defined(HAVE_FILE_KATTR)
struct file_kattr;
#else
struct fileattr;
#endif

/* zpl_inode.c */
extern void zpl_vap_init(vattr_t *vap, struct inode *dir,
    umode_t mode, cred_t *cr, zidmap_t *mnt_ns);

extern const struct inode_operations zpl_inode_operations;
extern const struct inode_operations zpl_dir_inode_operations;
extern const struct inode_operations zpl_symlink_inode_operations;
extern const struct inode_operations zpl_special_inode_operations;

/* zpl_file.c */
extern const struct address_space_operations zpl_address_space_operations;
extern const struct file_operations zpl_file_operations;
extern const struct file_operations zpl_dir_file_operations;
#if defined(HAVE_FILEATTR_OPS)
#if defined(HAVE_FILE_KATTR)
extern int zpl_fileattr_get(struct dentry *dentry, struct file_kattr *fa);
extern int zpl_fileattr_set(zidmap_t *idmap, struct dentry *dentry,
    struct file_kattr *fa);
#else
extern int zpl_fileattr_get(struct dentry *dentry, struct fileattr *fa);
extern int zpl_fileattr_set(zidmap_t *idmap, struct dentry *dentry,
    struct fileattr *fa);
#endif
#endif

/* zpl_super.c */
extern void zpl_prune_sb(uint64_t nr_to_scan, void *arg);

extern const struct super_operations zpl_super_operations;
extern const struct export_operations zpl_export_operations;
extern struct file_system_type zpl_fs_type;

/* zpl_xattr.c */
extern ssize_t zpl_xattr_list(struct dentry *dentry, char *buf, size_t size);
extern int zpl_xattr_security_init(struct inode *ip, struct inode *dip,
    const struct qstr *qstr, zidmap_t *mnt_ns);

#if defined(CONFIG_FS_POSIX_ACL)

#if defined(HAVE_SET_ACL_IDMAP_DENTRY)
extern int zpl_set_acl(struct mnt_idmap *idmap, struct dentry *dentry,
    struct posix_acl *acl, int type);
#elif defined(HAVE_SET_ACL_USERNS)
extern int zpl_set_acl(struct user_namespace *userns, struct inode *ip,
    struct posix_acl *acl, int type);
#elif defined(HAVE_SET_ACL_USERNS_DENTRY_ARG2)
extern int zpl_set_acl(struct user_namespace *userns, struct dentry *dentry,
    struct posix_acl *acl, int type);
#else
extern int zpl_set_acl(struct inode *ip, struct posix_acl *acl, int type);
#endif /* HAVE_SET_ACL_USERNS */

#if defined(HAVE_GET_ACL_IDMAP_DENTRY)
extern struct posix_acl *zpl_get_acl(struct mnt_idmap *idmap,
    struct dentry *dentry, int type);
#elif defined(HAVE_GET_ACL_RCU)
extern struct posix_acl *zpl_get_acl(struct inode *ip, int type, bool rcu);
#elif defined(HAVE_GET_ACL)
extern struct posix_acl *zpl_get_acl(struct inode *ip, int type);
#endif
#if defined(HAVE_GET_INODE_ACL)
extern struct posix_acl *zpl_get_inode_acl(struct inode *ip, int type,
    bool rcu);
#endif
extern int zpl_init_acl(zidmap_t *mnt_ns, struct inode *ip,
    struct inode *dir);
extern int zpl_chmod_acl(zidmap_t *mnt_ns, struct inode *ip);
#else
static inline int
zpl_init_acl(zidmap_t *mnt_ns, struct inode *ip, struct inode *dir)
{
	return (0);
}

static inline int
zpl_chmod_acl(zidmap_t *mnt_ns, struct inode *ip)
{
	return (0);
}
#endif /* CONFIG_FS_POSIX_ACL */

extern xattr_handler_t *zpl_xattr_handlers[];

/* zpl_ctldir.c */
extern const struct file_operations zpl_fops_root;
extern const struct inode_operations zpl_ops_root;

extern const struct file_operations zpl_fops_snapdir;
extern const struct inode_operations zpl_ops_snapdir;

extern const struct file_operations zpl_fops_shares;
extern const struct inode_operations zpl_ops_shares;

/* zpl_file_range.c */

/* handlers for file_operations of the same name */
extern ssize_t zpl_copy_file_range(struct file *src_file, loff_t src_off,
    struct file *dst_file, loff_t dst_off, size_t len, unsigned int flags);
extern loff_t zpl_remap_file_range(struct file *src_file, loff_t src_off,
    struct file *dst_file, loff_t dst_off, loff_t len, unsigned int flags);
extern int zpl_clone_file_range(struct file *src_file, loff_t src_off,
    struct file *dst_file, loff_t dst_off, uint64_t len);
extern int zpl_dedupe_file_range(struct file *src_file, loff_t src_off,
    struct file *dst_file, loff_t dst_off, uint64_t len);


#if defined(HAVE_INODE_TIMESTAMP_TRUNCATE)
#define	zpl_inode_timestamp_truncate(ts, ip)	timestamp_truncate(ts, ip)
#else
#define	zpl_inode_timestamp_truncate(ts, ip)	\
	timespec64_trunc(ts, (ip)->i_sb->s_time_gran)
#endif

#if defined(HAVE_INODE_OWNER_OR_CAPABLE)
#define	zpl_inode_owner_or_capable(ns, ip)	inode_owner_or_capable(ip)
#elif defined(HAVE_INODE_OWNER_OR_CAPABLE_USERNS)
#define	zpl_inode_owner_or_capable(ns, ip)	inode_owner_or_capable(ns, ip)
#elif defined(HAVE_INODE_OWNER_OR_CAPABLE_IDMAP)
#define	zpl_inode_owner_or_capable(idmap, ip) inode_owner_or_capable(idmap, ip)
#else
#error "Unsupported kernel"
#endif

#if defined(HAVE_SETATTR_PREPARE_USERNS) || defined(HAVE_SETATTR_PREPARE_IDMAP)
#define	zpl_setattr_prepare(ns, dentry, ia)	setattr_prepare(ns, dentry, ia)
#else
/*
 * Use kernel-provided version, or our own from
 * linux/vfs_compat.h
 */
#define	zpl_setattr_prepare(ns, dentry, ia)	setattr_prepare(dentry, ia)
#endif

#ifdef HAVE_INODE_GET_CTIME
#define	zpl_inode_get_ctime(ip)	inode_get_ctime(ip)
#else
#define	zpl_inode_get_ctime(ip)	(ip->i_ctime)
#endif
#ifdef HAVE_INODE_SET_CTIME_TO_TS
#define	zpl_inode_set_ctime_to_ts(ip, ts)	inode_set_ctime_to_ts(ip, ts)
#else
#define	zpl_inode_set_ctime_to_ts(ip, ts)	(ip->i_ctime = ts)
#endif
#ifdef HAVE_INODE_GET_ATIME
#define	zpl_inode_get_atime(ip)	inode_get_atime(ip)
#else
#define	zpl_inode_get_atime(ip)	(ip->i_atime)
#endif
#ifdef HAVE_INODE_SET_ATIME_TO_TS
#define	zpl_inode_set_atime_to_ts(ip, ts)	inode_set_atime_to_ts(ip, ts)
#else
#define	zpl_inode_set_atime_to_ts(ip, ts)	(ip->i_atime = ts)
#endif
#ifdef HAVE_INODE_GET_MTIME
#define	zpl_inode_get_mtime(ip)	inode_get_mtime(ip)
#else
#define	zpl_inode_get_mtime(ip)	(ip->i_mtime)
#endif
#ifdef HAVE_INODE_SET_MTIME_TO_TS
#define	zpl_inode_set_mtime_to_ts(ip, ts)	inode_set_mtime_to_ts(ip, ts)
#else
#define	zpl_inode_set_mtime_to_ts(ip, ts)	(ip->i_mtime = ts)
#endif

static inline struct page *
zpl_folio_head_page(struct folio *folio)
{
	return (folio_page(folio, 0));
}

static inline void
zpl_folio_wait_writeback(struct folio *folio)
{
#ifdef HAVE_PAGEMAP_FOLIO_WAIT_WRITEBACK
	folio_wait_writeback(folio);
#elif defined(HAVE_PAGEMAP_FOLIO_WAIT_BIT)
	folio_wait_bit(folio, PG_writeback);
#else
	wait_on_page_bit(zpl_folio_head_page(folio), PG_writeback);
#endif
}

/*
 * Segment-only page-cache mutators may preserve existing validity for the
 * whole cache unit, but they must not create it unless they cover the whole
 * unit.
 */
static inline boolean_t
zpl_folio_range_is_full(struct folio *folio, size_t off, size_t len)
{
	size_t fsize = folio_size(folio);

	ASSERT3U(off + len, <=, fsize);
	return (off == 0 && len == fsize);
}

static inline void
zpl_folio_range_write_done(struct folio *folio, boolean_t was_uptodate,
    size_t off, size_t len)
{
	ClearPageError(zpl_folio_head_page(folio));
	if (was_uptodate || zpl_folio_range_is_full(folio, off, len))
		folio_mark_uptodate(folio);
	else
		folio_clear_uptodate(folio);
}

#endif	/* _SYS_ZPL_H */
