plugin_LTLIBRARIES = libgstpeaq.la
bin_PROGRAMS = peaq 
check_PROGRAMS = testpeaq 
EXTRA_DIST = runtest.sh
TESTS = testpeaq runtest.sh
AM_TESTS_ENVIRONMENT=GSTLAUNCH=@GSTLAUNCH@; export GSTLAUNCH;
noinst_HEADERS = gstpeaq.h earmodel.h leveladapter.h \
	modpatt.h fftearmodel.h fbearmodel.h movaccum.h movs.h nn.h settings.h
libgstpeaq_la_SOURCES = gstpeaq.c gstpeaqplugin.c earmodel.c \
	leveladapter.c modpatt.c fftearmodel.c fbearmodel.c movaccum.c movs.c nn.c
libgstpeaq_la_CFLAGS = @PKGCONF_CFLAGS@
libgstpeaq_la_LIBADD = @PKGCONF_LIBS@
libgstpeaq_la_LDFLAGS = -module
peaq_SOURCES = peaq.c
peaq_CFLAGS = @PKGCONF_CFLAGS@
peaq_LDADD = @PKGCONF_BIN_LIBS@
testpeaq_SOURCES = testpeaq.c earmodel.c leveladapter.c modpatt.c \
		   fftearmodel.c fbearmodel.c movaccum.c
testpeaq_CFLAGS = @PKGCONF_CFLAGS@
testpeaq_LDADD = @PKGCONF_LIBS@
