plugin_LTLIBRARIES = libgstpeaq.la
bin_PROGRAMS = peaq 
check_PROGRAMS = testpeaq 
EXTRA_DIST = runtest-1.0.sh checkconformanceresults.sh
TESTS = testpeaq runtest-@GST_API_VERSION@.sh checkconformanceresults.sh
AM_TESTS_ENVIRONMENT=GSTLAUNCH=@GSTLAUNCH@; export GSTLAUNCH; CONFORMANCEDATADIR=@CONFORMANCEDATADIR@; export CONFORMANCEDATADIR;
noinst_HEADERS = gstpeaq.h peaqalgo.h earmodel.h leveladapter.h \
	modpatt.h fftearmodel.h fbearmodel.h movaccum.h movs.h nn.h settings.h
libgstpeaq_la_SOURCES = gstpeaq.c gstpeaqplugin.c peaqalgo.cc fbearmodel.cc movs.cc nn.cc
libgstpeaq_la_CFLAGS = @PKGCONF_CFLAGS@
libgstpeaq_la_CXXFLAGS = @PKGCONF_CFLAGS@
libgstpeaq_la_LIBADD = @PKGCONF_LIBS@
libgstpeaq_la_LDFLAGS = -module
peaq_SOURCES = peaq.c
peaq_CFLAGS = @PKGCONF_CFLAGS@
peaq_LDADD = @PKGCONF_BIN_LIBS@
testpeaq_SOURCES = testpeaq.cc fbearmodel.cc
testpeaq_CFLAGS = @PKGCONF_CFLAGS@
testpeaq_CXXFLAGS = @PKGCONF_CFLAGS@
testpeaq_LDADD = @PKGCONF_LIBS@
