
ACLOCAL_AMFLAGS = -I m4

SUBDIRS = 						\
	src						\
	po						\
	man						\
	data

EXTRA_DIST =						\
	MAINTAINERS

DISTCLEANFILES =					\
	test-driver					\
	gnome-power-manager-*.tar.*

MAINTAINERCLEANFILES =					\
	ABOUT-NLS					\
	aclocal.m4					\
	compile						\
	config.guess					\
	config.rpath					\
	config.h.in					\
	config.sub					\
	depcomp						\
	install-sh					\
	ltmain.sh					\
	missing						\
	mkinstalldirs					\
	ChangeLog					\
	NEWS

distclean-local:
	if test $(srdcir) = .; then :; else \
		rm -f ChangeLog; \
		rm -f NEWS; \
	fi

ChangeLog:
	@echo Creating $@
	@if test -d "$(srcdir)/.git"; then \
	  (GIT_DIR=$(top_srcdir)/.git ./missing --run git log gnome-3-6.. --stat -M -C --name-status  --date=short --no-color) | fmt --split-only > $@.tmp \
	  && mv -f $@.tmp $@ \
	  || ($(RM) $@.tmp; \
	      echo Failed to generate ChangeLog, your ChangeLog may be outdated >&2; \
	      (test -f $@ || echo git-log is required to generate this file >> $@)); \
	else \
	  test -f $@ || \
	  (echo A git checkout and git-log is required to generate ChangeLog >&2 && \
	  echo A git checkout and git-log is required to generate this file >> $@); \
	fi

NEWS: data/appdata/*.appdata.xml
	$(AM_V_GEN) \
	if test -e $(APPSTREAM_UTIL); then \
		$(APPSTREAM_UTIL) appdata-to-news $^ > $@; \
	else \
		touch $@; \
	fi

.PHONY: ChangeLog NEWS

-include $(top_srcdir)/git.mk
