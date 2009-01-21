
libexec_PROGRAMS = polkit-gnome-manager

polkit_gnome_manager_SOURCES = 						\
	polkit-gnome-auth-dialog.h	polkit-gnome-auth-dialog.c	\
	sexy-url-label.h 		sexy-url-label.c		\
	main.c								\
	$(BUILT_SOURCES)

polkit_gnome_manager_CPPFLAGS = 			\
	-I$(top_srcdir)					\
	-DG_LOG_DOMAIN=\"PolicyKitGnome\"		\
	-DDATADIR=\""$(pkgdatadir)"\"			\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\"	\
	$(DISABLE_DEPRECATED)				\
	$(AM_CPPFLAGS)

polkit_gnome_manager_CFLAGS = 				\
	$(GTK_CFLAGS)					\
	$(GCONF_CFLAGS)					\
	$(EGGDBUS_CFLAGS)				\
	$(POLKIT_AGENT_CFLAGS)				\
	$(POLKIT_GOBJECT_CFLAGS)			\
	$(WARN_CFLAGS)					\
	$(AM_CFLAGS)

polkit_gnome_manager_LDFLAGS = 				\
	$(AM_LDFLAGS)

polkit_gnome_manager_LDADD = 				\
	$(GTK_LIBS)					\
	$(GCONF_LIBS)					\
	$(EGGDBUS_LIBS)					\
	$(POLKIT_AGENT_LIBS)				\
	$(POLKIT_GOBJECT_LIBS)				\
	$(INTLLIBS)

CLEANFILES = $(BUILT_SOURCES)

EXTRA_DIST = polkit-gnome-manager.xml

clean-local :
	rm -f *~
