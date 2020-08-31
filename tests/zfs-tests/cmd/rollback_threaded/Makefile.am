include $(top_srcdir)/config/Rules.am

pkgexecdir = $(datadir)/@PACKAGE@/zfs-tests/bin

DEFAULT_INCLUDES += \
	-I$(top_srcdir)/include

# Unconditionally enable ASSERTs
AM_CPPFLAGS += -DDEBUG -UNDEBUG -DZFS_DEBUG

pkgexec_PROGRAMS = rollback_threaded
rollback_threaded_SOURCES = \
	rollback_test_file.c \
	rollback_test_file.h \
	rollback_test_dir.h \
	rollback_test_dir.c \
	rollback_test_hardlink.h \
	rollback_test_hardlink.c \
	rollback_test_symlink.h \
	rollback_test_symlink.c \
	rollback_test_tmpfile.h \
	rollback_test_tmpfile.c \
	rollback_test_perm_user.h \
	rollback_test_perm_user.c \
	rollback_test_perm_group.h \
	rollback_test_perm_group.c \
	rollback_test_perm_xattr.h \
	rollback_test_perm_xattr.c \
	rollback_test_perm_xattr_dir.h \
	rollback_test_perm_xattr_dir.c \
	rollback_test_neg_dentries.h \
	rollback_test_neg_dentries.c \
	rollback_test_openfd.h \
	rollback_test_openfd.c \
	rollback_test_openfd_unlinked.h \
	rollback_test_openfd_unlinked.c \
	rollback_tests.c \
	rollback_threaded.h \
	rollback_threaded.c


rollback_threaded_LDADD = -lpthread \
	$(abs_top_builddir)/lib/libzfs/libzfs.la
