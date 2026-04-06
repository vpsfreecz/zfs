dnl #
dnl # Linux 5.12 API change,
dnl # inode_operations->fileattr_set() first took struct user_namespace *.
dnl #
dnl # Linux 6.3 API change,
dnl # inode_operations->fileattr_set() first arg became struct mnt_idmap *.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_FILEATTR_OPS], [
	ZFS_LINUX_TEST_SRC([inode_operations_fileattr_idmap], [
		#include <linux/fs.h>
		#include <linux/fileattr.h>

		typedef typeof(((struct inode_operations *)0)->fileattr_set)
		    fileattr_set_t;
		typedef typeof(((struct inode_operations *)0)->fileattr_get)
		    fileattr_get_t;

		static int fileattr_set_fn(struct mnt_idmap *idmap,
		    struct dentry *dentry, struct fileattr *fa)
		{ return 0; }
		static int fileattr_get_fn(struct dentry *dentry,
		    struct fileattr *fa)
		{ return 0; }

		fileattr_set_t fileattr_set_ptr __attribute__ ((unused)) =
		    fileattr_set_fn;
		fileattr_get_t fileattr_get_ptr __attribute__ ((unused)) =
		    fileattr_get_fn;

		char fileattr_set_match[
		    __builtin_types_compatible_p(fileattr_set_t,
		    int (*)(struct mnt_idmap *, struct dentry *,
		    struct fileattr *)) ? 1 : -1] __attribute__ ((unused));
		char fileattr_get_match[
		    __builtin_types_compatible_p(fileattr_get_t,
		    int (*)(struct dentry *, struct fileattr *)) ? 1 : -1]
		    __attribute__ ((unused));
	],[])

	ZFS_LINUX_TEST_SRC([inode_operations_fileattr_userns], [
		#include <linux/fs.h>
		#include <linux/fileattr.h>

		typedef typeof(((struct inode_operations *)0)->fileattr_set)
		    fileattr_set_t;
		typedef typeof(((struct inode_operations *)0)->fileattr_get)
		    fileattr_get_t;

		static int fileattr_set_fn(struct user_namespace *userns,
		    struct dentry *dentry, struct fileattr *fa)
		{ return 0; }
		static int fileattr_get_fn(struct dentry *dentry,
		    struct fileattr *fa)
		{ return 0; }

		fileattr_set_t fileattr_set_ptr __attribute__ ((unused)) =
		    fileattr_set_fn;
		fileattr_get_t fileattr_get_ptr __attribute__ ((unused)) =
		    fileattr_get_fn;

		char fileattr_set_match[
		    __builtin_types_compatible_p(fileattr_set_t,
		    int (*)(struct user_namespace *, struct dentry *,
		    struct fileattr *)) ? 1 : -1] __attribute__ ((unused));
		char fileattr_get_match[
		    __builtin_types_compatible_p(fileattr_get_t,
		    int (*)(struct dentry *, struct fileattr *)) ? 1 : -1]
		    __attribute__ ((unused));
	],[])
])

AC_DEFUN([ZFS_AC_KERNEL_FILEATTR_OPS], [
	AC_MSG_CHECKING([whether iops->fileattr_{get,set}() exist])
	ZFS_LINUX_TEST_RESULT([inode_operations_fileattr_idmap], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_FILEATTR_OPS, 1,
		    [inode_operations has fileattr_get/fileattr_set callbacks])
	],[
		ZFS_LINUX_TEST_RESULT([inode_operations_fileattr_userns], [
			AC_MSG_RESULT(yes)
			AC_DEFINE(HAVE_FILEATTR_OPS, 1,
			    [inode_operations has fileattr_get/fileattr_set callbacks])
		],[
			AC_MSG_RESULT(no)
		])
	])
])
