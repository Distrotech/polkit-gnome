SUBDIRS = data src polkit-gnome tools

if BUILD_EXAMPLES
SUBDIRS += examples
endif

# Need to build docs after examples because the docs include generated files
SUBDIRS += doc po

# Creating ChangeLog from git log (taken from cairo/Makefile.am):
ChangeLog: $(srcdir)/ChangeLog

$(srcdir)/ChangeLog:
	rm -f $(srcdir)/ChangeLog && svn2cl --authors=AUTHORS -o $(srcdir)/ChangeLog

.PHONY: ChangeLog $(srcdir)/ChangeLog


EXTRA_DIST = \
	autogen.sh		\
	intltool-extract.in	\
	intltool-merge.in	\
	intltool-update.in	\
	TODO			\
	HACKING			\
	ChangeLog

DISTCLEANFILES = \
	intltool-extract	\
	intltool-merge		\
	intltool-update

clean-local :
	rm -f *~ po/*~
