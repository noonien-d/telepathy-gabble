/*
 * gabble-roster-channel.c - Source for GabbleRosterChannel
 * Copyright (C) 2005, 2006 Collabora Ltd.
 * Copyright (C) 2005 Nokia Corporation
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <dbus/dbus-glib.h>
#include <stdio.h>
#include <stdlib.h>

#define DEBUG_FLAG GABBLE_DEBUG_ROSTER

#include "debug.h"
#include "gabble-connection.h"
#include "group-mixin.h"
#include "handle-set.h"
#include "roster.h"
#include <telepathy-glib/tp-errors.h>
#include "telepathy-helpers.h"
#include <telepathy-glib/tp-interfaces.h>
#include "tp-channel-iface.h"
#include "util.h"

#include "gabble-roster-channel.h"
#include "gabble-roster-channel-glue.h"
#include "gabble-roster-channel-signals-marshal.h"

G_DEFINE_TYPE_WITH_CODE (GabbleRosterChannel, gabble_roster_channel,
    G_TYPE_OBJECT, G_IMPLEMENT_INTERFACE (TP_TYPE_CHANNEL_IFACE, NULL));

/* signal enum */
enum
{
    CLOSED,
    GROUP_FLAGS_CHANGED,
    MEMBERS_CHANGED,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

/* properties */
enum
{
  PROP_OBJECT_PATH = 1,
  PROP_CHANNEL_TYPE,
  PROP_HANDLE_TYPE,
  PROP_HANDLE,
  PROP_CONNECTION,
  LAST_PROPERTY
};

/* private structure */
typedef struct _GabbleRosterChannelPrivate GabbleRosterChannelPrivate;

struct _GabbleRosterChannelPrivate
{
  GabbleConnection *conn;
  char *object_path;
  TpHandle handle;
  guint handle_type;

  gboolean dispose_has_run;
};

#define GABBLE_ROSTER_CHANNEL_GET_PRIVATE(obj) \
    ((GabbleRosterChannelPrivate *)obj->priv)

static void
gabble_roster_channel_init (GabbleRosterChannel *self)
{
  GabbleRosterChannelPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GABBLE_TYPE_ROSTER_CHANNEL, GabbleRosterChannelPrivate);

  self->priv = priv;

  /* allocate any data required by the object here */
}

static GObject *
gabble_roster_channel_constructor (GType type, guint n_props,
                                   GObjectConstructParam *props)
{
  GObject *obj;
  GabbleRosterChannelPrivate *priv;
  DBusGConnection *bus;
  GabbleHandleRepo *handles;
  gboolean valid;
  TpHandle self_handle;
  guint handle_type;

  obj = G_OBJECT_CLASS (gabble_roster_channel_parent_class)->
           constructor (type, n_props, props);
  priv = GABBLE_ROSTER_CHANNEL_GET_PRIVATE (GABBLE_ROSTER_CHANNEL (obj));
  handle_type = priv->handle_type;
  handles = priv->conn->handles;
  self_handle = priv->conn->self_handle;

  /* register object on the bus */
  bus = tp_get_bus ();
  dbus_g_connection_register_g_object (bus, priv->object_path, obj);

  /* ref our list handle */
  valid = gabble_handle_ref (handles, handle_type, priv->handle);
  g_assert (valid);

  /* initialize group mixin */
  gabble_group_mixin_init (obj, G_STRUCT_OFFSET (GabbleRosterChannel, group),
                           handles, self_handle);
  if (handle_type == TP_HANDLE_TYPE_GROUP)
    {
      gabble_group_mixin_change_flags (obj,
          TP_CHANNEL_GROUP_FLAG_CAN_ADD |
          TP_CHANNEL_GROUP_FLAG_CAN_REMOVE,
          0);
    }
  else if (handle_type != TP_HANDLE_TYPE_LIST)
    {
      g_assert_not_reached ();
    }
  /* magic contact lists from here down... */
  else if (GABBLE_LIST_HANDLE_PUBLISH == priv->handle)
    {
      gabble_group_mixin_change_flags (obj,
          TP_CHANNEL_GROUP_FLAG_CAN_REMOVE |
          TP_CHANNEL_GROUP_FLAG_MESSAGE_ACCEPT |
          TP_CHANNEL_GROUP_FLAG_MESSAGE_REMOVE,
          0);
    }
  else if (GABBLE_LIST_HANDLE_SUBSCRIBE == priv->handle)
    {
      gabble_group_mixin_change_flags (obj,
          TP_CHANNEL_GROUP_FLAG_CAN_ADD |
          TP_CHANNEL_GROUP_FLAG_CAN_REMOVE |
          TP_CHANNEL_GROUP_FLAG_CAN_RESCIND |
          TP_CHANNEL_GROUP_FLAG_MESSAGE_ADD |
          TP_CHANNEL_GROUP_FLAG_MESSAGE_REMOVE |
          TP_CHANNEL_GROUP_FLAG_MESSAGE_RESCIND,
          0);
    }
  else if (GABBLE_LIST_HANDLE_KNOWN == priv->handle)
    {
      gabble_group_mixin_change_flags (obj,
          TP_CHANNEL_GROUP_FLAG_CAN_REMOVE,
          0);
    }
  else if (GABBLE_LIST_HANDLE_DENY == priv->handle)
    {
      gabble_group_mixin_change_flags (obj,
          TP_CHANNEL_GROUP_FLAG_CAN_ADD |
          TP_CHANNEL_GROUP_FLAG_CAN_REMOVE,
          0);
    }
  else
    {
      g_assert_not_reached ();
    }

  return obj;
}

static void
gabble_roster_channel_get_property (GObject    *object,
                                    guint       property_id,
                                    GValue     *value,
                                    GParamSpec *pspec)
{
  GabbleRosterChannel *chan = GABBLE_ROSTER_CHANNEL (object);
  GabbleRosterChannelPrivate *priv = GABBLE_ROSTER_CHANNEL_GET_PRIVATE (chan);

  switch (property_id) {
    case PROP_OBJECT_PATH:
      g_value_set_string (value, priv->object_path);
      break;
    case PROP_CHANNEL_TYPE:
      g_value_set_static_string (value, TP_IFACE_CHANNEL_TYPE_CONTACT_LIST);
      break;
    case PROP_HANDLE_TYPE:
      g_value_set_uint (value, priv->handle_type);
      break;
    case PROP_HANDLE:
      g_value_set_uint (value, priv->handle);
      break;
    case PROP_CONNECTION:
      g_value_set_object (value, priv->conn);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
gabble_roster_channel_set_property (GObject     *object,
                                    guint        property_id,
                                    const GValue *value,
                                    GParamSpec   *pspec)
{
  GabbleRosterChannel *chan = GABBLE_ROSTER_CHANNEL (object);
  GabbleRosterChannelPrivate *priv = GABBLE_ROSTER_CHANNEL_GET_PRIVATE (chan);

  switch (property_id) {
    case PROP_OBJECT_PATH:
      g_free (priv->object_path);
      priv->object_path = g_value_dup_string (value);
      break;
    case PROP_HANDLE_TYPE:
      priv->handle_type = g_value_get_uint (value);
      break;
    case PROP_HANDLE:
      priv->handle = g_value_get_uint (value);
      break;
    case PROP_CONNECTION:
      priv->conn = g_value_get_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void gabble_roster_channel_dispose (GObject *object);
static void gabble_roster_channel_finalize (GObject *object);

static gboolean _gabble_roster_channel_add_member_cb (GObject *obj, TpHandle handle, const gchar *message, GError **error);
static gboolean _gabble_roster_channel_remove_member_cb (GObject *obj, TpHandle handle, const gchar *message, GError **error);

static void
gabble_roster_channel_class_init (GabbleRosterChannelClass *gabble_roster_channel_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (gabble_roster_channel_class);
  GParamSpec *param_spec;

  g_type_class_add_private (gabble_roster_channel_class, sizeof (GabbleRosterChannelPrivate));

  object_class->constructor = gabble_roster_channel_constructor;

  object_class->get_property = gabble_roster_channel_get_property;
  object_class->set_property = gabble_roster_channel_set_property;

  object_class->dispose = gabble_roster_channel_dispose;
  object_class->finalize = gabble_roster_channel_finalize;

  param_spec = g_param_spec_object ("connection", "GabbleConnection object",
                                    "Gabble connection object that owns this "
                                    "Roster channel object.",
                                    GABBLE_TYPE_CONNECTION,
                                    G_PARAM_CONSTRUCT_ONLY |
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NICK |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_CONNECTION, param_spec);

  g_object_class_override_property (object_class, PROP_OBJECT_PATH, "object-path");
  g_object_class_override_property (object_class, PROP_CHANNEL_TYPE, "channel-type");
  g_object_class_override_property (object_class, PROP_HANDLE_TYPE, "handle-type");
  g_object_class_override_property (object_class, PROP_HANDLE, "handle");

  signals[CLOSED] =
    g_signal_new ("closed",
                  G_OBJECT_CLASS_TYPE (gabble_roster_channel_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);

  gabble_group_mixin_class_init (object_class,
                                 G_STRUCT_OFFSET (GabbleRosterChannelClass, group_class),
                                 _gabble_roster_channel_add_member_cb,
                                 _gabble_roster_channel_remove_member_cb);

  dbus_g_object_type_install_info (G_TYPE_FROM_CLASS (gabble_roster_channel_class), &dbus_glib_gabble_roster_channel_object_info);
}

void
gabble_roster_channel_dispose (GObject *object)
{
  GabbleRosterChannel *self = GABBLE_ROSTER_CHANNEL (object);
  GabbleRosterChannelPrivate *priv = GABBLE_ROSTER_CHANNEL_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  g_signal_emit(self, signals[CLOSED], 0);

  /* release any references held by the object here */

  if (G_OBJECT_CLASS (gabble_roster_channel_parent_class)->dispose)
    G_OBJECT_CLASS (gabble_roster_channel_parent_class)->dispose (object);
}

void
gabble_roster_channel_finalize (GObject *object)
{
  GabbleRosterChannel *self = GABBLE_ROSTER_CHANNEL (object);
  GabbleRosterChannelPrivate *priv = GABBLE_ROSTER_CHANNEL_GET_PRIVATE (self);

  /* free any data held directly by the object here */

  g_free (priv->object_path);

  gabble_handle_unref (priv->conn->handles, priv->handle_type, priv->handle);

  gabble_group_mixin_finalize (object);

  G_OBJECT_CLASS (gabble_roster_channel_parent_class)->finalize (object);
}


static gboolean
_gabble_roster_channel_send_presence (GabbleRosterChannel *chan,
                                      LmMessageSubType sub_type,
                                      TpHandle handle,
                                      const gchar *status,
                                      GError **error)
{
  GabbleRosterChannelPrivate *priv;
  GabbleHandleRepo *repo;
  const char *contact;
  LmMessage *message;
  gboolean result;

  priv = GABBLE_ROSTER_CHANNEL_GET_PRIVATE (chan);
  repo = priv->conn->handles;
  contact = gabble_handle_inspect (repo, TP_HANDLE_TYPE_CONTACT, handle);

  message = lm_message_new_with_sub_type (contact,
      LM_MESSAGE_TYPE_PRESENCE,
      sub_type);

  if (LM_MESSAGE_SUB_TYPE_SUBSCRIBE == sub_type)
    lm_message_node_add_own_nick (message->node, priv->conn);

  if (status != NULL && status[0] != '\0')
    lm_message_node_add_child (message->node, "status", status);

  result = _gabble_connection_send (priv->conn, message, error);

  lm_message_unref (message);

  return result;
}


/**
 * _gabble_roster_channel_add_member_cb
 *
 * Called by the group mixin to add one member.
 */
static gboolean
_gabble_roster_channel_add_member_cb (GObject *obj,
                                      TpHandle handle,
                                      const gchar *message,
                                      GError **error)
{
  GabbleRosterChannelPrivate *priv;
  GabbleHandleRepo *repo;
  gboolean ret = FALSE;

  priv = GABBLE_ROSTER_CHANNEL_GET_PRIVATE (GABBLE_ROSTER_CHANNEL (obj));

  repo = priv->conn->handles;

  DEBUG ("called on %s with handle %u (%s) \"%s\"", gabble_handle_inspect (repo, priv->handle_type, priv->handle), handle,
      gabble_handle_inspect (repo, TP_HANDLE_TYPE_CONTACT, handle), message);

  if (TP_HANDLE_TYPE_GROUP == priv->handle_type)
    {
      ret = gabble_roster_handle_add_to_group (priv->conn->roster,
          handle, priv->handle, error);
    }
  else if (TP_HANDLE_TYPE_LIST != priv->handle_type)
    {
      g_assert_not_reached ();
      return FALSE;
    }
  /* "magic" contact lists, from here down... */
  /* publish list */
  else if (GABBLE_LIST_HANDLE_PUBLISH == priv->handle)
    {
      /* send <presence type="subscribed"> */
      ret = _gabble_roster_channel_send_presence (GABBLE_ROSTER_CHANNEL (obj),
          LM_MESSAGE_SUB_TYPE_SUBSCRIBED, handle, message, error);
    }
  /* subscribe list */
  else if (GABBLE_LIST_HANDLE_SUBSCRIBE == priv->handle)
    {
      /* add item to the roster (GTalk depends on this, clearing the H flag) */
      gabble_roster_handle_add (priv->conn->roster, handle, NULL);

      /* send <presence type="subscribe"> */
      ret = _gabble_roster_channel_send_presence (GABBLE_ROSTER_CHANNEL (obj),
          LM_MESSAGE_SUB_TYPE_SUBSCRIBE, handle, message, error);
    }
  /* deny list */
  else if (GABBLE_LIST_HANDLE_DENY == priv->handle)
    {
      /* block contact */
      ret = gabble_roster_handle_set_blocked (priv->conn->roster, handle, TRUE,
          error);
    }
  else
    {
      g_assert_not_reached ();
    }

  return ret;
}


/**
 * _gabble_roster_channel_remove_member_cb
 *
 * Called by the group mixin to remove one member.
 */
static gboolean
_gabble_roster_channel_remove_member_cb (GObject *obj,
                                         TpHandle handle,
                                         const gchar *message,
                                         GError **error)
{
  GabbleRosterChannelPrivate *priv;
  GabbleHandleRepo *repo;
  gboolean ret = FALSE;

  priv = GABBLE_ROSTER_CHANNEL_GET_PRIVATE (GABBLE_ROSTER_CHANNEL (obj));

  repo = priv->conn->handles;

  DEBUG ("called on %s with handle %u (%s) \"%s\"", gabble_handle_inspect (repo, priv->handle_type, priv->handle), handle,
      gabble_handle_inspect (repo, TP_HANDLE_TYPE_CONTACT, handle), message);

  if (TP_HANDLE_TYPE_GROUP == priv->handle_type)
    {
      ret = gabble_roster_handle_remove_from_group (priv->conn->roster,
          handle, priv->handle, error);
    }
  else if (TP_HANDLE_TYPE_LIST != priv->handle_type)
    {
      g_assert_not_reached ();
      return FALSE;
    }
  /* "magic" contact lists, from here down... */
  /* publish list */
  else if (GABBLE_LIST_HANDLE_PUBLISH == priv->handle)
    {
      /* send <presence type="unsubscribed"> */
      ret = _gabble_roster_channel_send_presence (GABBLE_ROSTER_CHANNEL (obj),
          LM_MESSAGE_SUB_TYPE_UNSUBSCRIBED, handle, message, error);

      /* remove it from local_pending here, because roster callback doesn't
         know if it can (subscription='none' is used both during request and
         when it's rejected) */
      if (handle_set_is_member (GABBLE_ROSTER_CHANNEL (obj)->group.local_pending, handle))
        {
          TpIntSet *rem = tp_intset_new ();

          tp_intset_add (rem, handle);
          gabble_group_mixin_change_members (obj, "", NULL, rem, NULL, NULL,
              0, 0);

          tp_intset_destroy (rem);
        }
    }
  /* subscribe list */
  else if (GABBLE_LIST_HANDLE_SUBSCRIBE == priv->handle)
    {
      /* send <presence type="unsubscribe"> */
      ret = _gabble_roster_channel_send_presence (GABBLE_ROSTER_CHANNEL (obj),
          LM_MESSAGE_SUB_TYPE_UNSUBSCRIBE, handle, message, error);
    }
  /* known list */
  else if (GABBLE_LIST_HANDLE_KNOWN == priv->handle)
    {
      /* send roster subscription=remove IQ */
      ret = gabble_roster_handle_remove (priv->conn->roster, handle, error);
    }
  /* deny list */
  else if (GABBLE_LIST_HANDLE_DENY == priv->handle)
    {
      /* unblock contact */
      ret = gabble_roster_handle_set_blocked (priv->conn->roster, handle, FALSE,
          error);
    }
  else
    {
      g_assert_not_reached ();
    }

  return ret;
}


/**
 * gabble_roster_channel_add_members
 *
 * Implements D-Bus method AddMembers
 * on interface org.freedesktop.Telepathy.Channel.Interface.Group
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
gabble_roster_channel_add_members (GabbleRosterChannel *self,
                                   const GArray *contacts,
                                   const gchar *message,
                                   GError **error)
{
  return gabble_group_mixin_add_members (G_OBJECT (self), contacts, message,
      error);
}


/**
 * gabble_roster_channel_close
 *
 * Implements D-Bus method Close
 * on interface org.freedesktop.Telepathy.Channel
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
gabble_roster_channel_close (GabbleRosterChannel *self,
                             GError **error)
{
  g_set_error (error, TELEPATHY_ERRORS, TpError_NotImplemented,
      "you may not close contact list channels");

  return FALSE;
}


/**
 * gabble_roster_channel_get_all_members
 *
 * Implements D-Bus method GetAllMembers
 * on interface org.freedesktop.Telepathy.Channel.Interface.Group
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
gabble_roster_channel_get_all_members (GabbleRosterChannel *self,
                                       GArray **ret,
                                       GArray **ret1,
                                       GArray **ret2,
                                       GError **error)
{
  return gabble_group_mixin_get_all_members (G_OBJECT (self), ret, ret1, ret2,
      error);
}


/**
 * gabble_roster_channel_get_channel_type
 *
 * Implements D-Bus method GetChannelType
 * on interface org.freedesktop.Telepathy.Channel
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
gabble_roster_channel_get_channel_type (GabbleRosterChannel *self,
                                        gchar **ret,
                                        GError **error)
{
  *ret = g_strdup (TP_IFACE_CHANNEL_TYPE_CONTACT_LIST);

  return TRUE;
}


/**
 * gabble_roster_channel_get_group_flags
 *
 * Implements D-Bus method GetGroupFlags
 * on interface org.freedesktop.Telepathy.Channel.Interface.Group
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
gabble_roster_channel_get_group_flags (GabbleRosterChannel *self,
                                       guint *ret,
                                       GError **error)
{
  return gabble_group_mixin_get_group_flags (G_OBJECT (self), ret, error);
}


/**
 * gabble_roster_channel_get_handle
 *
 * Implements D-Bus method GetHandle
 * on interface org.freedesktop.Telepathy.Channel
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
gabble_roster_channel_get_handle (GabbleRosterChannel *self,
                                  guint *ret,
                                  guint *ret1,
                                  GError **error)
{
  GabbleRosterChannelPrivate *priv;

  g_assert (GABBLE_IS_ROSTER_CHANNEL (self));

  priv = GABBLE_ROSTER_CHANNEL_GET_PRIVATE (self);

  *ret = priv->handle_type;
  *ret1 = priv->handle;

  return TRUE;
}


/**
 * gabble_roster_channel_get_handle_owners
 *
 * Implements D-Bus method GetHandleOwners
 * on interface org.freedesktop.Telepathy.Channel.Interface.Group
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
gabble_roster_channel_get_handle_owners (GabbleRosterChannel *self,
                                         const GArray *handles,
                                         GArray **ret,
                                         GError **error)
{
  return gabble_group_mixin_get_handle_owners (G_OBJECT (self), handles, ret,
      error);
}


/**
 * gabble_roster_channel_get_interfaces
 *
 * Implements D-Bus method GetInterfaces
 * on interface org.freedesktop.Telepathy.Channel
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
gabble_roster_channel_get_interfaces (GabbleRosterChannel *self,
                                      gchar ***ret,
                                      GError **error)
{
  const char *interfaces[] = { TP_IFACE_CHANNEL_INTERFACE_GROUP, NULL };

  *ret = g_strdupv ((gchar **) interfaces);

  return TRUE;
}


/**
 * gabble_roster_channel_get_local_pending_members
 *
 * Implements D-Bus method GetLocalPendingMembers
 * on interface org.freedesktop.Telepathy.Channel.Interface.Group
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
gabble_roster_channel_get_local_pending_members (GabbleRosterChannel *self,
                                                 GArray **ret,
                                                 GError **error)
{
  return gabble_group_mixin_get_local_pending_members (G_OBJECT (self), ret,
      error);
}


/**
 * gabble_roster_channel_get_members
 *
 * Implements D-Bus method GetMembers
 * on interface org.freedesktop.Telepathy.Channel.Interface.Group
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
gabble_roster_channel_get_members (GabbleRosterChannel *self,
                                   GArray **ret,
                                   GError **error)
{
  return gabble_group_mixin_get_members (G_OBJECT (self), ret, error);
}


/**
 * gabble_roster_channel_get_remote_pending_members
 *
 * Implements D-Bus method GetRemotePendingMembers
 * on interface org.freedesktop.Telepathy.Channel.Interface.Group
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
gabble_roster_channel_get_remote_pending_members (GabbleRosterChannel *self,
                                                  GArray **ret,
                                                  GError **error)
{
  return gabble_group_mixin_get_remote_pending_members (G_OBJECT (self), ret,
      error);
}


/**
 * gabble_roster_channel_get_self_handle
 *
 * Implements D-Bus method GetSelfHandle
 * on interface org.freedesktop.Telepathy.Channel.Interface.Group
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
gabble_roster_channel_get_self_handle (GabbleRosterChannel *self,
                                       guint *ret,
                                       GError **error)
{
  return gabble_group_mixin_get_self_handle (G_OBJECT (self), ret, error);
}


/**
 * gabble_roster_channel_remove_members
 *
 * Implements D-Bus method RemoveMembers
 * on interface org.freedesktop.Telepathy.Channel.Interface.Group
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
gabble_roster_channel_remove_members (GabbleRosterChannel *self,
                                      const GArray *contacts,
                                      const gchar *message,
                                      GError **error)
{
  return gabble_group_mixin_remove_members (G_OBJECT (self), contacts, message,
      error);
}

