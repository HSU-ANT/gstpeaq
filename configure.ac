AC_PREREQ(2.60)

AC_INIT([GstPEAQ], [0.6], [martin.holters@hsu-hh.de], , [http://ant.hsu-hh.de/gstpeaq])
AC_CONFIG_AUX_DIR([misc])
AC_CONFIG_MACRO_DIR([m4])
AM_INIT_AUTOMAKE([1.9 gnu])

AC_ARG_ENABLE(man, [AC_HELP_STRING([--enable-man],
	                           [regenerate man pages from Docbook [default=no]])],
              enable_man=yes,
              enable_man=no)
AM_CONDITIONAL(ENABLE_MAN, test x$enable_man != xno)

AC_SUBST(ACLOCAL_AMFLAGS, "-I m4")

AM_PROG_CC_C_O
AC_DISABLE_STATIC
AC_PROG_LIBTOOL
AM_COND_IF([ENABLE_MAN], [AC_PATH_PROG([XSLTPROC], [xsltproc])])

AC_HEADER_STDC

AC_FUNC_MALLOC

AC_ARG_WITH([gstreamer],
	    [AC_HELP_STRING([--with-gstreamer=VERSION],
			    [choose specific GStreamer API version (0.10 or 1.0)])],
	    [],
	    [with_gstreamer="yes"])

gstreamer_0_10_packages="gstreamer-0.10 >= 0.10.14
			  gstreamer-base-0.10 >= 0.10.11
			  gstreamer-fft-0.10 >= 0.10.15"
gstreamer_1_0_packages="gstreamer-1.0 gstreamer-base-1.0 gstreamer-fft-1.0"
AS_IF([test "x$with_gstreamer" = "x0.10"],
      [PKG_CHECK_MODULES(PKGCONF, [$gstreamer_0_10_packages],
			 [GST_API_VERSION=0.10])],
      [test "x$with_gstreamer" = "x1.0"],
      [PKG_CHECK_MODULES(PKGCONF, [$gstreamer_1_0_packages],
			 [GST_API_VERSION=1.0])],
      [test "x$with_gstreamer" = "xyes"],
      [PKG_CHECK_MODULES(PKGCONF, [$gstreamer_1_0_packages],
			 [GST_API_VERSION=1.0],
			 [PKG_CHECK_MODULES(PKGCONF, [$gstreamer_0_10_packages],
					    [GST_API_VERSION=0.10])])],
      [test "x$with_gstreamer" = "xno"], [
       AC_ERROR([cannot build without GStreamer])],
      [AC_MSG_ERROR([GStreamer API version must be 0.10 or 1.0])])
AC_SUBST(GST_API_VERSION)
PKG_CHECK_MODULES(PKGCONF_BIN, [gstreamer-$GST_API_VERSION])

GST_SET_PLUGINDIR

AC_ARG_VAR(GSTLAUNCH, [name of the gst-launch tool])
AC_CHECK_PROGS(GSTLAUNCH, [gst-launch-$GST_API_VERSION gst-launch], [:])
AS_IF([test "x$GSTLAUNCH" = "x:"],
      [AC_WARN([gst-launch not found, cannot run all tests])])

AC_ARG_WITH([conformance-dataset],
	    [AC_HELP_STRING([--with-conformance-dataset=DIR],
			    [use BS.1367 conformance testing files at DIR])],
	    [],
	    [with_conformance_dataset="no"])
AS_IF([test "x$with_conformance_dataset" != xno],
      [CONFORMANCEDATADIR=`cd $with_conformance_dataset && pwd`
       AC_SUBST(CONFORMANCEDATADIR)])

GTK_DOC_CHECK([1.10],[--flavour no-tmpl])

AC_CONFIG_HEADER([config.h])
AC_CONFIG_FILES([Makefile src/Makefile doc/Makefile doc/man/Makefile])
AC_OUTPUT
