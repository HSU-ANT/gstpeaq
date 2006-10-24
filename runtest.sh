gst-launch-0.10 --gst-disable-segtrap --gst-debug-level=2 --gst-plugin-load=.libs/libgstpeaq.so \
	audiotestsrc name=src0 num-buffers=128 \
	audiotestsrc name=src1 num-buffers=128 \
	audioconvert name=convert0 \
	audioconvert name=convert1 \
	peaq name=peaq \
	src0.src\!convert0.sink src1.src\!convert1.sink \
	convert0.src\!peaq.ref convert1.src\!peaq.test
