if [ "$GSTLAUNCH" = ":" ]; then
	exit 77
fi

GSTLAUNCH=${GSTLAUNCH:-gst-launch-1.0}

ODG=`LC_ALL=C $GSTLAUNCH --gst-disable-segtrap --gst-debug-level=2 --gst-plugin-load=.libs/libgstpeaq.so \
	audiotestsrc name=src0 num-buffers=128 freq=440 \
	tee \
	queue name=queue0 \
	queue name=queue1 \
	peaq name=peaq0 \
	src0.src\!tee0.sink \
	tee0.src_2\!queue0.sink tee0.src_1\!queue1.sink \
	queue0.src\!peaq0.ref queue1.src\!peaq0.test \
| grep "Objective Difference Grade:" | cut -d " " -f4`
echo $ODG
if [ x$ODG != x0.171 ]; then
	exit 1
fi
ODG=`LC_ALL=C $GSTLAUNCH --gst-disable-segtrap --gst-debug-level=2 --gst-plugin-load=.libs/libgstpeaq.so \
	audiotestsrc name=src0 num-buffers=128 wave=saw freq=440 \
	audiotestsrc name=src1 num-buffers=128 wave=triangle freq=440 \
	peaq name=peaq \
	src0.src\!peaq.ref src1.src\!peaq.test \
| grep "Objective Difference Grade:" | cut -d " " -f4`
echo $ODG
if [ x$ODG != x-2.007 ]; then
	exit 1
fi
ODG=`LC_ALL=C $GSTLAUNCH --gst-disable-segtrap --gst-debug-level=2 --gst-plugin-load=.libs/libgstpeaq.so \
	audiotestsrc name=src0 num-buffers=128 wave=saw freq=440 \
	audiotestsrc name=src1 num-buffers=128 wave=triangle freq=440 \
	peaq name=peaq \
	src0.src\!'audio/x-raw,channels=2'\!peaq.ref src1.src\!peaq.test \
| grep "Objective Difference Grade:" | cut -d " " -f4`
echo $ODG
if [ x$ODG != x-2.007 ]; then
	exit 1
fi
ODG=`LC_ALL=C $GSTLAUNCH --gst-disable-segtrap --gst-debug-level=2 --gst-plugin-load=.libs/libgstpeaq.so \
	audiotestsrc name=src0 num-buffers=128 wave=saw freq=440 \
	audiotestsrc name=src1 num-buffers=128 wave=triangle freq=440 \
	peaq name=peaq \
	src0.src\!peaq.ref src1.src\!'audio/x-raw,channels=2'\!peaq.test \
| grep "Objective Difference Grade:" | cut -d " " -f4`
echo $ODG
if [ x$ODG != x-2.007 ]; then
	exit 1
fi

ODG=`LC_ALL=C ./peaq --gst-disable-segtrap --gst-debug-level=2 --gst-plugin-load=.libs/libgstpeaq.so \
	${srcdir}/../test/stimulus_ref.flac ${srcdir}/../test/stimulus_test.wav \
| grep "Objective Difference Grade:" | cut -d " " -f4`
echo $ODG
if [ x$ODG != x-1.077 ]; then
	exit 1
fi
ODG=`LC_ALL=C ./peaq --gst-disable-segtrap --gst-debug-level=2 --gst-plugin-load=.libs/libgstpeaq.so \
	${srcdir}/../test/stimulus_test.wav ${srcdir}/../test/stimulus_ref.flac \
| grep "Objective Difference Grade:" | cut -d " " -f4`
echo $ODG
if [ x$ODG != x-3.096 ]; then
	exit 1
fi

exit 0
