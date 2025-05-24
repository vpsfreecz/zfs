dnl #
dnl # Linux 5.19+ uses release_folio
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_VFS_RELEASE_FOLIO], [
        ZFS_LINUX_TEST_SRC([vfs_has_release_folio], [
                #include <linux/fs.h>

                static const struct address_space_operations
                aops __attribute__ ((unused)) = {
                        .release_folio = release_folio,
                };
        ],[])
])

AC_DEFUN([ZFS_AC_KERNEL_VFS_RELEASE_FOLIO], [
        dnl #
        dnl # Check if address_space_operations has ->release_folio
        dnl #
        AC_MSG_CHECKING([whether release_folio exists])
        ZFS_LINUX_TEST_RESULT([vfs_has_release_folio], [
                AC_MSG_RESULT([yes])
                AC_DEFINE([HAVE_VFS_RELEASE_FOLIO], [1],
                  [Define if struct address_space_operations has ->release_folio])
        ],[
                AC_MSG_RESULT([no])
        ])
])
