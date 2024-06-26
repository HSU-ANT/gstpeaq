  GstPEAQ  -  A GStreamer plugin for Perceptual Evaluation of Audio Quality

GstPEAQ is an implementation of the algorithm described in "Recommendation
ITU-R BS.1387-1: Method for objective measurements of perceived audio quality",
both in its basic and advanced version. It does not, however, conform to ITU-R
BS.1387-1 in the sense that is does not compute values within the allowed
tolerance for the given test items. The implemented algorithm of ITU-R
BS.1387-1 provides a way to compute an impairment grade between a reference
audio signal and a test audio signal, being the reference signal after having
undergone some distortion like e.g. perceptual coding. The impairment is
measured in terms of the Objective Difference Grade (ODG) that should match the
rating of an average human expert listener on a continuous scale with the
following anchor points:
   0 - impairment imperceptible
  -1 - impairment perceptible but not annoying
  -2 - impairment slightly annoying
  -3 - impairment annoying
  -4 - impairment very annoying
Although GstPEAQ does not conform to ITU-R BS.1387-1, deviations are
sufficiently small such that it should still provide an alternative to
listening tests just as good as implementations conforming to ITU-R BS.1387-1
do. See M. Holters and U. Zölzer, "GstPEAQ – an Open Source Implementation of
the PEAQ Algorithm"
(http://www.hsu-hh.de/download-1.5.1.php?brick_id=gywLUdLoxvNqEvNJ)
for details.

GstPEAQ comes as a GStreamer plugin providing an element named "peaq". The
element has two sink pads, "ref" and "test" for reference and test signal,
respectively. The computed ODG can be obtained by reading the "odg" property.
For the common case that reference and test signal are available as WAV files
(or in another format readable by GStreamer), a command line utility "peaq" is
provided that can simply be invoked as
  peaq [--advanced] {REFFILE} {TESTFILE}
where --advanced chooses the advanced version of the algorithm (the basic
version is used by default), and {REFFILE} and {TESTFILE} give the reference
and test signal file name, respectively.

Although development is primarily done for the Linux platform, GstPEAQ has been
successfully compiled for Windows and Mac OS X, too. See the respective
INSTALL.* file for further information.

Reference documentation, both concerning invocation and internals, can be found
at doc/html/index.html.
