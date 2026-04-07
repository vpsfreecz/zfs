dnl # SPDX-License-Identifier: CDDL-1.0
dnl #
dnl # 2.6.38 API change
dnl # The .get_sb callback has been replaced by a .mount callback
dnl # in the file_system_type structure.
dnl #
dnl # 5.2 API change
dnl # The new fs_context-based filesystem API is introduced, with the old
dnl # one (via file_system_type.mount) preserved as a compatibility shim.
dnl #
dnl # 7.0 API change
dnl # Compatibility shim removed, so all callers must go through the mount API.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_FS_CONTEXT], [
	ZFS_LINUX_TEST_SRC([fs_context], [
		#include <linux/fs.h>
		#include <linux/fs_context.h>
        ],[
		static struct fs_context fs __attribute__ ((unused)) = { 0 };
		static struct fs_context *fsp __attribute__ ((unused));
		fsp = vfs_dup_fs_context(&fs);
	])

	ZFS_LINUX_TEST_SRC([fs_context_submount], [
		#include <linux/fs.h>
		#include <linux/fs_context.h>
		#include <linux/mount.h>

		static struct file_system_type *fs_type __attribute__ ((unused));
		static struct dentry *dentry __attribute__ ((unused));
		static struct fs_context *fc __attribute__ ((unused));
		static struct vfsmount *mnt __attribute__ ((unused));
		static char source_name[] __attribute__ ((unused)) = "zfs";
		static struct fs_parameter source_param __attribute__ ((unused)) = {
			.key = "source",
			.type = fs_value_is_string,
			.string = source_name,
			.size = 3,
		};
		static struct fs_parameter flag_param __attribute__ ((unused)) = {
			.key = "nosuid",
			.type = fs_value_is_flag,
		};
	],[
		fc = fs_context_for_submount(fs_type, dentry);
		(void) vfs_parse_fs_param_source(fc, &source_param);
		(void) vfs_parse_fs_param(fc, &flag_param);
		mnt = fc_mount(fc);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_FS_CONTEXT], [
        AC_MSG_CHECKING([whether fs_context exists])
        ZFS_LINUX_TEST_RESULT([fs_context], [
                AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_FS_CONTEXT, 1, [fs_context exists])
        ],[
		AC_MSG_RESULT(no)
		AC_MSG_ERROR([
	*** This kernel does not have `struct fs_context`. OpenZFS cannot be compiled.
		])
        ])

	AC_MSG_CHECKING([whether fs_context submount helpers are available])
	ZFS_LINUX_TEST_RESULT([fs_context_submount], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_FS_CONTEXT_FOR_SUBMOUNT, 1,
		    [fs_context_for_submount() is available])
		AC_DEFINE(HAVE_FC_MOUNT, 1, [fc_mount() is available])
	],[
		AC_MSG_RESULT(no)
	])
])
