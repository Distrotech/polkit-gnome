/*
 * Copyright (C) 2009 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General
 * Public License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 * Author: David Zeuthen <davidz@redhat.com>
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <string.h>
#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <polkitagent/polkitagent.h>

#include "polkitgnomelistener.h"

int
main (int argc, char **argv)
{
  gint ret;
  GMainLoop *loop;
  PolkitAgentListener *listener;
  GError *error;

  g_type_init ();
  gtk_init (&argc, &argv);

  bindtextdomain (GETTEXT_PACKAGE, GNOMELOCALEDIR);
#if HAVE_BIND_TEXTDOMAIN_CODESET
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
#endif
  textdomain (GETTEXT_PACKAGE);

  ret = 1;

  loop = g_main_loop_new (NULL, FALSE);

  listener = polkit_gnome_listener_new ();

  error = NULL;
  if (!polkit_agent_register_listener (listener,
                                       NULL,
                                       "/org/gnome/PolicyKit1/AuthenticationAgent",
                                       &error))
    {
      g_printerr ("Cannot register authentication agent: %s\n", error->message);
      g_error_free (error);
      goto out;
    }

  g_main_loop_run (loop);

  ret = 0;

 out:
  g_object_unref (listener);
  g_main_loop_unref (loop);

  return ret;
}
