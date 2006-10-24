lib_LTLIBRARIES = libgstpeaq.la
check_PROGRAMS = testpeaq 
EXTRA_DIST = runtest.sh
TESTS = testpeaq runtest.sh
noinst_HEADERS = fft.h gstpeaq.h earmodel.h earmodel_bands.h leveladapter.h
libgstpeaq_la_SOURCES = fft.c gstpeaq.c gstpeaqplugin.c earmodel.c \
	leveladapter.c
libgstpeaq_la_CFLAGS = @PKGCONF_CFLAGS@
libgstpeaq_la_LIBADD = @PKGCONF_LIBS@
testpeaq_SOURCES = testpeaq.c fft.c earmodel.c leveladapter.c
testpeaq_CFLAGS = @PKGCONF_CFLAGS@
testpeaq_LDADD = @PKGCONF_LIBS@
