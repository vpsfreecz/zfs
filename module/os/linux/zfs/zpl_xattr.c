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
 *
 * Extended attributes (xattr) on Solaris are implemented as files
 * which exist in a hidden xattr directory.  These extended attributes
 * can be accessed using the attropen() system call which opens
 * the extended attribute.  It can then be manipulated just like
 * a standard file descriptor.  This has a couple advantages such
 * as practically no size limit on the file, and the extended
 * attributes permissions may differ from those of the parent file.
 * This interface is really quite clever, but it's also completely
 * different than what is supported on Linux.  It also comes with a
 * steep performance penalty when accessing small xattrs because they
 * are not stored with the parent file.
 *
 * Under Linux extended attributes are manipulated by the system
 * calls getxattr(2), setxattr(2), and listxattr(2).  They consider
 * extended attributes to be name/value pairs where the name is a
 * NULL terminated string.  The name must also include one of the
 * following namespace prefixes:
 *
 *   user     - No restrictions and is available to user applications.
 *   trusted  - Restricted to kernel and root (CAP_SYS_ADMIN) use.
 *   system   - Used for access control lists (system.nfs4_acl, etc).
 *   security - Used by SELinux to store a files security context.
 *
 * The value under Linux to limited to 65536 bytes of binary data.
 * In practice, individual xattrs tend to be much smaller than this
 * and are typically less than 100 bytes.  A good example of this
 * are the security.selinux xattrs which are less than 100 bytes and
 * exist for every file when xattr labeling is enabled.
 *
 * The Linux xattr implementation has been written to take advantage of
 * this typical usage.  When the dataset property 'xattr=sa' is set,
 * then xattrs will be preferentially stored as System Attributes (SA).
 * This allows tiny xattrs (~100 bytes) to be stored with the dnode and
 * up to 64k of xattrs to be stored in the spill block.  If additional
 * xattr space is required, which is unlikely under Linux, they will
 * be stored using the traditional directory approach.
 *
 * This optimization results in roughly a 3x performance improvement
 * when accessing xattrs because it avoids the need to perform a seek
 * for every xattr value.  When multiple xattrs are stored per-file
 * the performance improvements are even greater because all of the
 * xattrs stored in the spill block will be cached.
 *
 * However, by default SA based xattrs are disabled in the Linux port
 * to maximize compatibility with other implementations.  If you do
 * enable SA based xattrs then they will not be visible on platforms
 * which do not support this feature.
 *
 * NOTE: One additional consequence of the xattr directory implementation
 * is that when an extended attribute is manipulated an inode is created.
 * This inode will exist in the Linux inode cache but there will be no
 * associated entry in the dentry cache which references it.  This is
 * safe but it may result in some confusion.  Enabling SA based xattrs
 * largely avoids the issue except in the overflow case.
 */

#include <sys/zfs_znode.h>
#include <sys/zfs_vfsops.h>
#include <sys/zfs_vnops.h>
#include <sys/zfs_ugid_map.h>
#include <sys/zap.h>
#include <sys/txg.h>
#include <sys/vfs.h>
#include <sys/zpl.h>
#include <linux/capability.h>
#include <linux/xattr.h>
#include <linux/vfs_compat.h>

enum xattr_permission {
	XAPERM_DENY,
	XAPERM_ALLOW,
	XAPERM_COMPAT,
};

typedef struct xattr_filldir {
	size_t size;
	size_t offset;
	char *buf;
	struct dentry *dentry;
} xattr_filldir_t;

static enum xattr_permission zpl_xattr_permission(xattr_filldir_t *,
    const char *, int);

static int zfs_xattr_compat = 0;

/*
 * Determine is a given xattr name should be visible and if so copy it
 * in to the provided buffer (xf->buf).
 */
static int
zpl_xattr_filldir(xattr_filldir_t *xf, const char *name, int name_len)
{
	enum xattr_permission perm;

	/* Check permissions using the per-namespace list xattr handler. */
	perm = zpl_xattr_permission(xf, name, name_len);
	if (perm == XAPERM_DENY)
		return (0);

	/* Prefix the name with "user." if it does not have a namespace. */
	if (perm == XAPERM_COMPAT) {
		if (xf->buf) {
			if (xf->offset + XATTR_USER_PREFIX_LEN + 1 > xf->size)
				return (-ERANGE);

			memcpy(xf->buf + xf->offset, XATTR_USER_PREFIX,
			    XATTR_USER_PREFIX_LEN);
			xf->buf[xf->offset + XATTR_USER_PREFIX_LEN] = '\0';
		}

		xf->offset += XATTR_USER_PREFIX_LEN;
	}

	/* When xf->buf is NULL only calculate the required size. */
	if (xf->buf) {
		if (xf->offset + name_len + 1 > xf->size)
			return (-ERANGE);

		memcpy(xf->buf + xf->offset, name, name_len);
		xf->buf[xf->offset + name_len] = '\0';
	}

	xf->offset += (name_len + 1);

	return (0);
}

/*
 * Read as many directory entry names as will fit in to the provided buffer,
 * or when no buffer is provided calculate the required buffer size.
 */
static int
zpl_xattr_readdir(struct inode *dxip, xattr_filldir_t *xf)
{
	zap_cursor_t zc;
	zap_attribute_t	*zap = zap_attribute_alloc();
	int error;

	zap_cursor_init(&zc, ITOZSB(dxip)->z_os, ITOZ(dxip)->z_id);

	while ((error = -zap_cursor_retrieve(&zc, zap)) == 0) {

		if (zap->za_integer_length != 8 || zap->za_num_integers != 1) {
			error = -ENXIO;
			break;
		}

		error = zpl_xattr_filldir(xf, zap->za_name,
		    strlen(zap->za_name));
		if (error)
			break;

		zap_cursor_advance(&zc);
	}

	zap_cursor_fini(&zc);
	zap_attribute_free(zap);

	if (error == -ENOENT)
		error = 0;

	return (error);
}

static ssize_t
zpl_xattr_list_dir(xattr_filldir_t *xf, cred_t *cr)
{
	struct inode *ip = xf->dentry->d_inode;
	struct inode *dxip = NULL;
	znode_t *dxzp;
	int error;

	/* Lookup the xattr directory */
	error = -zfs_lookup(ITOZ(ip), NULL, &dxzp,
	    LOOKUP_XATTR | LOOKUP_SKIP_SEARCH, cr, NULL, NULL,
	    zfs_init_idmap);
	if (error) {
		if (error == -ENOENT)
			error = 0;

		return (error);
	}

	dxip = ZTOI(dxzp);
	error = zpl_xattr_readdir(dxip, xf);
	iput(dxip);

	return (error);
}

static ssize_t
zpl_xattr_list_sa(xattr_filldir_t *xf)
{
	znode_t *zp = ITOZ(xf->dentry->d_inode);
	nvpair_t *nvp = NULL;
	int error = 0;

	mutex_enter(&zp->z_lock);
	if (zp->z_xattr_cached == NULL)
		error = -zfs_sa_get_xattr(zp);
	mutex_exit(&zp->z_lock);

	if (error)
		return (error);

	ASSERT(zp->z_xattr_cached);

	while ((nvp = nvlist_next_nvpair(zp->z_xattr_cached, nvp)) != NULL) {
		ASSERT3U(nvpair_type(nvp), ==, DATA_TYPE_BYTE_ARRAY);

		error = zpl_xattr_filldir(xf, nvpair_name(nvp),
		    strlen(nvpair_name(nvp)));
		if (error)
			return (error);
	}

	return (0);
}

ssize_t
zpl_xattr_list(struct dentry *dentry, char *buffer, size_t buffer_size)
{
	znode_t *zp = ITOZ(dentry->d_inode);
	zfsvfs_t *zfsvfs = ZTOZSB(zp);
	xattr_filldir_t xf = { buffer_size, 0, buffer, dentry };
	cred_t *cr = CRED();
	fstrans_cookie_t cookie;
	int error = 0;

	crhold(cr);
	cookie = spl_fstrans_mark();
	if ((error = zpl_enter_verify_zp(zfsvfs, zp, FTAG)) != 0)
		goto out1;
	rw_enter(&zp->z_xattr_lock, RW_READER);

	if (zfsvfs->z_use_sa && zp->z_is_sa) {
		error = zpl_xattr_list_sa(&xf);
		if (error)
			goto out;
	}

	error = zpl_xattr_list_dir(&xf, cr);
	if (error)
		goto out;

	error = xf.offset;
out:

	rw_exit(&zp->z_xattr_lock);
	zpl_exit(zfsvfs, FTAG);
out1:
	spl_fstrans_unmark(cookie);
	crfree(cr);

	return (error);
}

static int
zpl_xattr_get_dir(struct inode *ip, const char *name, void *value,
    size_t size, cred_t *cr)
{
	fstrans_cookie_t cookie;
	struct inode *xip = NULL;
	znode_t *dxzp = NULL;
	znode_t *xzp = NULL;
	int error;

	/* Lookup the xattr directory */
	error = -zfs_lookup(ITOZ(ip), NULL, &dxzp,
	    LOOKUP_XATTR | LOOKUP_SKIP_SEARCH, cr, NULL, NULL,
	    zfs_init_idmap);
	if (error)
		goto out;

	/* Lookup a specific xattr name in the directory */
	error = -zfs_lookup(dxzp, (char *)name, &xzp, LOOKUP_SKIP_SEARCH,
	    cr, NULL, NULL, zfs_init_idmap);
	if (error)
		goto out;

	xip = ZTOI(xzp);
	if (!size) {
		error = i_size_read(xip);
		goto out;
	}

	if (size < i_size_read(xip)) {
		error = -ERANGE;
		goto out;
	}

	struct iovec iov;
	iov.iov_base = (void *)value;
	iov.iov_len = size;

	zfs_uio_t uio;
	zfs_uio_iovec_init(&uio, &iov, 1, 0, UIO_SYSSPACE, size, 0);

	cookie = spl_fstrans_mark();
	error = -zfs_read(ITOZ(xip), &uio, 0, cr);
	spl_fstrans_unmark(cookie);

	if (error == 0)
		error = size - zfs_uio_resid(&uio);
out:
	if (xzp)
		zrele(xzp);

	if (dxzp)
		zrele(dxzp);

	return (error);
}

static int
zpl_xattr_get_sa(struct inode *ip, const char *name, void *value, size_t size)
{
	znode_t *zp = ITOZ(ip);
	uchar_t *nv_value;
	uint_t nv_size;
	int error = 0;

	ASSERT(RW_LOCK_HELD(&zp->z_xattr_lock));

	mutex_enter(&zp->z_lock);
	if (zp->z_xattr_cached == NULL)
		error = -zfs_sa_get_xattr(zp);
	mutex_exit(&zp->z_lock);

	if (error)
		return (error);

	ASSERT(zp->z_xattr_cached);
	error = -nvlist_lookup_byte_array(zp->z_xattr_cached, name,
	    &nv_value, &nv_size);
	if (error)
		return (error);

	if (size == 0 || value == NULL)
		return (nv_size);

	if (size < nv_size)
		return (-ERANGE);

	memcpy(value, nv_value, nv_size);

	return (nv_size);
}

static int
__zpl_xattr_get(struct inode *ip, const char *name, void *value, size_t size,
    cred_t *cr)
{
	znode_t *zp = ITOZ(ip);
	zfsvfs_t *zfsvfs = ZTOZSB(zp);
	int error;

	ASSERT(RW_LOCK_HELD(&zp->z_xattr_lock));

	if (zfsvfs->z_use_sa && zp->z_is_sa) {
		error = zpl_xattr_get_sa(ip, name, value, size);
		if (error != -ENOENT)
			goto out;
	}

	error = zpl_xattr_get_dir(ip, name, value, size, cr);
out:
	if (error == -ENOENT)
		error = -ENODATA;

	return (error);
}

#define	XATTR_NOENT	0x0
#define	XATTR_IN_SA	0x1
#define	XATTR_IN_DIR	0x2
/* check where the xattr resides */
static int
__zpl_xattr_where(struct inode *ip, const char *name, int *where, cred_t *cr)
{
	znode_t *zp = ITOZ(ip);
	zfsvfs_t *zfsvfs = ZTOZSB(zp);
	int error;

	ASSERT(where);
	ASSERT(RW_LOCK_HELD(&zp->z_xattr_lock));

	*where = XATTR_NOENT;
	if (zfsvfs->z_use_sa && zp->z_is_sa) {
		error = zpl_xattr_get_sa(ip, name, NULL, 0);
		if (error >= 0)
			*where |= XATTR_IN_SA;
		else if (error != -ENOENT)
			return (error);
	}

	error = zpl_xattr_get_dir(ip, name, NULL, 0, cr);
	if (error >= 0)
		*where |= XATTR_IN_DIR;
	else if (error != -ENOENT)
		return (error);

	if (*where == (XATTR_IN_SA|XATTR_IN_DIR))
		cmn_err(CE_WARN, "ZFS: inode %p has xattr \"%s\""
		    " in both SA and dir", ip, name);
	if (*where == XATTR_NOENT)
		error = -ENODATA;
	else
		error = 0;
	return (error);
}

static int
zpl_xattr_get(struct inode *ip, const char *name, void *value, size_t size)
{
	znode_t *zp = ITOZ(ip);
	zfsvfs_t *zfsvfs = ZTOZSB(zp);
	cred_t *cr = CRED();
	fstrans_cookie_t cookie;
	int error;

	crhold(cr);
	cookie = spl_fstrans_mark();
	if ((error = zpl_enter_verify_zp(zfsvfs, zp, FTAG)) != 0)
		goto out;
	rw_enter(&zp->z_xattr_lock, RW_READER);
	error = __zpl_xattr_get(ip, name, value, size, cr);
	rw_exit(&zp->z_xattr_lock);
	zpl_exit(zfsvfs, FTAG);
out:
	spl_fstrans_unmark(cookie);
	crfree(cr);

	return (error);
}

static int
zpl_xattr_set_dir(struct inode *ip, const char *name, const void *value,
    size_t size, int flags, zidmap_t *mnt_ns, cred_t *cr)
{
	znode_t *dxzp = NULL;
	znode_t *xzp = NULL;
	vattr_t *vap = NULL;
	int lookup_flags, error;
	const int xattr_mode = S_IFREG | 0644;
	loff_t pos = 0;

	/*
	 * Lookup the xattr directory.  When we're adding an entry pass
	 * CREATE_XATTR_DIR to ensure the xattr directory is created.
	 * When removing an entry this flag is not passed to avoid
	 * unnecessarily creating a new xattr directory.
	 */
	lookup_flags = LOOKUP_XATTR;
	if (value != NULL)
		lookup_flags |= CREATE_XATTR_DIR;

	error = -zfs_lookup(ITOZ(ip), NULL, &dxzp,
	    lookup_flags | LOOKUP_SKIP_SEARCH, cr, NULL, NULL, mnt_ns);
	if (error)
		goto out;

	/* Lookup a specific xattr name in the directory */
	error = -zfs_lookup(dxzp, (char *)name, &xzp, LOOKUP_SKIP_SEARCH,
	    cr, NULL, NULL, zfs_init_idmap);
	if (error && (error != -ENOENT))
		goto out;

	error = 0;

	/* Remove a specific name xattr when value is set to NULL. */
	if (value == NULL) {
		if (xzp)
			error = -zfs_remove(dxzp, (char *)name, cr, 0, mnt_ns);

		goto out;
	}

	/* Lookup failed create a new xattr. */
	if (xzp == NULL) {
		vap = kmem_zalloc(sizeof (vattr_t), KM_SLEEP);
		vap->va_mode = xattr_mode;
		vap->va_mask = ATTR_MODE;
		vap->va_uid = zfs_vfsuid_to_uid(mnt_ns, zfs_i_user_ns(ip),
		    crgetuid(cr));
		vap->va_gid = zfs_vfsgid_to_gid(mnt_ns, zfs_i_user_ns(ip),
		    crgetgid(cr));

		error = -zfs_create(dxzp, (char *)name, vap, 0, 0644, &xzp,
		    cr, ATTR_NOACLCHECK, NULL, mnt_ns);
		if (error)
			goto out;
	}

	ASSERT(xzp != NULL);

	error = -zfs_freesp(xzp, 0, 0, xattr_mode, TRUE);
	if (error)
		goto out;

	error = -zfs_write_simple(xzp, value, size, pos, NULL);
out:
	if (error == 0) {
		zpl_inode_set_ctime_to_ts(ip, current_time(ip));
		zfs_mark_inode_dirty(ip);
	}

	if (vap)
		kmem_free(vap, sizeof (vattr_t));

	if (xzp)
		zrele(xzp);

	if (dxzp)
		zrele(dxzp);

	if (error == -ENOENT)
		error = -ENODATA;

	ASSERT3S(error, <=, 0);

	return (error);
}

static int
zpl_xattr_set_sa(struct inode *ip, const char *name, const void *value,
    size_t size, int flags, zidmap_t *mnt_ns, cred_t *cr)
{
	znode_t *zp = ITOZ(ip);
	nvlist_t *nvl;
	size_t sa_size;
	int error = 0;

	mutex_enter(&zp->z_lock);
	if (zp->z_xattr_cached == NULL)
		error = -zfs_sa_get_xattr(zp);
	mutex_exit(&zp->z_lock);

	if (error)
		return (error);

	ASSERT(zp->z_xattr_cached);
	nvl = zp->z_xattr_cached;

	if (value == NULL) {
		error = -nvlist_remove(nvl, name, DATA_TYPE_BYTE_ARRAY);
		if (error == -ENOENT)
			error = zpl_xattr_set_dir(ip, name, NULL, 0, flags,
			    mnt_ns, cr);
	} else {
		/* Limited to 32k to keep nvpair memory allocations small */
		if (size > DXATTR_MAX_ENTRY_SIZE)
			return (-EFBIG);

		/* Prevent the DXATTR SA from consuming the entire SA region */
		error = -nvlist_size(nvl, &sa_size, NV_ENCODE_XDR);
		if (error)
			return (error);

		if (sa_size > DXATTR_MAX_SA_SIZE)
			return (-EFBIG);

		error = -nvlist_add_byte_array(nvl, name,
		    (uchar_t *)value, size);
	}

	/*
	 * Update the SA for additions, modifications, and removals. On
	 * error drop the inconsistent cached version of the nvlist, it
	 * will be reconstructed from the ARC when next accessed.
	 */
	if (error == 0)
		error = -zfs_sa_set_xattr(zp, name, value, size);

	if (error) {
		nvlist_free(nvl);
		zp->z_xattr_cached = NULL;
	}

	ASSERT3S(error, <=, 0);

	return (error);
}

static int
__zpl_xattr_set_locked(struct inode *ip, const char *name, const void *value,
    size_t size, int flags, zidmap_t *mnt_ns, cred_t *cr)
{
	znode_t *zp = ITOZ(ip);
	zfsvfs_t *zfsvfs = ZTOZSB(zp);
	int where;
	int error;

	ASSERT(RW_WRITE_HELD(&zp->z_xattr_lock));

	/*
	 * Before setting the xattr check to see if it already exists.
	 * This is done to ensure the following optional flags are honored.
	 *
	 *   XATTR_CREATE: fail if xattr already exists
	 *   XATTR_REPLACE: fail if xattr does not exist
	 *
	 * We also want to know if it resides in sa or dir, so we can make
	 * sure we don't end up with duplicate in both places.
	 */
	error = __zpl_xattr_where(ip, name, &where, cr);
	if (error < 0) {
		if (error != -ENODATA)
			return (error);
		if (flags & XATTR_REPLACE)
			return (error);

		/* The xattr to be removed already doesn't exist */
		error = 0;
		if (value == NULL)
			return (error);
	} else {
		error = -EEXIST;
		if (flags & XATTR_CREATE)
			return (error);
	}

	/* Preferentially store the xattr as a SA for better performance */
	if (zfsvfs->z_use_sa && zp->z_is_sa &&
	    (zfsvfs->z_xattr_sa || (value == NULL && where & XATTR_IN_SA))) {
		error = zpl_xattr_set_sa(ip, name, value, size, flags,
		    mnt_ns, cr);
		if (error == 0) {
			/*
			 * Successfully put into SA, we need to clear the one
			 * in dir.
			 */
			if (where & XATTR_IN_DIR)
				zpl_xattr_set_dir(ip, name, NULL, 0, 0,
				    mnt_ns, cr);
			return (error);
		}
	}

	error = zpl_xattr_set_dir(ip, name, value, size, flags, mnt_ns, cr);
	/*
	 * Successfully put into dir, we need to clear the one in SA.
	 */
	if (error == 0 && (where & XATTR_IN_SA))
		zpl_xattr_set_sa(ip, name, NULL, 0, 0, mnt_ns, cr);

	return (error);
}

static int
zpl_xattr_set(struct inode *ip, const char *name, const void *value,
    size_t size, int flags, zidmap_t *mnt_ns)
{
	znode_t *zp = ITOZ(ip);
	zfsvfs_t *zfsvfs = ZTOZSB(zp);
	cred_t *cr = CRED();
	fstrans_cookie_t cookie;
	int error;

	crhold(cr);
	cookie = spl_fstrans_mark();
	if ((error = zpl_enter_verify_zp(zfsvfs, zp, FTAG)) != 0)
		goto out;
	rw_enter(&zp->z_xattr_lock, RW_WRITER);
	error = __zpl_xattr_set_locked(ip, name, value, size, flags,
	    mnt_ns, cr);
	rw_exit(&zp->z_xattr_lock);
	zpl_exit(zfsvfs, FTAG);
out:
	spl_fstrans_unmark(cookie);
	crfree(cr);
	ASSERT3S(error, <=, 0);

	return (error);
}

/*
 * Extended user attributes
 *
 * "Extended user attributes may be assigned to files and directories for
 * storing arbitrary additional information such as the mime type,
 * character set or encoding of a file.  The access permissions for user
 * attributes are defined by the file permission bits: read permission
 * is required to retrieve the attribute value, and writer permission is
 * required to change it.
 *
 * The file permission bits of regular files and directories are
 * interpreted differently from the file permission bits of special
 * files and symbolic links.  For regular files and directories the file
 * permission bits define access to the file's contents, while for
 * device special files they define access to the device described by
 * the special file.  The file permissions of symbolic links are not
 * used in access checks.  These differences would allow users to
 * consume filesystem resources in a way not controllable by disk quotas
 * for group or world writable special files and directories.
 *
 * For this reason, extended user attributes are allowed only for
 * regular files and directories, and access to extended user attributes
 * is restricted to the owner and to users with appropriate capabilities
 * for directories with the sticky bit set (see the chmod(1) manual page
 * for an explanation of the sticky bit)." - xattr(7)
 *
 * ZFS allows extended user attributes to be disabled administratively
 * by setting the 'xattr=off' property on the dataset.
 */
static int
__zpl_xattr_user_list(struct inode *ip, char *list, size_t list_size,
    const char *name, size_t name_len)
{
	return (ITOZSB(ip)->z_flags & ZSB_XATTR);
}
ZPL_XATTR_LIST_WRAPPER(zpl_xattr_user_list);

static int
__zpl_xattr_user_get(struct inode *ip, const char *name,
    void *value, size_t size)
{
	int error;
	/* xattr_resolve_name will do this for us if this is defined */
	if (ZFS_XA_NS_PREFIX_FORBIDDEN(name))
		return (-EINVAL);
	if (!(ITOZSB(ip)->z_flags & ZSB_XATTR))
		return (-EOPNOTSUPP);

	/*
	 * Try to look up the name with the namespace prefix first for
	 * compatibility with xattrs from this platform.  If that fails,
	 * try again without the namespace prefix for compatibility with
	 * other platforms.
	 */
	char *xattr_name = kmem_asprintf("%s%s", XATTR_USER_PREFIX, name);
	error = zpl_xattr_get(ip, xattr_name, value, size);
	kmem_strfree(xattr_name);
	if (error == -ENODATA)
		error = zpl_xattr_get(ip, name, value, size);

	return (error);
}
ZPL_XATTR_GET_WRAPPER(zpl_xattr_user_get);

static int
__zpl_xattr_user_set(zidmap_t *user_ns,
    struct inode *ip, const char *name,
    const void *value, size_t size, int flags)
{
	int error = 0;
	/* xattr_resolve_name will do this for us if this is defined */
	if (ZFS_XA_NS_PREFIX_FORBIDDEN(name))
		return (-EINVAL);
	if (!(ITOZSB(ip)->z_flags & ZSB_XATTR))
		return (-EOPNOTSUPP);

	/*
	 * Remove alternate compat version of the xattr so we only set the
	 * version specified by the zfs_xattr_compat tunable.
	 *
	 * The following flags must be handled correctly:
	 *
	 *   XATTR_CREATE: fail if xattr already exists
	 *   XATTR_REPLACE: fail if xattr does not exist
	 */
	char *prefixed_name = kmem_asprintf("%s%s", XATTR_USER_PREFIX, name);
	const char *clear_name, *set_name;
	if (zfs_xattr_compat) {
		clear_name = prefixed_name;
		set_name = name;
	} else {
		clear_name = name;
		set_name = prefixed_name;
	}
	/*
	 * Clear the old value with the alternative name format, if it exists.
	 */
	error = zpl_xattr_set(ip, clear_name, NULL, 0, flags, user_ns);
	/*
	 * XATTR_CREATE was specified and we failed to clear the xattr
	 * because it already exists.  Stop here.
	 */
	if (error == -EEXIST)
		goto out;
	/*
	 * If XATTR_REPLACE was specified and we succeeded to clear
	 * an xattr, we don't need to replace anything when setting
	 * the new value.  If we failed with -ENODATA that's fine,
	 * there was nothing to be cleared and we can ignore the error.
	 */
	if (error == 0)
		flags &= ~XATTR_REPLACE;
	/*
	 * Set the new value with the configured name format.
	 */
	error = zpl_xattr_set(ip, set_name, value, size, flags, user_ns);
out:
	kmem_strfree(prefixed_name);
	return (error);
}
ZPL_XATTR_SET_WRAPPER(zpl_xattr_user_set);

static xattr_handler_t zpl_xattr_user_handler =
{
	.prefix	= XATTR_USER_PREFIX,
	.list	= zpl_xattr_user_list,
	.get	= zpl_xattr_user_get,
	.set	= zpl_xattr_user_set,
};

/*
 * Trusted extended attributes
 *
 * "Trusted extended attributes are visible and accessible only to
 * processes that have the CAP_SYS_ADMIN capability.  Attributes in this
 * class are used to implement mechanisms in user space (i.e., outside
 * the kernel) which keep information in extended attributes to which
 * ordinary processes should not have access." - xattr(7)
 */
static uint_t zfs_xattr_trusted_userns_enable = 0;
ZFS_MODULE_PARAM(zfs, zfs_, xattr_trusted_userns_enable, UINT, ZMOD_RW,
	"Allow trusted.* xattr access from first-level user namespaces");

bool __xattr_trusted(void);

bool
__xattr_trusted(void)
{
	struct user_namespace *ns = current_user_ns();
	if (ns == &init_user_ns)
		return (ns_capable(ns, CAP_SYS_ADMIN));

	if (zfs_xattr_trusted_userns_enable != 0 &&
	    ns->parent == &init_user_ns)
		return (ns_capable(ns, CAP_SYS_ADMIN));

	return (false);
}


static int
__zpl_xattr_trusted_list(struct inode *ip, char *list, size_t list_size,
    const char *name, size_t name_len)
{
	return (__xattr_trusted());
}
ZPL_XATTR_LIST_WRAPPER(zpl_xattr_trusted_list);

static int
__zpl_xattr_trusted_get(struct inode *ip, const char *name,
    void *value, size_t size)
{
	char *xattr_name;
	int error;

	if (!__xattr_trusted())
		return (-EACCES);
	/* xattr_resolve_name will do this for us if this is defined */
	xattr_name = kmem_asprintf("%s%s", XATTR_TRUSTED_PREFIX, name);
	error = zpl_xattr_get(ip, xattr_name, value, size);
	kmem_strfree(xattr_name);

	return (error);
}
ZPL_XATTR_GET_WRAPPER(zpl_xattr_trusted_get);

static int
__zpl_xattr_trusted_set(zidmap_t *user_ns,
    struct inode *ip, const char *name,
    const void *value, size_t size, int flags)
{
	char *xattr_name;
	int error;

	if (!__xattr_trusted())
		return (-EACCES);
	/* xattr_resolve_name will do this for us if this is defined */
	xattr_name = kmem_asprintf("%s%s", XATTR_TRUSTED_PREFIX, name);
	error = zpl_xattr_set(ip, xattr_name, value, size, flags, user_ns);
	kmem_strfree(xattr_name);

	return (error);
}
ZPL_XATTR_SET_WRAPPER(zpl_xattr_trusted_set);

static xattr_handler_t zpl_xattr_trusted_handler = {
	.prefix	= XATTR_TRUSTED_PREFIX,
	.list	= zpl_xattr_trusted_list,
	.get	= zpl_xattr_trusted_get,
	.set	= zpl_xattr_trusted_set,
};

#define	ZPL_FILECAP_RECORD_SIZE	64
#define	ZPL_FILECAP_MAGIC_SIZE	8
#define	ZPL_FILECAP_STATE_OFFSET	8
#define	ZPL_FILECAP_SIZE_OFFSET	12
#define	ZPL_FILECAP_VALUE_OFFSET	16
#define	ZPL_FILECAP_ROOT_OFFSET	40
#define	ZPL_FILECAP_RESERVED_OFFSET	44

enum zpl_filecap_state {
	ZPL_FILECAP_TOMBSTONE = 1,
	ZPL_FILECAP_PASSTHROUGH = 2,
	ZPL_FILECAP_MAPPED = 3,
};

static const uint8_t zpl_filecap_magic[ZPL_FILECAP_MAGIC_SIZE] = {
	'V', 'P', 'S', 'F', 'C', 'A', 'P', '1'
};

static uint32_t
zpl_filecap_get_le32(const uint8_t *p)
{
	__le32 value;

	memcpy(&value, p, sizeof (value));
	return (le32_to_cpu(value));
}

static void
zpl_filecap_put_le32(uint8_t *p, uint32_t value)
{
	__le32 encoded = cpu_to_le32(value);

	memcpy(p, &encoded, sizeof (encoded));
}

static boolean_t
zpl_filecap_value_valid(const void *value, size_t size)
{
	const uint8_t *bytes = value;
	uint32_t magic;

	if (value == NULL || (size != XATTR_CAPS_SZ_2 &&
	    size != XATTR_CAPS_SZ_3))
		return (B_FALSE);

	magic = zpl_filecap_get_le32(bytes);
	if ((magic & VFS_CAP_FLAGS_MASK) & ~VFS_CAP_FLAGS_EFFECTIVE)
		return (B_FALSE);

	if (size == XATTR_CAPS_SZ_2)
		return ((magic & VFS_CAP_REVISION_MASK) == VFS_CAP_REVISION_2);

	return ((magic & VFS_CAP_REVISION_MASK) == VFS_CAP_REVISION_3 &&
	    zpl_filecap_get_le32(bytes + XATTR_CAPS_SZ_2) != UINT32_MAX);
}

static boolean_t
zpl_filecap_record_valid(const uint8_t record[ZPL_FILECAP_RECORD_SIZE])
{
	uint32_t state, size, root;
	size_t i;

	if (memcmp(record, zpl_filecap_magic, ZPL_FILECAP_MAGIC_SIZE) != 0)
		return (B_FALSE);

	state = zpl_filecap_get_le32(record + ZPL_FILECAP_STATE_OFFSET);
	size = zpl_filecap_get_le32(record + ZPL_FILECAP_SIZE_OFFSET);
	root = zpl_filecap_get_le32(record + ZPL_FILECAP_ROOT_OFFSET);

	for (i = ZPL_FILECAP_RESERVED_OFFSET;
	    i < ZPL_FILECAP_RECORD_SIZE; i++) {
		if (record[i] != 0)
			return (B_FALSE);
	}

	if (state == ZPL_FILECAP_TOMBSTONE) {
		if (size != 0 || root != 0)
			return (B_FALSE);
		for (i = ZPL_FILECAP_VALUE_OFFSET;
		    i < ZPL_FILECAP_RESERVED_OFFSET; i++) {
			if (record[i] != 0)
				return (B_FALSE);
		}
		return (B_TRUE);
	}

	if ((state != ZPL_FILECAP_PASSTHROUGH &&
	    state != ZPL_FILECAP_MAPPED) ||
	    !zpl_filecap_value_valid(record + ZPL_FILECAP_VALUE_OFFSET, size))
		return (B_FALSE);

	for (i = ZPL_FILECAP_VALUE_OFFSET + size;
	    i < ZPL_FILECAP_ROOT_OFFSET; i++) {
		if (record[i] != 0)
			return (B_FALSE);
	}

	if (state == ZPL_FILECAP_PASSTHROUGH)
		return (root == 0);

	return (size == XATTR_CAPS_SZ_3 && root != UINT32_MAX);
}

static void
zpl_filecap_record_build(uint8_t record[ZPL_FILECAP_RECORD_SIZE],
    enum zpl_filecap_state state, const void *value, size_t size,
    uint32_t root)
{
	memset(record, 0, ZPL_FILECAP_RECORD_SIZE);
	memcpy(record, zpl_filecap_magic, ZPL_FILECAP_MAGIC_SIZE);
	zpl_filecap_put_le32(record + ZPL_FILECAP_STATE_OFFSET, state);
	zpl_filecap_put_le32(record + ZPL_FILECAP_SIZE_OFFSET, size);
	if (value != NULL)
		memcpy(record + ZPL_FILECAP_VALUE_OFFSET, value, size);
	zpl_filecap_put_le32(record + ZPL_FILECAP_ROOT_OFFSET, root);
	ASSERT(zpl_filecap_record_valid(record));
}

static int
zpl_filecap_record_get(znode_t *zp,
    uint8_t record[ZPL_FILECAP_RECORD_SIZE])
{
	zfsvfs_t *zfsvfs = ZTOZSB(zp);
	int error;

	ASSERT(RW_LOCK_HELD(&zp->z_xattr_lock));
	error = sa_lookup(zp->z_sa_hdl,
	    SA_ZPL_VPSADMINOS_FILECAP(zfsvfs), record,
	    ZPL_FILECAP_RECORD_SIZE);
	if (error == ENOENT)
		return (-ENODATA);
	if (error != 0)
		return (-error);
	if (!zpl_filecap_record_valid(record))
		return (-EIO);

	return (0);
}

static int
zpl_filecap_record_write(znode_t *zp,
    uint8_t record[ZPL_FILECAP_RECORD_SIZE])
{
	zfsvfs_t *zfsvfs = ZTOZSB(zp);
	dmu_tx_t *tx;
	int error;

	ASSERT(RW_WRITE_HELD(&zp->z_xattr_lock));
	ASSERT(zpl_filecap_record_valid(record));

	tx = dmu_tx_create(zfsvfs->z_os);
	dmu_tx_hold_sa_create(tx, ZPL_FILECAP_RECORD_SIZE);
	dmu_tx_hold_sa(tx, zp->z_sa_hdl, B_TRUE);
	error = dmu_tx_assign(tx, DMU_TX_WAIT);
	if (error != 0) {
		dmu_tx_abort(tx);
		return (-error);
	}

	error = sa_update(zp->z_sa_hdl,
	    SA_ZPL_VPSADMINOS_FILECAP(zfsvfs), record,
	    ZPL_FILECAP_RECORD_SIZE, tx);
	dmu_tx_commit(tx);
	if (error != 0)
		return (-error);

	/*
	 * The public xattr has its own ZIL path.  Sync the internal authority
	 * state before changing that xattr so replay can never expose a mapped
	 * value without the tombstone which precedes it.
	 */
	txg_wait_synced(dmu_objset_pool(zfsvfs->z_os), 0);
	return (0);
}

static int
__zpl_filecap_raw_get(struct inode *ip, void *value, size_t size, cred_t *cr)
{
	int raw_size;

	/* Match ordinary xattr lookup when both backends contain a value. */
	raw_size = __zpl_xattr_get(ip, XATTR_NAME_CAPS, NULL, 0, cr);
	if (raw_size < 0)
		return (raw_size);
	if (raw_size > XATTR_CAPS_SZ_3)
		return (-EIO);
	if (value == NULL || size == 0)
		return (raw_size);
	if (size < raw_size)
		return (-ERANGE);
	if (__zpl_xattr_get(ip, XATTR_NAME_CAPS, value, size, cr) != raw_size)
		return (-EIO);
	return (raw_size);
}

static int
zpl_filecap_copyout(const uint8_t *source, size_t source_size,
    void *value, size_t size)
{
	if (value == NULL || size == 0)
		return (source_size);
	if (size < source_size)
		return (-ERANGE);
	memcpy(value, source, source_size);
	return (source_size);
}

static int
__zpl_filecap_get(struct inode *ip, void *value, size_t size, cred_t *cr)
{
	znode_t *zp = ITOZ(ip);
	zfsvfs_t *zfsvfs = ZTOZSB(zp);
	uint8_t record[ZPL_FILECAP_RECORD_SIZE];
	uint8_t raw[XATTR_CAPS_SZ_3];
	uint8_t mapped[XATTR_CAPS_SZ_3];
	uint32_t state, record_size, root;
	uint64_t host_root;
	int error, raw_size;

	ASSERT(RW_LOCK_HELD(&zp->z_xattr_lock));
	error = zpl_filecap_record_get(zp, record);
	if (error == -ENODATA)
		return (__zpl_filecap_raw_get(ip, value, size, cr));
	if (error != 0)
		return (error);

	state = zpl_filecap_get_le32(record + ZPL_FILECAP_STATE_OFFSET);
	if (state == ZPL_FILECAP_TOMBSTONE)
		return (-ENODATA);

	raw_size = __zpl_filecap_raw_get(ip, raw, sizeof (raw), cr);
	if (raw_size < 0)
		return (raw_size);

	record_size = zpl_filecap_get_le32(record + ZPL_FILECAP_SIZE_OFFSET);
	if (raw_size != record_size || memcmp(raw,
	    record + ZPL_FILECAP_VALUE_OFFSET, raw_size) != 0 ||
	    state == ZPL_FILECAP_PASSTHROUGH)
		return (zpl_filecap_copyout(raw, raw_size, value, size));

	root = zpl_filecap_get_le32(record + ZPL_FILECAP_ROOT_OFFSET);
	error = zfs_ugid_map_ns_to_host(zfsvfs->z_uid_map, root, &host_root);
	if (error != 0 || host_root >= UINT32_MAX)
		return (zpl_filecap_copyout(raw, raw_size, value, size));

	memcpy(mapped, raw, raw_size);
	zpl_filecap_put_le32(mapped + XATTR_CAPS_SZ_2, host_root);
	return (zpl_filecap_copyout(mapped, raw_size, value, size));
}

static int
zpl_filecap_get(struct inode *ip, void *value, size_t size)
{
	znode_t *zp = ITOZ(ip);
	zfsvfs_t *zfsvfs = ZTOZSB(zp);
	cred_t *cr = CRED();
	fstrans_cookie_t cookie;
	int error;

	crhold(cr);
	cookie = spl_fstrans_mark();
	if ((error = zpl_enter_verify_zp(zfsvfs, zp, FTAG)) != 0)
		goto out;
	rw_enter(&zp->z_xattr_lock, RW_READER);
	error = __zpl_filecap_get(ip, value, size, cr);
	rw_exit(&zp->z_xattr_lock);
	zpl_exit(zfsvfs, FTAG);
out:
	spl_fstrans_unmark(cookie);
	crfree(cr);
	return (error);
}

static int
__zpl_filecap_set(zidmap_t *mnt_ns, struct inode *ip, const void *value,
    size_t size, int flags, cred_t *cr)
{
	znode_t *zp = ITOZ(ip);
	zfsvfs_t *zfsvfs = ZTOZSB(zp);
	uint8_t old_record[ZPL_FILECAP_RECORD_SIZE];
	uint8_t tombstone[ZPL_FILECAP_RECORD_SIZE];
	uint8_t final_record[ZPL_FILECAP_RECORD_SIZE];
	uint8_t raw[XATTR_CAPS_SZ_3];
	uint64_t canonical_root = 0;
	uint64_t host_root, roundtrip;
	enum zpl_filecap_state final_state = ZPL_FILECAP_PASSTHROUGH;
	boolean_t record_exists;
	boolean_t logical_exists;
	int error, logical_size, raw_size;

	ASSERT(RW_WRITE_HELD(&zp->z_xattr_lock));
	error = zpl_filecap_record_get(zp, old_record);
	record_exists = (error == 0);
	if (error != 0 && error != -ENODATA)
		return (error);

	if (zfsvfs->z_uid_map == NULL && !record_exists)
		return (__zpl_xattr_set_locked(ip, XATTR_NAME_CAPS, value,
		    size, flags, mnt_ns, cr));

	logical_size = __zpl_filecap_get(ip, NULL, 0, cr);
	logical_exists = (logical_size >= 0);
	if (logical_size < 0 && logical_size != -ENODATA)
		return (logical_size);
	if ((flags & XATTR_CREATE) && logical_exists)
		return (-EEXIST);
	if ((flags & XATTR_REPLACE) && !logical_exists)
		return (-ENODATA);
	if (value == NULL && !logical_exists && !record_exists)
		return (0);

	if (value != NULL) {
		if (!zpl_filecap_value_valid(value, size))
			return (-EINVAL);
		if (size == XATTR_CAPS_SZ_3 && zfsvfs->z_uid_map != NULL) {
			host_root = zpl_filecap_get_le32(
			    (const uint8_t *)value + XATTR_CAPS_SZ_2);
			error = zfs_ugid_map_host_to_ns_strict(
			    zfsvfs->z_uid_map, host_root, &canonical_root);
			if (error == 0) {
				error = zfs_ugid_map_ns_to_host(
				    zfsvfs->z_uid_map, canonical_root,
				    &roundtrip);
				if (error != 0 || roundtrip != host_root)
					return (-EOVERFLOW);
				final_state = ZPL_FILECAP_MAPPED;
			}
		}
		zpl_filecap_record_build(final_record, final_state, value,
		    size, canonical_root);
	}

	/* A synced tombstone makes every interrupted update fail closed. */
	zpl_filecap_record_build(tombstone, ZPL_FILECAP_TOMBSTONE,
	    NULL, 0, 0);
	error = zpl_filecap_record_write(zp, tombstone);
	if (error != 0)
		return (error);

	error = __zpl_xattr_set_locked(ip, XATTR_NAME_CAPS, value, size, 0,
	    mnt_ns, cr);
	if (error != 0)
		return (error);

	raw_size = __zpl_filecap_raw_get(ip, raw, sizeof (raw), cr);
	if (value == NULL) {
		if (raw_size == -ENODATA)
			return (0);
		return (raw_size < 0 ? raw_size : -EIO);
	}

	if (raw_size != size || memcmp(raw, value, size) != 0)
		return (raw_size < 0 ? raw_size : -EIO);

	error = zpl_filecap_record_write(zp, final_record);
	if (error != 0)
		(void) __zpl_xattr_set_locked(ip, XATTR_NAME_CAPS, NULL, 0,
		    0, mnt_ns, cr);
	return (error);
}

static int
zpl_filecap_set(zidmap_t *mnt_ns, struct inode *ip, const void *value,
    size_t size, int flags)
{
	znode_t *zp = ITOZ(ip);
	zfsvfs_t *zfsvfs = ZTOZSB(zp);
	cred_t *cr = CRED();
	fstrans_cookie_t cookie;
	int error;

	crhold(cr);
	cookie = spl_fstrans_mark();
	if ((error = zpl_enter_verify_zp(zfsvfs, zp, FTAG)) != 0)
		goto out;
	rw_enter(&zp->z_xattr_lock, RW_WRITER);
	error = __zpl_filecap_set(mnt_ns, ip, value, size, flags, cr);
	rw_exit(&zp->z_xattr_lock);
	zpl_exit(zfsvfs, FTAG);
out:
	spl_fstrans_unmark(cookie);
	crfree(cr);
	return (error);
}

/*
 * Extended security attributes
 *
 * "The security attribute namespace is used by kernel security modules,
 * such as Security Enhanced Linux, and also to implement file
 * capabilities (see capabilities(7)).  Read and write access
 * permissions to security attributes depend on the policy implemented
 * for each security attribute by the security module.  When no security
 * module is loaded, all processes have read access to extended security
 * attributes, and write access is limited to processes that have the
 * CAP_SYS_ADMIN capability." - xattr(7)
 */
static int
__zpl_xattr_security_list(struct inode *ip, char *list, size_t list_size,
    const char *name, size_t name_len)
{
	return (1);
}
ZPL_XATTR_LIST_WRAPPER(zpl_xattr_security_list);

static int
__zpl_xattr_security_get(struct inode *ip, const char *name,
    void *value, size_t size)
{
	char *xattr_name;
	int error;

	if (strcmp(name, XATTR_CAPS_SUFFIX) == 0)
		return (zpl_filecap_get(ip, value, size));

	/* xattr_resolve_name will do this for us if this is defined */
	xattr_name = kmem_asprintf("%s%s", XATTR_SECURITY_PREFIX, name);
	error = zpl_xattr_get(ip, xattr_name, value, size);
	kmem_strfree(xattr_name);

	return (error);
}
ZPL_XATTR_GET_WRAPPER(zpl_xattr_security_get);

static int
__zpl_xattr_security_set(zidmap_t *user_ns,
    struct inode *ip, const char *name,
    const void *value, size_t size, int flags)
{
	char *xattr_name;
	int error;

	if (strcmp(name, XATTR_CAPS_SUFFIX) == 0)
		return (zpl_filecap_set(user_ns, ip, value, size, flags));

	/* xattr_resolve_name will do this for us if this is defined */
	xattr_name = kmem_asprintf("%s%s", XATTR_SECURITY_PREFIX, name);
	error = zpl_xattr_set(ip, xattr_name, value, size, flags, user_ns);
	kmem_strfree(xattr_name);

	return (error);
}
ZPL_XATTR_SET_WRAPPER(zpl_xattr_security_set);

static int
zpl_xattr_security_init_impl(struct inode *ip, const struct xattr *xattrs,
    void *fs_info)
{
	zidmap_t *mnt_ns = fs_info != NULL ? fs_info : zfs_init_idmap;
	const struct xattr *xattr;
	int error = 0;

	for (xattr = xattrs; xattr->name != NULL; xattr++) {
		error = __zpl_xattr_security_set(mnt_ns, ip,
		    xattr->name, xattr->value, xattr->value_len, 0);

		if (error < 0)
			break;
	}

	return (error);
}

int
zpl_xattr_security_init(struct inode *ip, struct inode *dip,
    const struct qstr *qstr, zidmap_t *mnt_ns)
{
	return security_inode_init_security(ip, dip, qstr,
	    &zpl_xattr_security_init_impl, mnt_ns);
}

/*
 * Security xattr namespace handlers.
 */
static xattr_handler_t zpl_xattr_security_handler = {
	.prefix	= XATTR_SECURITY_PREFIX,
	.list	= zpl_xattr_security_list,
	.get	= zpl_xattr_security_get,
	.set	= zpl_xattr_security_set,
};

/*
 * Extended system attributes
 *
 * "Extended system attributes are used by the kernel to store system
 * objects such as Access Control Lists.  Read and write access permissions
 * to system attributes depend on the policy implemented for each system
 * attribute implemented by filesystems in the kernel." - xattr(7)
 */
#ifdef CONFIG_FS_POSIX_ACL
static int
zpl_set_acl_impl(zidmap_t *mnt_ns, struct inode *ip, struct posix_acl *acl,
    int type)
{
	char *name, *value = NULL;
	int error = 0;
	size_t size = 0;
	zfsvfs_t *zfsvfs;

	if (S_ISLNK(ip->i_mode))
		return (-EOPNOTSUPP);

	switch (type) {
	case ACL_TYPE_ACCESS:
		name = XATTR_NAME_POSIX_ACL_ACCESS;
		if (acl) {
			umode_t mode = ip->i_mode;
			error = posix_acl_equiv_mode(acl, &mode);
			if (error < 0) {
				return (error);
			} else {
				/*
				 * The mode bits will have been set by
				 * ->zfs_setattr()->zfs_acl_chmod_setattr()
				 * using the ZFS ACL conversion.  If they
				 * differ from the Posix ACL conversion dirty
				 * the inode to write the Posix mode bits.
				 */
				if (ip->i_mode != mode) {
					ip->i_mode = ITOZ(ip)->z_mode = mode;
					zpl_inode_set_ctime_to_ts(ip,
					    current_time(ip));
					zfs_mark_inode_dirty(ip);
				}

				if (error == 0)
					acl = NULL;
			}
		}
		break;

	case ACL_TYPE_DEFAULT:
		name = XATTR_NAME_POSIX_ACL_DEFAULT;
		if (!S_ISDIR(ip->i_mode))
			return (acl ? -EACCES : 0);
		break;

	default:
		return (-EINVAL);
	}

	if (acl) {
		size = posix_acl_xattr_size(acl->a_count);
		value = kmem_alloc(size, KM_SLEEP);

		zfsvfs = ITOZSB(ip);
		error = zpl_acl_to_xattr_map(zfsvfs->z_uid_map,
		    zfsvfs->z_gid_map, acl, value, size);
		if (error < 0) {
			kmem_free(value, size);
			return (error);
		}
	}

	error = zpl_xattr_set(ip, name, value, size, 0, mnt_ns);
	if (value)
		kmem_free(value, size);

	if (!error) {
		if (acl) {
			/*
			 * With uid/gid mappings, userspace can provide ACL
			 * IDs in namespace form and expects mapped IDs on
			 * readback. Caching the raw ACL can bypass mapping on
			 * subsequent reads, so force re-read from xattr.
			 */
			if (zfsvfs->z_uid_map != NULL ||
			    zfsvfs->z_gid_map != NULL)
				forget_cached_acl(ip, type);
			else
				set_cached_acl(ip, type, acl);
		} else {
			forget_cached_acl(ip, type);
		}
	}

	return (error);
}

int
#ifdef HAVE_SET_ACL_USERNS
zpl_set_acl(struct user_namespace *userns, struct inode *ip,
    struct posix_acl *acl, int type)
#elif defined(HAVE_SET_ACL_IDMAP_DENTRY)
zpl_set_acl(struct mnt_idmap *userns, struct dentry *dentry,
    struct posix_acl *acl, int type)
#elif defined(HAVE_SET_ACL_USERNS_DENTRY_ARG2)
zpl_set_acl(struct user_namespace *userns, struct dentry *dentry,
    struct posix_acl *acl, int type)
#else
zpl_set_acl(struct inode *ip, struct posix_acl *acl, int type)
#endif /* HAVE_SET_ACL_USERNS */
{
#ifdef HAVE_SET_ACL_USERNS
	return (zpl_set_acl_impl(userns, ip, acl, type));
#elif defined(HAVE_SET_ACL_IDMAP_DENTRY)
	return (zpl_set_acl_impl(userns, d_inode(dentry), acl, type));
#elif defined(HAVE_SET_ACL_USERNS_DENTRY_ARG2)
	return (zpl_set_acl_impl(userns, d_inode(dentry), acl, type));
#else
	return (zpl_set_acl_impl(zfs_init_idmap, ip, acl, type));
#endif /* HAVE_SET_ACL_USERNS */
}

static struct posix_acl *
zpl_get_acl_impl(struct inode *ip, int type)
{
	struct posix_acl *acl;
	void *value = NULL;
	char *name;
	zfsvfs_t *zfsvfs;

	switch (type) {
	case ACL_TYPE_ACCESS:
		name = XATTR_NAME_POSIX_ACL_ACCESS;
		break;
	case ACL_TYPE_DEFAULT:
		name = XATTR_NAME_POSIX_ACL_DEFAULT;
		break;
	default:
		return (ERR_PTR(-EINVAL));
	}

	int size = zpl_xattr_get(ip, name, NULL, 0);
	if (size > 0) {
		value = kmem_alloc(size, KM_SLEEP);
		size = zpl_xattr_get(ip, name, value, size);
	}

	if (size > 0) {
		zfsvfs = ITOZSB(ip);
		acl = zpl_acl_from_xattr_map(zfsvfs->z_uid_map,
		    zfsvfs->z_gid_map, value, size);
	} else if (size == -ENODATA || size == -ENOSYS) {
		acl = NULL;
	} else {
		acl = ERR_PTR(-EIO);
	}

	if (size > 0)
		kmem_free(value, size);

	return (acl);
}

#if defined(HAVE_GET_ACL_IDMAP_DENTRY)
struct posix_acl *
zpl_get_acl(struct mnt_idmap *idmap, struct dentry *dentry, int type)
{
	/*
	 * Linux passes the mount idmap at the callback boundary on modern
	 * kernels, but the ACL itself is still stored in the filesystem view.
	 */
	(void) idmap;

	return (zpl_get_acl_impl(d_inode(dentry), type));
}
#endif

#if defined(HAVE_GET_ACL_RCU)
struct posix_acl *
zpl_get_acl(struct inode *ip, int type, bool rcu)
{
	if (rcu)
		return (ERR_PTR(-ECHILD));

	return (zpl_get_acl_impl(ip, type));
}
#elif defined(HAVE_GET_ACL)
struct posix_acl *
zpl_get_acl(struct inode *ip, int type)
{
	return (zpl_get_acl_impl(ip, type));
}
#endif

#if defined(HAVE_GET_INODE_ACL)
struct posix_acl *
zpl_get_inode_acl(struct inode *ip, int type, bool rcu)
{
	if (rcu)
		return (ERR_PTR(-ECHILD));

	return (zpl_get_acl_impl(ip, type));
}
#endif

#if !(defined(HAVE_GET_ACL_IDMAP_DENTRY) || defined(HAVE_GET_ACL_RCU) || \
    defined(HAVE_GET_ACL) || defined(HAVE_GET_INODE_ACL))
#error "Unsupported iops->get_acl() implementation"
#endif

int
zpl_init_acl(zidmap_t *mnt_ns, struct inode *ip, struct inode *dir)
{
	struct posix_acl *acl = NULL;
	int error = 0;

	if (ITOZSB(ip)->z_acl_type != ZFS_ACLTYPE_POSIX)
		return (0);

	if (!S_ISLNK(ip->i_mode)) {
		acl = zpl_get_acl_impl(dir, ACL_TYPE_DEFAULT);
		if (IS_ERR(acl))
			return (PTR_ERR(acl));
		if (!acl) {
			ITOZ(ip)->z_mode = (ip->i_mode &= ~current_umask());
			zpl_inode_set_ctime_to_ts(ip, current_time(ip));
			zfs_mark_inode_dirty(ip);
			return (0);
		}
	}

	if (acl) {
		umode_t mode;

		if (S_ISDIR(ip->i_mode)) {
			error = zpl_set_acl_impl(mnt_ns, ip, acl,
			    ACL_TYPE_DEFAULT);
			if (error)
				goto out;
		}

		mode = ip->i_mode;
		error = __posix_acl_create(&acl, GFP_KERNEL, &mode);
		if (error >= 0) {
			ip->i_mode = ITOZ(ip)->z_mode = mode;
			zfs_mark_inode_dirty(ip);
			if (error > 0) {
				error = zpl_set_acl_impl(mnt_ns, ip, acl,
				    ACL_TYPE_ACCESS);
			}
		}
	}
out:
	zpl_posix_acl_release(acl);

	return (error);
}

int
zpl_chmod_acl(zidmap_t *mnt_ns, struct inode *ip)
{
	struct posix_acl *acl;
	int error;

	if (ITOZSB(ip)->z_acl_type != ZFS_ACLTYPE_POSIX)
		return (0);

	if (S_ISLNK(ip->i_mode))
		return (-EOPNOTSUPP);

	acl = zpl_get_acl_impl(ip, ACL_TYPE_ACCESS);
	if (IS_ERR(acl) || !acl)
		return (PTR_ERR(acl));

	error = __posix_acl_chmod(&acl, GFP_KERNEL, ip->i_mode);
	if (!error)
		error = zpl_set_acl_impl(mnt_ns, ip, acl, ACL_TYPE_ACCESS);

	zpl_posix_acl_release(acl);

	return (error);
}

static int
__zpl_xattr_acl_list_access(struct inode *ip, char *list, size_t list_size,
    const char *name, size_t name_len)
{
	char *xattr_name = XATTR_NAME_POSIX_ACL_ACCESS;
	size_t xattr_size = sizeof (XATTR_NAME_POSIX_ACL_ACCESS);

	if (ITOZSB(ip)->z_acl_type != ZFS_ACLTYPE_POSIX)
		return (0);

	if (list && xattr_size <= list_size)
		memcpy(list, xattr_name, xattr_size);

	return (xattr_size);
}
ZPL_XATTR_LIST_WRAPPER(zpl_xattr_acl_list_access);

static int
__zpl_xattr_acl_list_default(struct inode *ip, char *list, size_t list_size,
    const char *name, size_t name_len)
{
	char *xattr_name = XATTR_NAME_POSIX_ACL_DEFAULT;
	size_t xattr_size = sizeof (XATTR_NAME_POSIX_ACL_DEFAULT);

	if (ITOZSB(ip)->z_acl_type != ZFS_ACLTYPE_POSIX)
		return (0);

	if (list && xattr_size <= list_size)
		memcpy(list, xattr_name, xattr_size);

	return (xattr_size);
}
ZPL_XATTR_LIST_WRAPPER(zpl_xattr_acl_list_default);

static int
__zpl_xattr_acl_get_access(struct inode *ip, const char *name,
    void *buffer, size_t size)
{
	struct posix_acl *acl;
	zfsvfs_t *zfsvfs;
	int type = ACL_TYPE_ACCESS;
	int error;
	/* xattr_resolve_name will do this for us if this is defined */
	if (ITOZSB(ip)->z_acl_type != ZFS_ACLTYPE_POSIX)
		return (-EOPNOTSUPP);

	acl = zpl_get_acl_impl(ip, type);
	if (IS_ERR(acl))
		return (PTR_ERR(acl));
	if (acl == NULL)
		return (-ENODATA);

	zfsvfs = ITOZSB(ip);
	error = zpl_acl_to_xattr_map(zfsvfs->z_uid_map,
	    zfsvfs->z_gid_map, acl, buffer, size);
	zpl_posix_acl_release(acl);

	return (error);
}
ZPL_XATTR_GET_WRAPPER(zpl_xattr_acl_get_access);

static int
__zpl_xattr_acl_get_default(struct inode *ip, const char *name,
    void *buffer, size_t size)
{
	struct posix_acl *acl;
	zfsvfs_t *zfsvfs;
	int type = ACL_TYPE_DEFAULT;
	int error;
	/* xattr_resolve_name will do this for us if this is defined */
	if (ITOZSB(ip)->z_acl_type != ZFS_ACLTYPE_POSIX)
		return (-EOPNOTSUPP);

	acl = zpl_get_acl_impl(ip, type);
	if (IS_ERR(acl))
		return (PTR_ERR(acl));
	if (acl == NULL)
		return (-ENODATA);

	zfsvfs = ITOZSB(ip);
	error = zpl_acl_to_xattr_map(zfsvfs->z_uid_map,
	    zfsvfs->z_gid_map, acl, buffer, size);
	zpl_posix_acl_release(acl);

	return (error);
}
ZPL_XATTR_GET_WRAPPER(zpl_xattr_acl_get_default);

static int
__zpl_xattr_acl_set_access(zidmap_t *mnt_ns,
    struct inode *ip, const char *name,
    const void *value, size_t size, int flags)
{
	struct posix_acl *acl;
	int type = ACL_TYPE_ACCESS;
	int error = 0;
	/* xattr_resolve_name will do this for us if this is defined */
	if (ITOZSB(ip)->z_acl_type != ZFS_ACLTYPE_POSIX)
		return (-EOPNOTSUPP);

#if defined(HAVE_XATTR_SET_USERNS) || defined(HAVE_XATTR_SET_IDMAP)
	if (!zpl_inode_owner_or_capable(mnt_ns, ip))
		return (-EPERM);
#else
	(void) mnt_ns;
	if (!zpl_inode_owner_or_capable(zfs_init_idmap, ip))
		return (-EPERM);
#endif

	if (value) {
		struct user_namespace userns;

		acl = zpl_acl_from_xattr_idmap(mnt_ns, value, size);
		if (IS_ERR(acl))
			return (PTR_ERR(acl));
		else if (acl) {
			error = posix_acl_valid(
			    zfs_idmap_userns(mnt_ns, &userns), acl);
			if (error) {
				zpl_posix_acl_release(acl);
				return (error);
			}
		}
	} else {
		acl = NULL;
	}
	error = zpl_set_acl_impl(mnt_ns, ip, acl, type);
	zpl_posix_acl_release(acl);

	return (error);
}
ZPL_XATTR_SET_WRAPPER(zpl_xattr_acl_set_access);

static int
__zpl_xattr_acl_set_default(zidmap_t *mnt_ns,
    struct inode *ip, const char *name,
    const void *value, size_t size, int flags)
{
	struct posix_acl *acl;
	int type = ACL_TYPE_DEFAULT;
	int error = 0;
	/* xattr_resolve_name will do this for us if this is defined */
	if (ITOZSB(ip)->z_acl_type != ZFS_ACLTYPE_POSIX)
		return (-EOPNOTSUPP);

#if defined(HAVE_XATTR_SET_USERNS) || defined(HAVE_XATTR_SET_IDMAP)
	if (!zpl_inode_owner_or_capable(mnt_ns, ip))
		return (-EPERM);
#else
	(void) mnt_ns;
	if (!zpl_inode_owner_or_capable(zfs_init_idmap, ip))
		return (-EPERM);
#endif

	if (value) {
		struct user_namespace userns;

		acl = zpl_acl_from_xattr_idmap(mnt_ns, value, size);
		if (IS_ERR(acl))
			return (PTR_ERR(acl));
		else if (acl) {
			error = posix_acl_valid(
			    zfs_idmap_userns(mnt_ns, &userns), acl);
			if (error) {
				zpl_posix_acl_release(acl);
				return (error);
			}
		}
	} else {
		acl = NULL;
	}

	error = zpl_set_acl_impl(mnt_ns, ip, acl, type);
	zpl_posix_acl_release(acl);

	return (error);
}
ZPL_XATTR_SET_WRAPPER(zpl_xattr_acl_set_default);

/*
 * ACL access xattr namespace handlers.
 *
 * Use .name instead of .prefix when available. xattr_resolve_name will match
 * whole name and reject anything that has .name only as prefix.
 */
static xattr_handler_t zpl_xattr_acl_access_handler = {
	.name	= XATTR_NAME_POSIX_ACL_ACCESS,
	.list	= zpl_xattr_acl_list_access,
	.get	= zpl_xattr_acl_get_access,
	.set	= zpl_xattr_acl_set_access,
	.flags	= ACL_TYPE_ACCESS,
};

/*
 * ACL default xattr namespace handlers.
 *
 * Use .name instead of .prefix. xattr_resolve_name will match whole name and
 * reject anything that has .name only as prefix.
 */
static xattr_handler_t zpl_xattr_acl_default_handler = {
	.name	= XATTR_NAME_POSIX_ACL_DEFAULT,
	.list	= zpl_xattr_acl_list_default,
	.get	= zpl_xattr_acl_get_default,
	.set	= zpl_xattr_acl_set_default,
	.flags	= ACL_TYPE_DEFAULT,
};

#endif /* CONFIG_FS_POSIX_ACL */

xattr_handler_t *zpl_xattr_handlers[] = {
	&zpl_xattr_security_handler,
	&zpl_xattr_trusted_handler,
	&zpl_xattr_user_handler,
#ifdef CONFIG_FS_POSIX_ACL
	&zpl_xattr_acl_access_handler,
	&zpl_xattr_acl_default_handler,
#endif /* CONFIG_FS_POSIX_ACL */
	NULL
};

static const struct xattr_handler *
zpl_xattr_handler(const char *name)
{
	if (strncmp(name, XATTR_USER_PREFIX,
	    XATTR_USER_PREFIX_LEN) == 0)
		return (&zpl_xattr_user_handler);

	if (strncmp(name, XATTR_TRUSTED_PREFIX,
	    XATTR_TRUSTED_PREFIX_LEN) == 0)
		return (&zpl_xattr_trusted_handler);

	if (strncmp(name, XATTR_SECURITY_PREFIX,
	    XATTR_SECURITY_PREFIX_LEN) == 0)
		return (&zpl_xattr_security_handler);

#ifdef CONFIG_FS_POSIX_ACL
	if (strncmp(name, XATTR_NAME_POSIX_ACL_ACCESS,
	    sizeof (XATTR_NAME_POSIX_ACL_ACCESS)) == 0)
		return (&zpl_xattr_acl_access_handler);

	if (strncmp(name, XATTR_NAME_POSIX_ACL_DEFAULT,
	    sizeof (XATTR_NAME_POSIX_ACL_DEFAULT)) == 0)
		return (&zpl_xattr_acl_default_handler);
#endif /* CONFIG_FS_POSIX_ACL */

	return (NULL);
}

static enum xattr_permission
zpl_xattr_permission(xattr_filldir_t *xf, const char *name, int name_len)
{
	const struct xattr_handler *handler;
	struct dentry *d __maybe_unused = xf->dentry;
	enum xattr_permission perm = XAPERM_ALLOW;

	handler = zpl_xattr_handler(name);
	if (handler == NULL) {
		/* Do not expose FreeBSD system namespace xattrs. */
		if (ZFS_XA_NS_PREFIX_MATCH(FREEBSD, name))
			return (XAPERM_DENY);
		/*
		 * Anything that doesn't match a known namespace gets put in the
		 * user namespace for compatibility with other platforms.
		 */
		perm = XAPERM_COMPAT;
		handler = &zpl_xattr_user_handler;
	}

	if (handler->list) {
		if (!handler->list(d))
			return (XAPERM_DENY);
	}

	return (perm);
}

#ifdef CONFIG_FS_POSIX_ACL

struct acl_rel_struct {
	struct acl_rel_struct *next;
	struct posix_acl *acl;
	clock_t time;
};

#define	ACL_REL_GRACE	(60*HZ)
#define	ACL_REL_WINDOW	(1*HZ)
#define	ACL_REL_SCHED	(ACL_REL_GRACE+ACL_REL_WINDOW)

/*
 * Lockless multi-producer single-consumer fifo list.
 * Nodes are added to tail and removed from head. Tail pointer is our
 * synchronization point. It always points to the next pointer of the last
 * node, or head if list is empty.
 */
static struct acl_rel_struct *acl_rel_head = NULL;
static struct acl_rel_struct **acl_rel_tail = &acl_rel_head;

static void
zpl_posix_acl_free(void *arg)
{
	struct acl_rel_struct *freelist = NULL;
	struct acl_rel_struct *a;
	clock_t new_time;
	boolean_t refire = B_FALSE;

	ASSERT3P(acl_rel_head, !=, NULL);
	while (acl_rel_head) {
		a = acl_rel_head;
		if (ddi_get_lbolt() - a->time >= ACL_REL_GRACE) {
			/*
			 * If a is the last node we need to reset tail, but we
			 * need to use cmpxchg to make sure it is still the
			 * last node.
			 */
			if (acl_rel_tail == &a->next) {
				acl_rel_head = NULL;
				if (cmpxchg(&acl_rel_tail, &a->next,
				    &acl_rel_head) == &a->next) {
					ASSERT3P(a->next, ==, NULL);
					a->next = freelist;
					freelist = a;
					break;
				}
			}
			/*
			 * a is not last node, make sure next pointer is set
			 * by the adder and advance the head.
			 */
			while (READ_ONCE(a->next) == NULL)
				cpu_relax();
			acl_rel_head = a->next;
			a->next = freelist;
			freelist = a;
		} else {
			/*
			 * a is still in grace period. We are responsible to
			 * reschedule the free task, since adder will only do
			 * so if list is empty.
			 */
			new_time = a->time + ACL_REL_SCHED;
			refire = B_TRUE;
			break;
		}
	}

	if (refire)
		taskq_dispatch_delay(system_delay_taskq, zpl_posix_acl_free,
		    NULL, TQ_SLEEP, new_time);

	while (freelist) {
		a = freelist;
		freelist = a->next;
		kfree(a->acl);
		kmem_free(a, sizeof (struct acl_rel_struct));
	}
}

void
zpl_posix_acl_release_impl(struct posix_acl *acl)
{
	struct acl_rel_struct *a, **prev;

	a = kmem_alloc(sizeof (struct acl_rel_struct), KM_SLEEP);
	a->next = NULL;
	a->acl = acl;
	a->time = ddi_get_lbolt();
	/* atomically points tail to us and get the previous tail */
	prev = xchg(&acl_rel_tail, &a->next);
	ASSERT3P(*prev, ==, NULL);
	*prev = a;
	/* if it was empty before, schedule the free task */
	if (prev == &acl_rel_head)
		taskq_dispatch_delay(system_delay_taskq, zpl_posix_acl_free,
		    NULL, TQ_SLEEP, ddi_get_lbolt() + ACL_REL_SCHED);
}
#endif

ZFS_MODULE_PARAM(zfs, zfs_, xattr_compat, INT, ZMOD_RW,
	"Use legacy ZFS xattr naming for writing new user namespace xattrs");
