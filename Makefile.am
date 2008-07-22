SUBDIRS = data src polkit-gnome tools

if BUILD_EXAMPLES
SUBDIRS += examples
endif

# Need to build docs after examples because the docs include generated files
SUBDIRS += doc po

# Creating ChangeLog from git log (taken from cairo/Makefile.am):
ChangeLog: $(srcdir)/ChangeLog

$(srcdir)/ChangeLog:
	if test -d $(srcdir)/.svn ; then \
	  if test ! -e $(srcdir)/ChangeLog ; then \
	    svn2cl --authors=AUTHORS -o $(srcdir)/ChangeLog ; \
	  fi \
	fi

.PHONY: ChangeLog $(srcdir)/ChangeLog


EXTRA_DIST = \
	autogen.sh		\
	TODO			\
	HACKING			\
	ChangeLog

clean-local :
	rm -f *~ po/*~
