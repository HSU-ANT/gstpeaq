mkdir -p aux
libtoolize
aclocal
autoheader
autoconf
automake --add-missing
CFLAGS="-Wall -g" ./configure --enable-maintainer-mode
