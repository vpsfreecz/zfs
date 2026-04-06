dnl #
dnl # Linux 6.12 API change,
dnl # alloc_inode_sb() is available for superblock-scoped inode allocation.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_ALLOC_INODE_SB], [
	ZFS_LINUX_TEST_SRC([alloc_inode_sb], [
		#include <linux/fs.h>
		#include <linux/slab.h>

		static struct super_block *sb __attribute__ ((unused));
		static struct kmem_cache *cache __attribute__ ((unused));
		static void *obj __attribute__ ((unused));
	],[
		obj = alloc_inode_sb(sb, cache, GFP_KERNEL);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_ALLOC_INODE_SB], [
	AC_MSG_CHECKING([whether alloc_inode_sb() is available])
	ZFS_LINUX_TEST_RESULT([alloc_inode_sb], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_ALLOC_INODE_SB, 1,
		    [alloc_inode_sb() is available])
	],[
		AC_MSG_RESULT(no)
	])
])
