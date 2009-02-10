#SUBDIRS = data src polkit-gnome tools

SUBDIRS = src

#if BUILD_EXAMPLES
#SUBDIRS += examples
#endif

# Need to build docs after examples because the docs include generated files
#SUBDIRS += doc po

SUBDIRS += po

EXTRA_DIST = \
	autogen.sh		\
	TODO			\
	HACKING

clean-local :
	rm -f *~ po/*~
