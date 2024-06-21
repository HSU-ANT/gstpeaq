git log --pretty --numstat --summary | git2cl >ChangeLog
libtoolize
gtkdocize
aclocal -I m4
autoheader
autoconf
automake --add-missing
CFLAGS="-Wall -g -DGST_DISABLE_DEPRECATED" ./configure --enable-gtk-doc --enable-man --prefix=/usr $@
