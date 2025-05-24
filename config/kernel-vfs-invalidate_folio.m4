dnl #
dnl # Linux 5.18+ uses invalidate_folio
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_VFS_INVALIDATE_FOLIO], [
        ZFS_LINUX_TEST_SRC([vfs_has_invalidate_folio], [
                #include <linux/fs.h>

                static const struct address_space_operations
                aops __attribute__ ((unused)) = {
                        .invalidate_folio = invalidate_folio,
                };
        ],[])
])

AC_DEFUN([ZFS_AC_KERNEL_VFS_INVALIDATE_FOLIO], [
        dnl #
        dnl # Check if address_space_operations has ->invalidate_folio
        dnl #
        AC_MSG_CHECKING([whether invalidate_folio exists])
        ZFS_LINUX_TEST_RESULT([vfs_has_invalidate_folio], [
                AC_MSG_RESULT([yes])
                AC_DEFINE([HAVE_VFS_INVALIDATE_FOLIO], [1],
                  [Define if struct address_space_operations has ->invalidate_folio])
        ],[
                AC_MSG_RESULT([no])
        ])
])
