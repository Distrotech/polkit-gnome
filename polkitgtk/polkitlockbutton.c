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

#include "config.h"

#include <glib/gi18n-lib.h>
#include <string.h>
#include <polkit/polkit.h>

#include "polkitlockbutton.h"

/**
 * SECTION:polkitlockbutton
 * @title: PolkitLockButton
 * @short_description: Toggle button for obtaining/revoking authorizations
 * @stability: Stable
 *
 * #PolkitLockButton is a widget that can be used in control panels to
 * allow users to obtain and revoke authorizations needed for the
 * control panel UI to function.
 *
 * If the user lacks the authorization but authorization can be
 * obtained through authentication, the widget looks like this
 * <mediaobject id="lock-button-locked">
 *  <imageobject>
 *    <imagedata fileref="polkit-lock-button-locked.png" format="PNG"/>
 *  </imageobject>
 * </mediaobject>
 * and the user can click the button to obtain the authorization. This
 * will pop up an authentication dialog.
 * Once authorization is obtained, the widget changes to this
 * <mediaobject id="lock-button-unlocked">
 *  <imageobject>
 *    <imagedata fileref="polkit-lock-button-unlocked.png" format="PNG"/>
 *  </imageobject>
 * </mediaobject>
 * and the authorization can be dropped by clicking the button.
 * If the user is not able to obtain authorization at all, the widget
 * looks like this
 * <mediaobject id="lock-button-unlocked-not-authorized">
 *  <imageobject>
 *    <imagedata fileref="polkit-lock-button-not-authorized.png" format="PNG"/>
 *  </imageobject>
 * </mediaobject>
 * If the user is authorized (either implicitly via the .policy file
 * defaults or through e.g. Local Authority configuration) and no
 * authentication is necessary, the widget will be hidden.
 */

struct _PolkitLockButtonPrivate
{
  PolkitAuthority *authority;
  PolkitSubject *subject;
  gchar *action_id;

  gchar *text_unlock;
  gchar *text_lock;
  gchar *text_not_authorized;

  GtkWidget *toggle_button;
  GtkWidget *label;
  gboolean ignore_toggled_signal;

  gboolean can_obtain;
  gboolean authorized;
  gboolean hidden;

  /* is non-NULL exactly when we are authorized and have a temporary authorization */
  gchar *tmp_authz_id;

  /* This is non-NULL exactly when we have a non-interactive check outstanding */
  GCancellable *check_cancellable;

  /* This is non-NULL exactly when we have an interactive check outstanding */
  GCancellable *interactive_check_cancellable;

};

enum
{
  PROP_0,
  PROP_ACTION_ID,
  PROP_IS_AUTHORIZED,
  PROP_IS_VISIBLE,
  PROP_CAN_OBTAIN,
  PROP_TEXT_UNLOCK,
  PROP_TEXT_LOCK,
  PROP_TEXT_NOT_AUTHORIZED,
};

enum
{
  CHANGED_SIGNAL,
  LAST_SIGNAL,
};

static guint signals[LAST_SIGNAL] = {0, };

static void initiate_check (PolkitLockButton *button);
static void do_sync_check (PolkitLockButton *button);
static void update_state (PolkitLockButton *button);

static void on_authority_changed (PolkitAuthority *authority,
                                  gpointer         user_data);

static void on_toggled (GtkToggleButton *toggle_button,
                        gpointer         user_data);

G_DEFINE_TYPE (PolkitLockButton, polkit_lock_button, GTK_TYPE_HBOX);

static void
polkit_lock_button_finalize (GObject *object)
{
  PolkitLockButton *button = POLKIT_LOCK_BUTTON (object);

  g_free (button->priv->action_id);
  g_free (button->priv->tmp_authz_id);
  g_object_unref (button->priv->subject);

  if (button->priv->check_cancellable != NULL)
    {
      g_cancellable_cancel (button->priv->check_cancellable);
      g_object_unref (button->priv->check_cancellable);
    }

  if (button->priv->interactive_check_cancellable != NULL)
    {
      g_cancellable_cancel (button->priv->interactive_check_cancellable);
      g_object_unref (button->priv->interactive_check_cancellable);
    }

  g_signal_handlers_disconnect_by_func (button->priv->authority,
                                        on_authority_changed,
                                        button);
  g_object_unref (button->priv->authority);

  if (G_OBJECT_CLASS (polkit_lock_button_parent_class)->finalize != NULL)
    G_OBJECT_CLASS (polkit_lock_button_parent_class)->finalize (object);
}

static void
polkit_lock_button_get_property (GObject    *object,
                                 guint       property_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
  PolkitLockButton *button = POLKIT_LOCK_BUTTON (object);

  switch (property_id)
    {
    case PROP_ACTION_ID:
      g_value_set_string (value, button->priv->action_id);
      break;

    case PROP_IS_AUTHORIZED:
      g_value_set_boolean (value, button->priv->authorized);
      break;

    case PROP_IS_VISIBLE:
      g_value_set_boolean (value, !button->priv->hidden);
      break;

    case PROP_CAN_OBTAIN:
      g_value_set_boolean (value, button->priv->can_obtain);
      break;

    case PROP_TEXT_UNLOCK:
      g_value_set_string (value, button->priv->text_unlock);
      break;

    case PROP_TEXT_LOCK:
      g_value_set_string (value, button->priv->text_lock);
      break;

    case PROP_TEXT_NOT_AUTHORIZED:
      g_value_set_string (value, button->priv->text_not_authorized);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
polkit_lock_button_set_property (GObject      *object,
                                 guint         property_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
  PolkitLockButton *button = POLKIT_LOCK_BUTTON (object);

  switch (property_id)
    {
    case PROP_ACTION_ID:
      button->priv->action_id = g_value_dup_string (value);
      break;

    case PROP_TEXT_UNLOCK:
      polkit_lock_button_set_unlock_text (button, g_value_get_string (value));
      break;

    case PROP_TEXT_LOCK:
      polkit_lock_button_set_lock_text (button, g_value_get_string (value));
      break;

    case PROP_TEXT_NOT_AUTHORIZED:
      polkit_lock_button_set_not_authorized_text (button, g_value_get_string (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}


static void
polkit_lock_button_init (PolkitLockButton *button)
{
  button->priv = G_TYPE_INSTANCE_GET_PRIVATE (button,
                                              POLKIT_TYPE_LOCK_BUTTON,
                                              PolkitLockButtonPrivate);

}

static void
polkit_lock_button_constructed (GObject *object)
{
  PolkitLockButton *button = POLKIT_LOCK_BUTTON (object);
  GtkWidget *image;

  gtk_box_set_spacing (GTK_BOX (button), 6);

  button->priv->authority = polkit_authority_get ();
  g_signal_connect (button->priv->authority,
                    "changed",
                    G_CALLBACK (on_authority_changed),
                    button);

  button->priv->toggle_button = gtk_toggle_button_new ();
  image = gtk_image_new_from_stock (GTK_STOCK_DIALOG_AUTHENTICATION,
                                    GTK_ICON_SIZE_BUTTON);
  gtk_button_set_image (GTK_BUTTON (button->priv->toggle_button), image);
  g_signal_connect (button->priv->toggle_button,
                    "toggled",
                    G_CALLBACK (on_toggled),
                    button);

  gtk_box_pack_start (GTK_BOX (button),
                      button->priv->toggle_button,
                      FALSE,
                      FALSE,
                      0);

  button->priv->label = gtk_label_new ("");
  gtk_box_pack_start (GTK_BOX (button),
                      button->priv->label,
                      FALSE,
                      FALSE,
                      0);

  /* take control of visibility of child widgets */
  gtk_widget_set_no_show_all (button->priv->toggle_button, TRUE);
  gtk_widget_set_no_show_all (button->priv->label, TRUE);

  if (button->priv->subject == NULL)
    {
      button->priv->subject = polkit_unix_process_new (getpid ());
    }

  /* synchronously check on construction - TODO: we could implement GAsyncInitable
   * in the future to avoid this sync check
   */
  do_sync_check (button);

  update_state (button);

  if (G_OBJECT_CLASS (polkit_lock_button_parent_class)->constructed != NULL)
    G_OBJECT_CLASS (polkit_lock_button_parent_class)->constructed (object);
}

static void
polkit_lock_button_class_init (PolkitLockButtonClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->finalize     = polkit_lock_button_finalize;
  gobject_class->get_property = polkit_lock_button_get_property;
  gobject_class->set_property = polkit_lock_button_set_property;
  gobject_class->constructed  = polkit_lock_button_constructed;

  g_type_class_add_private (klass, sizeof (PolkitLockButtonPrivate));

  /**
   * PolkitLockButton:action-id:
   *
   * The action identifier to use for the button.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_ACTION_ID,
                                   g_param_spec_string ("action-id",
                                                        _("Action Identifier"),
                                                        _("The action identifier to use for the button"),
                                                        NULL,
                                                        G_PARAM_READWRITE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_NAME |
                                                        G_PARAM_STATIC_NICK |
                                                        G_PARAM_STATIC_BLURB));

  /**
   * PolkitLockButton:is-authorized:
   *
   * Whether the process is authorized.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_IS_AUTHORIZED,
                                   g_param_spec_boolean ("is-authorized",
                                                         _("Is Authorized"),
                                                         _("Whether the process is authorized"),
                                                         FALSE,
                                                         G_PARAM_READABLE |
                                                         G_PARAM_STATIC_NAME |
                                                         G_PARAM_STATIC_NICK |
                                                         G_PARAM_STATIC_BLURB));

  /**
   * PolkitLockButton:is-visible:
   *
   * Whether the widget is visible.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_IS_VISIBLE,
                                   g_param_spec_boolean ("is-visible",
                                                         _("Is Visible"),
                                                         _("Whether the widget is visible"),
                                                         TRUE,
                                                         G_PARAM_READABLE |
                                                         G_PARAM_STATIC_NAME |
                                                         G_PARAM_STATIC_NICK |
                                                         G_PARAM_STATIC_BLURB));

  /**
   * PolkitLockButton:can-obtain:
   *
   * Whether authorization can be obtained.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_CAN_OBTAIN,
                                   g_param_spec_boolean ("can-obtain",
                                                         _("Can Obtain"),
                                                         _("Whether authorization can be obtained"),
                                                         FALSE,
                                                         G_PARAM_READABLE |
                                                         G_PARAM_STATIC_NAME |
                                                         G_PARAM_STATIC_NICK |
                                                         G_PARAM_STATIC_BLURB));

  /**
   * PolkitLockButton:text-unlock:
   *
   * The text to display when prompting the user to unlock.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_TEXT_UNLOCK,
                                   g_param_spec_string ("text-unlock",
                                                        _("Unlock Text"),
                                                        _("The text to display when prompting the user to unlock."),
                                                        _("Click to make changes"),
                                                        G_PARAM_READWRITE |
                                                        G_PARAM_CONSTRUCT |
                                                        G_PARAM_STATIC_NAME |
                                                        G_PARAM_STATIC_NICK |
                                                        G_PARAM_STATIC_BLURB));

  /**
   * PolkitLockButton:text-lock:
   *
   * The text to display when prompting the user to lock.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_TEXT_LOCK,
                                   g_param_spec_string ("text-lock",
                                                        _("Lock Text"),
                                                        _("The text to display when prompting the user to lock."),
                                                        _("Click to prevent changes"),
                                                        G_PARAM_READWRITE |
                                                        G_PARAM_CONSTRUCT |
                                                        G_PARAM_STATIC_NAME |
                                                        G_PARAM_STATIC_NICK |
                                                        G_PARAM_STATIC_BLURB));

  /**
   * PolkitLockButton:text-not-authorized:
   *
   * The text to display when the user cannot obtain authorization through authentication.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_TEXT_NOT_AUTHORIZED,
                                   g_param_spec_string ("text-not-authorized",
                                                        _("Unlock Text"),
                                                        _("The text to display when the user cannot obtain authorization through authentication."),
                                                        _("Not authorized to make changes"),
                                                        G_PARAM_READWRITE |
                                                        G_PARAM_CONSTRUCT |
                                                        G_PARAM_STATIC_NAME |
                                                        G_PARAM_STATIC_NICK |
                                                        G_PARAM_STATIC_BLURB));

  /**
   * PolkitLockButton::changed:
   * @button: A #PolkitLockButton.
   *
   * Emitted when something on @button changes.
   */
  signals[CHANGED_SIGNAL] = g_signal_new ("changed",
                                          G_TYPE_FROM_CLASS (klass),
                                          G_SIGNAL_RUN_LAST,
                                          G_STRUCT_OFFSET (PolkitLockButtonClass, changed),
                                          NULL,
                                          NULL,
                                          g_cclosure_marshal_VOID__VOID,
                                          G_TYPE_NONE,
                                          0);

}

/**
 * polkit_lock_button_new:
 * @action_id: An action identifer.
 *
 * Constructs a #PolkitLockButton for @action_id.
 *
 * Returns: A #PolkitLockButton.
 */
GtkWidget *
polkit_lock_button_new (const gchar *action_id)
{
  g_return_val_if_fail (action_id != NULL, NULL);

  return GTK_WIDGET (g_object_new (POLKIT_TYPE_LOCK_BUTTON,
                                   "action-id", action_id,
                                   NULL));
}

static void
update_state (PolkitLockButton *button)
{
  const gchar *text;
  gboolean active;
  gboolean sensitive;
  gboolean old_hidden;

  old_hidden = button->priv->hidden;
  button->priv->hidden = FALSE;

  if (button->priv->authorized)
    {
      text = button->priv->text_lock;
      active = TRUE;
      sensitive = TRUE;
      /* if the authorization isn't temporary, then hide all the controls */
      if (button->priv->tmp_authz_id == NULL)
        button->priv->hidden = TRUE;
    }
  else
    {
      active = FALSE;
      if (button->priv->can_obtain)
        {
          text = button->priv->text_unlock;
          g_free (button->priv->tmp_authz_id);
          button->priv->tmp_authz_id = NULL;
          sensitive = TRUE;
        }
      else
        {
          text = button->priv->text_not_authorized;
          g_free (button->priv->tmp_authz_id);
          button->priv->tmp_authz_id = NULL;
          sensitive = FALSE;
        }
    }

  gtk_label_set_text (GTK_LABEL (button->priv->label), text);
  button->priv->ignore_toggled_signal = TRUE;
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button->priv->toggle_button), active);
  button->priv->ignore_toggled_signal = FALSE;
  gtk_widget_set_sensitive (button->priv->toggle_button, sensitive);

  if (button->priv->hidden)
    {
      gtk_widget_hide (button->priv->toggle_button);
      gtk_widget_hide (button->priv->label);
    }
  else
    {
      gtk_widget_show (button->priv->toggle_button);
      gtk_widget_show (button->priv->label);
    }

  if (old_hidden != button->priv->hidden)
    g_object_notify (G_OBJECT (button), "is-visible");
}

static void
on_authority_changed (PolkitAuthority *authority,
                      gpointer         user_data)
{
  PolkitLockButton *button = POLKIT_LOCK_BUTTON (user_data);
  initiate_check (button);
}

static void
process_result (PolkitLockButton          *button,
                PolkitAuthorizationResult *result)
{
  gboolean old_can_obtain;
  gboolean old_authorized;
  PolkitDetails *details;

  old_can_obtain = button->priv->can_obtain;
  old_authorized = button->priv->authorized;
  button->priv->can_obtain = polkit_authorization_result_get_is_challenge (result);
  button->priv->authorized = polkit_authorization_result_get_is_authorized (result);

  /* save the temporary authorization id */
  details = polkit_authorization_result_get_details (result);
  if (details != NULL)
    {
      g_free (button->priv->tmp_authz_id);
      button->priv->tmp_authz_id = g_strdup (polkit_details_lookup (details,
                                                                    "polkit.temporary_authorization_id"));
    }

  update_state (button);

  if (old_can_obtain != button->priv->can_obtain ||
      old_authorized != button->priv->authorized)
    {
      g_signal_emit (button,
                     signals[CHANGED_SIGNAL],
                     0);
    }

  if (old_can_obtain != button->priv->can_obtain)
    g_object_notify (G_OBJECT (button), "can-obtain");

  if (old_authorized != button->priv->authorized)
    g_object_notify (G_OBJECT (button), "is-authorized");
}

static void
check_cb (GObject       *source_object,
          GAsyncResult  *res,
          gpointer       user_data)
{
  PolkitAuthority *authority = POLKIT_AUTHORITY (source_object);
  PolkitLockButton *button = POLKIT_LOCK_BUTTON (user_data);
  PolkitAuthorizationResult *result;
  GError *error;

  error = NULL;
  result = polkit_authority_check_authorization_finish (authority,
                                                        res,
                                                        &error);
  if (error != NULL)
    {
      g_warning ("Error checking authorization for action id `%s': %s",
                 button->priv->action_id,
                 error->message);
      g_error_free (error);
    }
  else
    {
      process_result (button, result);
    }

  if (result != NULL)
    g_object_unref (result);

  if (button->priv->check_cancellable != NULL)
    {
      g_object_unref (button->priv->check_cancellable);
      button->priv->check_cancellable = NULL;
    }
}

static void
initiate_check (PolkitLockButton *button)
{

  /* if we have a check pending already, then do nothing */
  if (button->priv->check_cancellable != NULL)
    goto out;

  button->priv->check_cancellable = g_cancellable_new ();

  polkit_authority_check_authorization (button->priv->authority,
                                        button->priv->subject,
                                        button->priv->action_id,
                                        NULL, /* PolkitDetails */
                                        POLKIT_CHECK_AUTHORIZATION_FLAGS_NONE,
                                        button->priv->check_cancellable,
                                        check_cb,
                                        button);

 out:
  ;
}

static void
do_sync_check (PolkitLockButton *button)
{
  GError *error;
  PolkitAuthorizationResult *result;

  error = NULL;
  result = polkit_authority_check_authorization_sync (button->priv->authority,
                                                      button->priv->subject,
                                                      button->priv->action_id,
                                                      NULL, /* PolkitDetails */
                                                      POLKIT_CHECK_AUTHORIZATION_FLAGS_NONE,
                                                      NULL, /* cancellable */
                                                      &error);
  if (error != NULL)
    {
      g_warning ("Error sync-checking authorization for action id `%s': %s",
                 button->priv->action_id,
                 error->message);
      g_error_free (error);
    }
  else
    {
      process_result (button, result);
    }

  if (result != NULL)
    g_object_unref (result);
}

static void
interactive_check_cb (GObject       *source_object,
                      GAsyncResult  *res,
                      gpointer       user_data)
{
  PolkitAuthority *authority = POLKIT_AUTHORITY (source_object);
  PolkitLockButton *button = POLKIT_LOCK_BUTTON (user_data);
  PolkitAuthorizationResult *result;
  PolkitDetails *details;
  GError *error;

  error = NULL;
  result = polkit_authority_check_authorization_finish (authority,
                                                        res,
                                                        &error);
  if (error != NULL)
    {
      g_warning ("Error obtaining authorization for action id `%s': %s",
                 button->priv->action_id,
                 error->message);
      g_error_free (error);
      goto out;
    }

  /* state is updated in the ::changed signal handler */

  /* save the temporary authorization id */
  details = polkit_authorization_result_get_details (result);
  if (details != NULL)
    {
      button->priv->tmp_authz_id = g_strdup (polkit_details_lookup (details,
                                                                    "polkit.temporary_authorization_id"));
    }

 out:
  if (result != NULL)
    g_object_unref (result);

  if (button->priv->interactive_check_cancellable != NULL)
    {
      g_object_unref (button->priv->interactive_check_cancellable);
      button->priv->interactive_check_cancellable = NULL;
    }
}

static void
on_toggled (GtkToggleButton *toggle_button,
            gpointer         user_data)
{
  PolkitLockButton *button = POLKIT_LOCK_BUTTON (user_data);

  if (button->priv->ignore_toggled_signal)
    goto out;

  if (!button->priv->authorized && button->priv->can_obtain)
    {
      /* if we already have a pending interactive check, then do nothing */
      if (button->priv->interactive_check_cancellable != NULL)
        goto out;

      button->priv->interactive_check_cancellable = g_cancellable_new ();

      polkit_authority_check_authorization (button->priv->authority,
                                            button->priv->subject,
                                            button->priv->action_id,
                                            NULL, /* PolkitDetails */
                                            POLKIT_CHECK_AUTHORIZATION_FLAGS_ALLOW_USER_INTERACTION,
                                            button->priv->interactive_check_cancellable,
                                            interactive_check_cb,
                                            button);
    }
  else if (button->priv->authorized && button->priv->tmp_authz_id != NULL)
    {
      polkit_authority_revoke_temporary_authorization_by_id (button->priv->authority,
                                                             button->priv->tmp_authz_id,
                                                             NULL,  /* cancellable */
                                                             NULL,  /* callback */
                                                             NULL); /* user_data */
    }

 out:

  update_state (button);
}

/**
 * polkit_lock_button_get_is_authorized:
 * @button: A #PolkitLockButton.
 *
 * Gets whether the process is authorized.
 *
 * Returns: %TRUE if authorized.
 */
gboolean
polkit_lock_button_get_is_authorized (PolkitLockButton *button)
{
  g_return_val_if_fail (POLKIT_IS_LOCK_BUTTON (button), FALSE);
  return button->priv->authorized;
}

/**
 * polkit_lock_button_get_can_obtain:
 * @button: A #PolkitLockButton.
 *
 * Gets whether the user can obtain an authorization through
 * authentication.
 *
 * Returns: Whether the authorization is obtainable.
 */
gboolean
polkit_lock_button_get_can_obtain (PolkitLockButton *button)
{
  g_return_val_if_fail (POLKIT_IS_LOCK_BUTTON (button), FALSE);
  return button->priv->can_obtain;
}

/**
 * polkit_lock_button_get_is_visible:
 * @button: A #PolkitLockButton.
 *
 * Gets whether @button is currently being shown.
 *
 * Returns: %TRUE if @button has visible UI elements.
 */
gboolean
polkit_lock_button_get_is_visible (PolkitLockButton *button)
{
  g_return_val_if_fail (POLKIT_IS_LOCK_BUTTON (button), FALSE);
  return ! button->priv->hidden;
}

/**
 * polkit_lock_button_set_unlock_text:
 * @button: A #PolkitLockButton.
 * @text: The text to set.
 *
 * Makes @button display @text when the prompting the user to unlock.
 */
void
polkit_lock_button_set_unlock_text (PolkitLockButton *button,
                                    const gchar      *text)
{
  g_return_if_fail (POLKIT_IS_LOCK_BUTTON (button));
  g_return_if_fail (text != NULL);

  if (button->priv->text_unlock != NULL)
    {
      button->priv->text_unlock = g_strdup (text);
      update_state (button);
    }
  else
    {
      button->priv->text_unlock = g_strdup (text);
    }
}

/**
 * polkit_lock_button_set_lock_text:
 * @button: A #PolkitLockButton.
 * @text: The text to set.
 *
 * Makes @button display @text when the prompting the user to unlock.
 */
void
polkit_lock_button_set_lock_text (PolkitLockButton *button,
                                  const gchar      *text)
{
  g_return_if_fail (POLKIT_IS_LOCK_BUTTON (button));
  g_return_if_fail (text != NULL);

  if (button->priv->text_lock != NULL)
    {
      button->priv->text_lock = g_strdup (text);
      update_state (button);
    }
  else
    {
      button->priv->text_lock = g_strdup (text);
    }
}

/**
 * polkit_lock_button_set_not_authorized_text:
 * @button: A #PolkitLockButton.
 * @text: The text to set.
 *
 * Makes @button display @text when the prompting the user to unlock.
 */
void
polkit_lock_button_set_not_authorized_text (PolkitLockButton *button,
                                            const gchar      *text)
{
  g_return_if_fail (POLKIT_IS_LOCK_BUTTON (button));
  g_return_if_fail (text != NULL);

  if (button->priv->text_not_authorized != NULL)
    {
      button->priv->text_not_authorized = g_strdup (text);
      update_state (button);
    }
  else
    {
      button->priv->text_not_authorized = g_strdup (text);
    }
}
