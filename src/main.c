/*
 * Copyright (C) 2007 David Zeuthen <david@fubar.dk>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib-object.h>

#include <gdk/gdkx.h>
#include <gtk/gtk.h>

#include <polkitagent/polkitagent.h>

#include "polkit-gnome-auth-dialog.h"

static PolkitAuthority *authority;

typedef struct
{
  gchar *action_id;
  PolkitActionDescription *action_desc;
  gchar *cookie;
  GList *identities;
  gpointer pending_call;

  gboolean gained_authorization;
  gboolean was_cancelled;
  gboolean invalid_data;

  GMainLoop *loop;
  PolkitAgentAuthenticationSession *session;
  GtkWidget *dialog;

} AuthData;

static AuthData *
auth_data_new (const gchar *action_id,
               const gchar *cookie,
               GList *identities,
               gpointer pending_call)
{
  AuthData *data;

  data = g_new0 (AuthData, 1);
  data->action_id = g_strdup (action_id);
  data->cookie = g_strdup (cookie);

  data->identities = g_list_copy (identities);
  g_list_foreach (data->identities, (GFunc) g_object_ref, NULL);

  data->pending_call = pending_call;

  return data;
}

static void
auth_data_free (AuthData *data)
{
  g_free (data->action_id);

  if (data->action_desc != NULL)
    g_object_unref (data->action_desc);

  g_free (data->cookie);
  g_list_foreach (data->identities, (GFunc) g_object_unref, NULL);
  g_list_free (data->identities);

  if (data->session != NULL)
    g_object_unref (data->session);

  g_main_loop_unref (data->loop);

  gtk_widget_destroy (data->dialog);

  g_free (data);
}

static AuthData *auth_data = NULL;
static PolkitAgentAuthenticationAgent *agent;

static void
do_show_dialog (void)
{
  guint32 server_time;

  gtk_widget_realize (auth_data->dialog);
  server_time = gdk_x11_get_server_time (auth_data->dialog->window);
  gtk_window_present_with_time (GTK_WINDOW (auth_data->dialog), server_time);
  gtk_widget_show_all (auth_data->dialog);
}

static void
do_cancel_auth (void)
{
  auth_data->was_cancelled = TRUE;
  polkit_agent_authentication_session_cancel (auth_data->session);
}

static char *
conversation_pam_prompt (PolkitAgentAuthenticationSession *session,
                         const char *request,
                         gboolean echo_on,
                         gpointer user_data)
{
  gchar *password;
  int response;

  password = NULL;

  g_debug ("in conversation_pam_prompt, request='%s', echo_on=%d", request, echo_on);

#if 0
  /* Fix up, and localize, password prompt if it's password auth */
  printf ("request: '%s'\n", request);
  if (g_ascii_strncasecmp (request, "password:", 9) == 0)
    {
      if (auth_data->requires_admin)
        {
          if (auth_data->admin_user_selected != NULL)
            {
              request2 = g_strdup_printf (_("_Password for %s:"), auth_data->admin_user_selected);
            }
          else
            {
              request2 = g_strdup (_("_Password for root:"));
            }
        }
      else
        {
          request2 = g_strdup (_("_Password:"));
        }
    }
  else if (g_ascii_strncasecmp (request, "password or swipe finger:", 25) == 0)
    {
      if (auth_data->requires_admin)
        {
          if (auth_data->admin_user_selected != NULL)
            {
              request2 = g_strdup_printf (_("_Password or swipe finger for %s:"),
                                          auth_data->admin_user_selected);
            }
          else
            {
              request2 = g_strdup (_("_Password or swipe finger for root:"));
            }
        }
      else
        {
          request2 = g_strdup (_("_Password or swipe finger:"));
        }
    }
  else
    {
      request2 = g_strdup (request);
    }
#endif

  polkit_gnome_auth_dialog_set_prompt (POLKIT_GNOME_AUTH_DIALOG (auth_data->dialog), request, echo_on);

  do_show_dialog ();

  response = gtk_dialog_run (GTK_DIALOG (auth_data->dialog));

  /* cancel auth unless user clicked "Authenticate" */
  if (response != GTK_RESPONSE_OK)
    {
      do_cancel_auth ();
      goto out;
    }

  password = g_strdup (polkit_gnome_auth_dialog_get_password (POLKIT_GNOME_AUTH_DIALOG (auth_data->dialog)));

out:
  return password;
}

static char *
conversation_pam_prompt_echo_off (PolkitAgentAuthenticationSession *session,
                                  const char *request,
                                  gpointer user_data)
{
  return conversation_pam_prompt (session, request, FALSE, user_data);
}

static char *
conversation_pam_prompt_echo_on (PolkitAgentAuthenticationSession *session,
                                 const char *request,
                                 gpointer user_data)
{
  return conversation_pam_prompt (session, request, TRUE, FALSE);
}

static void
conversation_pam_error_msg (PolkitAgentAuthenticationSession *session,
                            const char *msg,
                            gpointer user_data)
{
  g_debug ("error_msg='%s'", msg);
}

static void
conversation_pam_text_info (PolkitAgentAuthenticationSession *session,
                            const char *msg,
                            gpointer user_data)
{
  g_debug ("text_info='%s'", msg);
}


static void
conversation_done (PolkitAgentAuthenticationSession *session,
                   gboolean                          gained_authorization,
                   gboolean                          invalid_data,
                   gpointer                          user_data)
{
  auth_data->gained_authorization = gained_authorization;
  auth_data->invalid_data = invalid_data;;

  g_debug ("in conversation_done gained=%d, invalid=%d", gained_authorization, invalid_data);

  if ((auth_data->invalid_data || auth_data->was_cancelled) && auth_data->dialog != NULL)
    {
      gtk_widget_destroy (auth_data->dialog);
      auth_data->dialog = NULL;
    }

  g_main_loop_quit (auth_data->loop);
}

static PolkitActionDescription *
get_desc_for_action (const gchar *action_id)
{
  GList *action_descs;
  GList *l;
  PolkitActionDescription *result;

  result = NULL;

  action_descs = polkit_authority_enumerate_actions_sync (authority,
                                                          NULL,
                                                          NULL,
                                                          NULL);
  for (l = action_descs; l != NULL; l = l->next)
    {
      PolkitActionDescription *action_desc = POLKIT_ACTION_DESCRIPTION (l->data);

      if (strcmp (polkit_action_description_get_action_id (action_desc), action_id) == 0)
        {
          result = g_object_ref (action_desc);
          goto out;
        }
    }

 out:

  g_list_foreach (action_descs, (GFunc) g_object_unref, NULL);
  g_list_free (action_descs);

  return result;
}

static gboolean
do_authentication (gpointer user_data)
{
  gint num_tries;
  PolkitIdentity *identity;
  GIcon *icon;
  gchar *icon_name;

  auth_data->action_desc = get_desc_for_action (auth_data->action_id);

  icon_name = NULL;

  icon = polkit_action_description_get_icon (auth_data->action_desc);
  if (icon != NULL)
    icon_name = g_icon_to_string (icon);

  auth_data->dialog = polkit_gnome_auth_dialog_new ("", /* TODO: path to exe file */
                                                    auth_data->action_id,
                                                    polkit_action_description_get_vendor_name (auth_data->action_desc),
                                                    polkit_action_description_get_vendor_url (auth_data->action_desc),
                                                    icon_name,
                                                    polkit_action_description_get_message (auth_data->action_desc));

  polkit_gnome_auth_dialog_set_options (POLKIT_GNOME_AUTH_DIALOG (auth_data->dialog),
                                        FALSE,
                                        FALSE,
                                        FALSE,
                                        NULL);

  auth_data->loop = g_main_loop_new (NULL, TRUE);

  num_tries = 0;

  identity = POLKIT_IDENTITY (auth_data->identities->data);

 try_again:

  /* TODO: make this work with multiple users */
  auth_data->session = polkit_agent_authentication_session_new (identity,
                                                                auth_data->cookie);

  polkit_agent_authentication_session_set_functions (auth_data->session,
                                                     conversation_pam_prompt_echo_off,
                                                     conversation_pam_prompt_echo_on,
                                                     conversation_pam_error_msg,
                                                     conversation_pam_text_info,
                                                     conversation_done,
                                                     NULL);


  if (!polkit_agent_authentication_session_initiate_auth (auth_data->session))
    {
      g_warning ("Failed to initiate authentication session");
      goto done;
    }
  g_main_loop_run (auth_data->loop);

  num_tries++;

  g_debug ("gained_authorization=%d was_cancelled=%d invalid_data=%d.",
           auth_data->gained_authorization,
           auth_data->was_cancelled,
           auth_data->invalid_data);

  if (!auth_data->gained_authorization && !auth_data->was_cancelled && !auth_data->invalid_data)
    {
      if (auth_data->dialog != NULL)
        {
          /* shake the dialog to indicate error */
          polkit_gnome_auth_dialog_indicate_auth_error (POLKIT_GNOME_AUTH_DIALOG (auth_data->dialog));

          if (num_tries < 3)
            {
              g_object_unref (auth_data->session);
              auth_data->session = NULL;
              goto try_again;
            }
        }
    }

 done:

  polkit_agent_authentication_agent_finish (agent,
                                            auth_data->pending_call,
                                            NULL);

  auth_data_free (auth_data);
  auth_data = NULL;

  g_free (icon_name);

  return FALSE;
}


static void
begin_authentication_func (PolkitAgentAuthenticationAgent *agent,
                           const gchar                    *action_id,
                           const gchar                    *cookie,
                           GList                          *identities,
                           gpointer                        pending_call)
{
  GList *l;

  g_debug ("Begin authentication func action_id=%s cookie=%s num_identities=%d",
           action_id,
           cookie,
           g_list_length (identities));

  for (l = identities; l != NULL; l = l->next)
    {
      PolkitIdentity *identity = POLKIT_IDENTITY (l->data);
      gchar *identity_str;

      identity_str = polkit_identity_to_string (identity);
      g_debug ("  identity %s", identity_str);
      g_free (identity_str);
    }

  if (auth_data != NULL)
    {
      GError *error;

      error = g_error_new (POLKIT_ERROR,
                           POLKIT_ERROR_FAILED,
                           "Authentication is already in progress for another action");

      polkit_agent_authentication_agent_finish (agent,
                                                auth_data->pending_call,
                                                error);

      g_error_free (error);
      goto out;
    }

  auth_data = auth_data_new (action_id,
                             cookie,
                             identities,
                             pending_call);

  g_idle_add (do_authentication, NULL);

 out:
  ;
}

static void
cancel_authentication_func (PolkitAgentAuthenticationAgent *agent,
                            const gchar                    *cookie,
                            gpointer                        user_data)
{
  g_debug ("Cancel authentication func cookie=%s", cookie);

  if (auth_data != NULL)
    {
      if (strcmp (auth_data->cookie, cookie) == 0)
        {
          do_cancel_auth ();
        }
    }
}

int
main (int argc, char **argv)
{
  GMainLoop *loop;
  GError *error;
  int ret;

  g_type_init ();
  gtk_init (&argc, &argv);

  bindtextdomain (GETTEXT_PACKAGE, GNOMELOCALEDIR);
#if HAVE_BIND_TEXTDOMAIN_CODESET
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
#endif
  textdomain (GETTEXT_PACKAGE);

  ret = 1;

  loop = NULL;
  authority = NULL;
  agent = NULL;

  authority = polkit_authority_get ();

  error = NULL;
  agent = polkit_agent_authentication_agent_new (begin_authentication_func,
                                                 cancel_authentication_func,
                                                 NULL,
                                                 &error);
  if (agent == NULL)
    {
      g_warning ("Error registering authentication agent: %s", error->message);
      g_error_free (error);
      goto out;
    }

  loop = g_main_loop_new (NULL, FALSE);

  g_main_loop_run (loop);

  ret = 0;

 out:
  if (agent != NULL)
    g_object_unref (agent);

  if (loop != NULL)
    g_main_loop_unref (loop);

  if (authority != NULL)
    g_object_unref (authority);

  return ret;
}
