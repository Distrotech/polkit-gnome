
@INTLTOOL_DESKTOP_RULE@
desktopdir = $(datadir)/applications
desktop_in_files = polkit-gnome-authorization.desktop.in
desktop_DATA = $(desktop_in_files:.desktop.in=.desktop)

servicedir       = $(datadir)/dbus-1/services
service_in_files = org.gnome.PolicyKit.service.in                                  \
		   org.gnome.PolicyKit.AuthorizationManager.service.in             \
		   gnome-org.freedesktop.PolicyKit.AuthenticationAgent.service.in
service_DATA     = $(service_in_files:.service.in=.service)

org.gnome.PolicyKit.service : org.gnome.PolicyKit.service.in Makefile
	@sed -e "s|\@libexecdir\@|$(libexecdir)|" $< > $@

gnome-org.freedesktop.PolicyKit.AuthenticationAgent.service : gnome-org.freedesktop.PolicyKit.AuthenticationAgent.service.in Makefile
	@sed -e "s|\@libexecdir\@|$(libexecdir)|" $< > $@

org.gnome.PolicyKit.AuthorizationManager.service : org.gnome.PolicyKit.AuthorizationManager.service.in Makefile
	@sed -e "s|\@bindir\@|$(bindir)|" $< > $@

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = polkit-gnome.pc

DISTCLEANFILES = $(service_DATA) $(desktop_DATA) $(pkgconfig_DATA)

EXTRA_DIST = $(service_in_files) $(desktop_in_files) polkit-gnome.pc.in

clean-local :
	rm -f *~ $(service_DATA) $(pkgconfig_DATA)
