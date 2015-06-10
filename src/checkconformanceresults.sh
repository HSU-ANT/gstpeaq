#!/bin/bash

BASEDIR=`dirname $0`

if [ "x$CONFORMANCEDATADIR" == x ]; then
	echo "CONFORMANCEDATADIR not set, conformance test NOT run."
	exit 77
fi

if [ ! -d $CONFORMANCEDATADIR ]; then
	echo "Reference data not found, conformance test NOT run."
	exit 77
fi

PEAQ="${BASEDIR}/peaq --gst-disable-segtrap --gst-plugin-load=${BASEDIR}/.libs/libgstpeaq.so"

docheck() {
	MODE=$1
	REFERENCE=`grep -o -e "<entry>.*</entry><entry>.*</entry><entry>.*</entry><entry>.*</entry>" ${BASEDIR}/../doc/conformance_${MODE}_table.xml | sed "s/<entry>\(.*\)<\/entry><entry>\(.*\)<\/entry><entry>\(.*\)<\/entry><entry>\(.*\)<\/entry>/\1:\3/"`
	for ITEM in $REFERENCE; do
		ITEMNAME=${ITEM:0:7}
		CODFILE=${CONFORMANCEDATADIR}/${ITEMNAME}.wav
		REFFILE=${CONFORMANCEDATADIR}/${ITEMNAME/cod/ref}.wav
		REF_DI=${ITEM:8}
		DI=`LANG=LC_ALL ${PEAQ} --${MODE} "${REFFILE}" "$CODFILE" | grep "Distortion Index:" | cut -d " " -f3`
		echo -n $ITEMNAME $DI $REF_DI
		if [ $DI = $REF_DI ]; then
			echo " OK"
		else
			echo " FAILED"
			exit 1
		fi
	done
}

echo Basic version:
docheck basic
echo Advanced version:
docheck advanced

exit 0
