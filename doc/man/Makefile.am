dist_man1_MANS = peaq.1

if ENABLE_MAN
.xml.1:
	@XSLTPROC@ --param man.justify 1 -nonet http://docbook.sourceforge.net/release/xsl/current/manpages/docbook.xsl $<
endif

EXTRA_DIST = peaq.xml
