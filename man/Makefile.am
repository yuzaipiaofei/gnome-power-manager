EXTRA_DIST =						\
	gnome-power-statistics.sgml

if HAVE_DOCBOOK2MAN
man_MANS =						\
	gnome-power-statistics.1
endif

if HAVE_DOCBOOK2MAN
gnome-power-statistics.1: gnome-power-statistics.sgml
	docbook2man $? &> /dev/null
endif

CLEANFILES = *.1 manpage.*

-include $(top_srcdir)/git.mk
