mkdir -p aux
libtoolize
gtkdocize
aclocal -I m4
autoheader
autoconf
automake --add-missing
CFLAGS="-Wall -Werror -g" ./configure --enable-maintainer-mode --enable-gtk-doc --prefix=/usr
