#!/bin/bash

runpeaq() {
	CODFILE=$1
	REFFILE=${CODFILE/cod/ref}
	ODG=`LANG=LC_ALL ./peaq --gst-disable-segtrap --gst-debug-level=2 --gst-plugin-load=.libs/libgstpeaq.so \
		"${REFFILE}" "$CODFILE" \
		| grep "Objective Difference Grade:" | cut -d " " -f4`
	DELTA=`echo $ODG - $2 | bc`
	echo `basename $CODFILE` $ODG "(should be $2, diff: $DELTA)"
}

DATADIR="../BS.1387-ConformanceDatabase"
if [ ! -d $DATADIR ]; then
	echo "Reference data not found, conformance test NOT run."
	exit 0
fi

runpeaq "${DATADIR}/acodsna.wav" -0.676
runpeaq "${DATADIR}/bcodtri.wav" -0.304
runpeaq "${DATADIR}/ccodsax.wav" -1.829
runpeaq "${DATADIR}/ecodsmg.wav" -0.412
runpeaq "${DATADIR}/fcodsb1.wav" -1.195
runpeaq "${DATADIR}/fcodtr1.wav" -0.598
runpeaq "${DATADIR}/fcodtr2.wav" -1.927
runpeaq "${DATADIR}/fcodtr3.wav" -2.601
runpeaq "${DATADIR}/gcodcla.wav" -0.386
runpeaq "${DATADIR}/icodsna.wav" -3.786
runpeaq "${DATADIR}/kcodsme.wav" 0.038
runpeaq "${DATADIR}/lcodhrp.wav" -0.876
runpeaq "${DATADIR}/lcodpip.wav" -0.293
runpeaq "${DATADIR}/mcodcla.wav" -2.331
runpeaq "${DATADIR}/ncodsfe.wav" 0.045
runpeaq "${DATADIR}/scodclv.wav" -0.435
