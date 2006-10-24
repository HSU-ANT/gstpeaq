mkdir -p aux
libtoolize
aclocal
autoheader
autoconf
automake --add-missing
CFLAGS="-Wall -Werror -g" ./configure --enable-maintainer-mode
