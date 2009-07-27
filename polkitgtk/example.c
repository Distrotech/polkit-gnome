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
 * Free Software Foundation, Inc., 59 Temple Place, Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Author: David Zeuthen <davidz@redhat.com>
 */

#include <polkitgtk/polkitgtk.h>

static void
on_button_changed (PolkitLockButton *button,
                   gpointer          user_data)
{
  GtkWidget *entry = GTK_WIDGET (user_data);

  gtk_widget_set_sensitive (entry,
                            polkit_lock_button_get_is_authorized (button));
}

int
main (int argc, char *argv[])
{
  GtkWidget *window;
  GtkWidget *label;
  GtkWidget *button;
  GtkWidget *entry;
  GtkWidget *vbox;
  gchar *s;

  gtk_init (&argc, &argv);

  if (argc != 2)
    {
      g_printerr ("usage: %s <action_id>\n", argv[0]);
      goto out;
    }

  window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  gtk_window_set_resizable (GTK_WINDOW (window), FALSE);

  vbox = gtk_vbox_new (FALSE, 12);
  gtk_container_set_border_width (GTK_CONTAINER (window), 12);
  gtk_container_add (GTK_CONTAINER (window), vbox);

  s = g_strdup_printf ("Showing PolkitLockButton for action id: %s", argv[1]);
  label = gtk_label_new (s);
  gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
  gtk_box_pack_start (GTK_BOX (vbox), label, FALSE, FALSE, 0);
  g_free (s);

  entry = gtk_entry_new ();
  gtk_box_pack_start (GTK_BOX (vbox), entry, FALSE, FALSE, 0);

  button = polkit_lock_button_new (argv[1]);
  g_signal_connect (button,
                    "changed",
                    G_CALLBACK (on_button_changed),
                    entry);
  gtk_box_pack_start (GTK_BOX (vbox), button, FALSE, FALSE, 0);

  gtk_widget_set_sensitive (entry,
                            polkit_lock_button_get_is_authorized (POLKIT_LOCK_BUTTON (button)));

  gtk_widget_show_all (window);
  gtk_window_present (GTK_WINDOW (window));

  gtk_main ();

 out:
  return 0;
}
