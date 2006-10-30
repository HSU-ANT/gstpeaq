ODG=`LANG=LC_ALL gst-launch-0.10 --gst-disable-segtrap --gst-debug-level=2 --gst-plugin-load=.libs/libgstpeaq.so \
	audiotestsrc name=src0 num-buffers=128 freq=440 \
	audioconvert name=convert0 \
	tee \
	queue name=queue0 \
	queue name=queue1 \
	peaq name=peaq0 \
	src0.src\!convert0.sink convert0.src\!tee0.sink \
	tee0.src2\!queue0.sink tee0.src1\!queue1.sink \
	queue0.src\!peaq0.ref queue1.src\!peaq0.test \
| grep "Objective Difference Grade:" | cut -d " " -f4`
echo $ODG
if [ x$ODG != x0.187 ]; then
	exit 1
fi
ODG=`LANG=LC_ALL gst-launch-0.10 --gst-disable-segtrap --gst-debug-level=2 --gst-plugin-load=.libs/libgstpeaq.so \
	audiotestsrc name=src0 num-buffers=128 wave=saw freq=440 \
	audiotestsrc name=src1 num-buffers=128 wave=triangle freq=440 \
	audioconvert name=convert0 \
	audioconvert name=convert1 \
	peaq name=peaq \
	src0.src\!convert0.sink src1.src\!convert1.sink \
	convert0.src\!peaq.ref convert1.src\!peaq.test \
| grep "Objective Difference Grade:" | cut -d " " -f4`
echo $ODG
if [ x$ODG != x-1.993 ]; then
	exit 1
fi
exit 0
