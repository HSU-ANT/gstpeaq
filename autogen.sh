mkdir -p aux
libtoolize
aclocal -I m4
autoheader
autoconf
automake --add-missing
CFLAGS="-Wall -Werror -g" ./configure --enable-maintainer-mode --prefix=/usr
