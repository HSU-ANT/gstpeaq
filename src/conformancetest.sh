#!/bin/bash

runpeaq() {
	CODFILE=$1
	REFFILE=${CODFILE/cod/ref}
	OUTPUT=`LANG=LC_ALL ./peaq --gst-disable-segtrap --gst-debug-level=2 --gst-plugin-load=.libs/libgstpeaq.so \
		"${REFFILE}" "$CODFILE"`
	DI=`echo "$OUTPUT" | grep "Distortion Index:" | cut -d " " -f3`
	DI_DELTA=`echo $DI - $2 | bc`
	ODG=`echo "$OUTPUT" | grep "Objective Difference Grade:" | cut -d " " -f4`
	ODG_DELTA=`echo $ODG - $3 | bc`
	echo `basename $CODFILE` "DI: " $DI "(should be $2, diff: $DI_DELTA)" "ODG: " $ODG "(should be $3, diff: $ODG_DELTA)"
}

DATADIR="../BS.1387-ConformanceDatabase"
if [ ! -d $DATADIR ]; then
	echo "Reference data not found, conformance test NOT run."
	exit 0
fi

runpeaq "${DATADIR}/acodsna.wav" 1.304  -0.676
runpeaq "${DATADIR}/bcodtri.wav" 1.949  -0.304
runpeaq "${DATADIR}/ccodsax.wav" 0.048  -1.829
runpeaq "${DATADIR}/ecodsmg.wav" 1.731  -0.412
runpeaq "${DATADIR}/fcodsb1.wav" 0.677  -1.195
runpeaq "${DATADIR}/fcodtr1.wav" 1.419  -0.598
runpeaq "${DATADIR}/fcodtr2.wav" -0.045 -1.927
runpeaq "${DATADIR}/fcodtr3.wav" -0.715 -2.601
runpeaq "${DATADIR}/gcodcla.wav" 1.781  -0.386
runpeaq "${DATADIR}/icodsna.wav" -3.029 -3.786
runpeaq "${DATADIR}/kcodsme.wav" 3.093  0.038
runpeaq "${DATADIR}/lcodhrp.wav" 1.041  -0.876
runpeaq "${DATADIR}/lcodpip.wav" 1.973  -0.293
runpeaq "${DATADIR}/mcodcla.wav" -0.436 -2.331
runpeaq "${DATADIR}/ncodsfe.wav" 3.135  0.045
runpeaq "${DATADIR}/scodclv.wav" 1.689  -0.435
