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


#include "config.h"

#include <string.h>

#include "polkitgnomelistener.h"
#include "polkitgnomeauthenticator.h"

struct _PolkitGnomeListener
{
  PolkitAgentListener parent_instance;

  /* we only support a single active authenticator right now */
  PolkitGnomeAuthenticator *the_authenticator;
};

struct _PolkitGnomeListenerClass
{
  PolkitAgentListenerClass parent_class;
};

static void polkit_gnome_listener_initiate_authentication (PolkitAgentListener  *listener,
                                                           const gchar          *action_id,
                                                           const gchar          *message,
                                                           const gchar          *icon_name,
                                                           PolkitDetails        *details,
                                                           const gchar          *cookie,
                                                           GList                *identities,
                                                           GCancellable         *cancellable,
                                                           GAsyncReadyCallback   callback,
                                                           gpointer              user_data);

static gboolean polkit_gnome_listener_initiate_authentication_finish (PolkitAgentListener  *listener,
                                                                      GAsyncResult         *res,
                                                                      GError              **error);

G_DEFINE_TYPE (PolkitGnomeListener, polkit_gnome_listener, POLKIT_AGENT_TYPE_LISTENER);

static void
polkit_gnome_listener_init (PolkitGnomeListener *listener)
{
}

static void
polkit_gnome_listener_finalize (GObject *object)
{
  PolkitGnomeListener *listener;

  listener = POLKIT_GNOME_LISTENER (object);

  if (G_OBJECT_CLASS (polkit_gnome_listener_parent_class)->finalize != NULL)
    G_OBJECT_CLASS (polkit_gnome_listener_parent_class)->finalize (object);
}

static void
polkit_gnome_listener_class_init (PolkitGnomeListenerClass *klass)
{
  GObjectClass *gobject_class;
  PolkitAgentListenerClass *listener_class;

  gobject_class = G_OBJECT_CLASS (klass);
  listener_class = POLKIT_AGENT_LISTENER_CLASS (klass);

  gobject_class->finalize = polkit_gnome_listener_finalize;

  listener_class->initiate_authentication          = polkit_gnome_listener_initiate_authentication;
  listener_class->initiate_authentication_finish   = polkit_gnome_listener_initiate_authentication_finish;
}

PolkitAgentListener *
polkit_gnome_listener_new (void)
{
  return POLKIT_AGENT_LISTENER (g_object_new (POLKIT_GNOME_TYPE_LISTENER, NULL));
}

typedef struct
{
  PolkitGnomeListener *listener;
  GSimpleAsyncResult *simple;
  GCancellable *cancellable;

  gulong cancel_id;
} AuthData;

static AuthData *
auth_data_new (PolkitGnomeListener *listener,
               GSimpleAsyncResult *simple,
               GCancellable *cancellable)
{
  AuthData *data;

  data = g_new0 (AuthData, 1);
  data->listener = g_object_ref (listener);
  data->simple = g_object_ref (simple);
  data->cancellable = g_object_ref (cancellable);
  return data;
}

static void
auth_data_free (AuthData *data)
{
  g_object_unref (data->listener);
  g_object_unref (data->simple);
  if (data->cancellable != NULL && data->cancel_id > 0)
    g_signal_handler_disconnect (data->cancellable, data->cancel_id);
  g_object_unref (data->cancellable);
  g_free (data);
}

static void
authenticator_completed (PolkitGnomeAuthenticator *authenticator,
                         gboolean                  gained_authorization,
                         gpointer                  user_data)
{
  AuthData *data = user_data;

  g_warn_if_fail (authenticator == data->listener->the_authenticator);

  g_object_unref (data->listener->the_authenticator);
  data->listener->the_authenticator = NULL;

  g_simple_async_result_complete (data->simple);
  g_object_unref (data->simple);

  auth_data_free (data);
}

static void
cancelled_cb (GCancellable *cancellable,
              gpointer user_data)
{
  AuthData *data = user_data;

  polkit_gnome_authenticator_cancel (data->listener->the_authenticator);
}

static void
polkit_gnome_listener_initiate_authentication (PolkitAgentListener  *agent_listener,
                                               const gchar          *action_id,
                                               const gchar          *message,
                                               const gchar          *icon_name,
                                               PolkitDetails        *details,
                                               const gchar          *cookie,
                                               GList                *identities,
                                               GCancellable         *cancellable,
                                               GAsyncReadyCallback   callback,
                                               gpointer              user_data)
{
  PolkitGnomeListener *listener = POLKIT_GNOME_LISTENER (agent_listener);
  GSimpleAsyncResult *simple;
  AuthData *data;

  simple = g_simple_async_result_new (G_OBJECT (listener),
                                      callback,
                                      user_data,
                                      polkit_gnome_listener_initiate_authentication);

  if (listener->the_authenticator != NULL)
    {
      g_simple_async_result_set_error (simple,
                                       POLKIT_ERROR,
                                       POLKIT_ERROR_FAILED,
                                       "Authentication is already in progress for another action");
      g_simple_async_result_complete (simple);
      goto out;
    }

  listener->the_authenticator = polkit_gnome_authenticator_new (action_id,
                                                                message,
                                                                icon_name,
                                                                details,
                                                                cookie,
                                                                identities);
  if (listener->the_authenticator == NULL)
    {
      g_simple_async_result_set_error (simple,
                                       POLKIT_ERROR,
                                       POLKIT_ERROR_FAILED,
                                       "Authentication is already in progress for another action");
      g_simple_async_result_complete (simple);
      goto out;
    }

  data = auth_data_new (listener, simple, cancellable);

  g_signal_connect (listener->the_authenticator,
                    "completed",
                    G_CALLBACK (authenticator_completed),
                    data);

  if (cancellable != NULL)
    {
      data->cancel_id = g_signal_connect (cancellable,
                                          "cancelled",
                                          G_CALLBACK (cancelled_cb),
                                          data);
    }

  polkit_gnome_authenticator_initiate (listener->the_authenticator);

 out:
  ;
}

static gboolean
polkit_gnome_listener_initiate_authentication_finish (PolkitAgentListener  *listener,
                                                      GAsyncResult         *res,
                                                      GError              **error)
{
  GSimpleAsyncResult *simple = G_SIMPLE_ASYNC_RESULT (res);

  g_warn_if_fail (g_simple_async_result_get_source_tag (simple) == polkit_gnome_listener_initiate_authentication);

  if (g_simple_async_result_propagate_error (simple, error))
    return FALSE;

  return TRUE;
}

