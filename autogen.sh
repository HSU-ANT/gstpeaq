mkdir -p aux
./makeChangeLog.sh
libtoolize
gtkdocize
aclocal -I m4
autoheader
autoconf
automake --add-missing
CFLAGS="-Wall -Werror -g -DGST_DISABLE_DEPRECATED" ./configure --enable-gtk-doc --prefix=/usr
