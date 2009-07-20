
FULL_LIBEXECDIR=$(libexecdir)

desktopdir = $(sysconfdir)/xdg/autostart
desktop_DATA = polkit-gnome-authentication-agent-1.desktop

@INTLTOOL_DESKTOP_RULE@

polkit-gnome-authentication-agent-1.desktop.in : polkit-gnome-authentication-agent-1.desktop.in.in
	cat polkit-gnome-authentication-agent-1.desktop.in.in | sed 's,@FULL_LIBEXECDIR@,$(FULL_LIBEXECDIR),' > $@

libexec_PROGRAMS = polkit-gnome-authentication-agent-1

polkit_gnome_authentication_agent_1_SOURCES = 						\
	polkitgnomelistener.h			polkitgnomelistener.c			\
	polkitgnomeauthenticator.h		polkitgnomeauthenticator.c		\
	polkitgnomeauthenticationdialog.h	polkitgnomeauthenticationdialog.c	\
	main.c										\
	$(BUILT_SOURCES)

polkit_gnome_authentication_agent_1_CPPFLAGS = 		\
	-I$(top_srcdir)					\
	-DG_LOG_DOMAIN=\"polkit-gnome-1\"		\
	-DDATADIR=\""$(pkgdatadir)"\"			\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\"	\
	-DPOLKIT_AGENT_I_KNOW_API_IS_SUBJECT_TO_CHANGE	\
	$(DISABLE_DEPRECATED)				\
	$(AM_CPPFLAGS)

polkit_gnome_authentication_agent_1_CFLAGS = 		\
	$(GTK_CFLAGS)					\
	$(GCONF_CFLAGS)					\
	$(POLKIT_AGENT_CFLAGS)				\
	$(POLKIT_GOBJECT_CFLAGS)			\
	$(WARN_CFLAGS)					\
	$(AM_CFLAGS)

polkit_gnome_authentication_agent_1_LDFLAGS = 		\
	$(AM_LDFLAGS)

polkit_gnome_authentication_agent_1_LDADD = 		\
	$(GTK_LIBS)					\
	$(GCONF_LIBS)					\
	$(POLKIT_AGENT_LIBS)				\
	$(POLKIT_GOBJECT_LIBS)				\
	$(INTLLIBS)

EXTRA_DIST = polkit-gnome-authentication-agent-1.desktop.in.in

clean-local :
	rm -f *~ polkit-gnome-authentication-agent-1.desktop polkit-gnome-authentication-agent-1.desktop.in
