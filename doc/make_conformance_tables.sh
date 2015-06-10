#!/bin/bash

runpeaq() {
	MODE=$1
	CODFILE=$2
	REFFILE=${CODFILE/cod/ref}
	OUTPUT=`LANG=LC_ALL ${BASEDIR}/../src/peaq --gst-disable-segtrap --gst-debug-level=2 --gst-plugin-load=${BASEDIR}/../src/.libs/libgstpeaq.so \
		${MODE} "${REFFILE}" "$CODFILE"`
	DI=`echo "$OUTPUT" | grep "Distortion Index:" | cut -d " " -f3`
	DI_DELTA=`echo $DI - $3 | bc`
	ODG=`echo "$OUTPUT" | grep "Objective Difference Grade:" | cut -d " " -f4`
	ODG_DELTA=`echo $ODG - $4 | bc`
	ITEM=`basename ${CODFILE} .wav`
	echo $ITEM "DI: " $DI "(should be $3, diff: $DI_DELTA)" "ODG: " $ODG "(should be $4, diff: $ODG_DELTA)"
	cat >>${XML_FILE} <<EOF
			<row>
				<entry>${ITEM}</entry><entry>${3}</entry><entry>${DI}</entry><entry>${DI_DELTA}</entry>
			</row>
EOF
	ODG_DELTA_SUM=`echo $ODG_DELTA_SUM + $ODG_DELTA | bc`
	ODG_DELTA_SQUARED_SUM=`echo "scale=6; $ODG_DELTA_SQUARED_SUM + $ODG_DELTA^2" | bc`
	DI_DELTA_SUM=`echo $DI_DELTA_SUM + $DI_DELTA | bc`
	DI_DELTA_SQUARED_SUM=`echo "scale=6; $DI_DELTA_SQUARED_SUM + $DI_DELTA^2" | bc`
	let FILE_COUNT++
}

BASEDIR=`dirname $0`

DATADIR=`grep CONFORMANCEDATADIR ${BASEDIR}/Makefile | cut -f 3 -d " "`
if [ "x$DATADIR" == x ]; then
	echo "CONFORMANCEDATADIR not set, cannot evaluate conformance."
	exit 0
fi
if [ ! -d $DATADIR ]; then
	echo "Reference data not found, cannot evaluate conformance."
	exit 0
fi


XML_FILE="${BASEDIR}/conformance_basic_table.xml"
cat >${XML_FILE} <<EOF
<table frame="none" id="conformance_basic_table">
	<title>Conformance test results for the basic version.</title>
	<tgroup cols='4' align='right' colsep='1' rowsep='1'>
		<colspec align='left' />
		<thead>
			<row>
				<entry>Item</entry>
				<entry>Reference DI</entry>
				<entry>Actual DI</entry>
				<entry>Difference</entry>
			</row>
		</thead>
		<tbody>
EOF

ODG_DELTA_SUM=0
ODG_DELTA_SQUARED_SUM=0
DI_DELTA_SUM=0
DI_DELTA_SQUARED_SUM=0
FILE_COUNT=0
runpeaq --basic "${DATADIR}/acodsna.wav" 1.304  -0.676
runpeaq --basic "${DATADIR}/bcodtri.wav" 1.949  -0.304
runpeaq --basic "${DATADIR}/ccodsax.wav" 0.048  -1.829
runpeaq --basic "${DATADIR}/ecodsmg.wav" 1.731  -0.412
runpeaq --basic "${DATADIR}/fcodsb1.wav" 0.677  -1.195
runpeaq --basic "${DATADIR}/fcodtr1.wav" 1.419  -0.598
runpeaq --basic "${DATADIR}/fcodtr2.wav" -0.045 -1.927
runpeaq --basic "${DATADIR}/fcodtr3.wav" -0.715 -2.601
runpeaq --basic "${DATADIR}/gcodcla.wav" 1.781  -0.386
runpeaq --basic "${DATADIR}/icodsna.wav" -3.029 -3.786
runpeaq --basic "${DATADIR}/kcodsme.wav" 3.093  0.038
runpeaq --basic "${DATADIR}/lcodhrp.wav" 1.041  -0.876
runpeaq --basic "${DATADIR}/lcodpip.wav" 1.973  -0.293
runpeaq --basic "${DATADIR}/mcodcla.wav" -0.436 -2.331
runpeaq --basic "${DATADIR}/ncodsfe.wav" 3.135  0.045
runpeaq --basic "${DATADIR}/scodclv.wav" 1.689  -0.435
echo "ODG mean error (bias):" `echo "scale=3; $ODG_DELTA_SUM / $FILE_COUNT" | bc`
echo "ODG mean square error:" `echo "scale=6; $ODG_DELTA_SQUARED_SUM / $FILE_COUNT" | bc`
echo "DI mean error (bias):" `echo "scale=3; $DI_DELTA_SUM / $FILE_COUNT" | bc`
echo "DI mean square error:" `echo "scale=6; $DI_DELTA_SQUARED_SUM / $FILE_COUNT" | bc`
cat >>${XML_FILE} <<EOF
		</tbody>
	</tgroup>
</table>
EOF

XML_FILE="${BASEDIR}/conformance_advanced_table.xml"
cat >${XML_FILE} <<EOF
<table frame="none" id="conformance_advanced_table">
	<title>Conformance test results for the advanced version.</title>
	<tgroup cols='4' align='right' colsep='1' rowsep='1'>
		<colspec align='left' />
		<thead>
			<row>
				<entry>Item</entry>
				<entry>Reference DI</entry>
				<entry>Actual DI</entry>
				<entry>Difference</entry>
			</row>
		</thead>
		<tbody>
EOF

ODG_DELTA_SUM=0
ODG_DELTA_SQUARED_SUM=0
DI_DELTA_SUM=0
DI_DELTA_SQUARED_SUM=0
FILE_COUNT=0
runpeaq --advanced "${DATADIR}/acodsna.wav" 1.632 -0.467
runpeaq --advanced "${DATADIR}/bcodtri.wav" 2.000 -0.281
runpeaq --advanced "${DATADIR}/ccodsax.wav" 0.567 -1.300
runpeaq --advanced "${DATADIR}/ecodsmg.wav" 1.594 -0.489
runpeaq --advanced "${DATADIR}/fcodsb1.wav" 1.039 -0.877
runpeaq --advanced "${DATADIR}/fcodtr1.wav" 1.555 -0.512
runpeaq --advanced "${DATADIR}/fcodtr2.wav" 0.162 -1.711
runpeaq --advanced "${DATADIR}/fcodtr3.wav" -0.783 -2.662
runpeaq --advanced "${DATADIR}/gcodcla.wav" 1.457 -0.573
runpeaq --advanced "${DATADIR}/icodsna.wav" -2.510 -3.664
runpeaq --advanced "${DATADIR}/kcodsme.wav" 2.765 -0.029
runpeaq --advanced "${DATADIR}/lcodhrp.wav" 1.538 -0.523
runpeaq --advanced "${DATADIR}/lcodpip.wav" 2.149 -0.219
runpeaq --advanced "${DATADIR}/mcodcla.wav" 0.430 -1.435
runpeaq --advanced "${DATADIR}/ncodsfe.wav" 3.163 0.050
runpeaq --advanced "${DATADIR}/scodclv.wav" 1.972 -0.293
echo "ODG mean error (bias):" `echo "scale=3; $ODG_DELTA_SUM / $FILE_COUNT" | bc`
echo "ODG mean square error:" `echo "scale=6; $ODG_DELTA_SQUARED_SUM / $FILE_COUNT" | bc`
echo "DI mean error (bias):" `echo "scale=3; $DI_DELTA_SUM / $FILE_COUNT" | bc`
echo "DI mean square error:" `echo "scale=6; $DI_DELTA_SQUARED_SUM / $FILE_COUNT" | bc`

cat >>${XML_FILE} <<EOF
		</tbody>
	</tgroup>
</table>
EOF
