mkdir -p aux
libtoolize
aclocal
autoconf
automake --add-missing
./configure --enable-maintainer-mode
