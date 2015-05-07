AC_PREREQ(2.60)

AC_INIT([gstpeaq], [0.4.1], [martin.holters@hsuhh.de])
AC_CONFIG_AUX_DIR([misc])
AC_CONFIG_MACRO_DIR([m4])
AM_INIT_AUTOMAKE([1.9 gnu])

AC_ARG_ENABLE(man, [AC_HELP_STRING([--enable-man],
	                           [regenerate man pages from Docbook [default=no]])],
              enable_man=yes,
              enable_man=no)
AM_CONDITIONAL(ENABLE_MAN, test x$enable_man != xno)

AC_SUBST(ACLOCAL_AMFLAGS, "-I m4")
GST_MAJORMINOR=0.10
AC_SUBST(GST_MAJORMINOR)

AM_PROG_CC_C_O
AC_DISABLE_STATIC
AC_PROG_LIBTOOL
AM_COND_IF([ENABLE_MAN], [AC_PATH_PROG([XSLTPROC], [xsltproc])])

AC_HEADER_STDC

AC_CHECK_LIB(m, main)

AC_FUNC_MALLOC

PKG_CHECK_MODULES(PKGCONF, [gstreamer-0.10 >= 0.10.14 gstreamer-base-0.10 >= 0.10.11 gstreamer-fft-0.10 >= 0.10.15])
PKG_CHECK_MODULES(PKGCONF_BIN, [gstreamer-0.10])

GST_SET_PLUGINDIR

GTK_DOC_CHECK([1.10],[--flavour no-tmpl])

AC_CONFIG_HEADER([config.h])
AC_CONFIG_FILES([Makefile src/Makefile doc/Makefile doc/man/Makefile])
AC_OUTPUT
