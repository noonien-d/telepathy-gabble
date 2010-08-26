/*
 * util.c - Source for Gabble utility functions
 * Copyright (C) 2006-2007 Collabora Ltd.
 * Copyright (C) 2006-2007 Nokia Corporation
 *   @author Robert McQueen <robert.mcqueen@collabora.co.uk>
 *   @author Simon McVittie <simon.mcvittie@collabora.co.uk>
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

#include "config.h"
#include "util.h"
#include "gabble/disco-identity.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gobject/gvaluecollector.h>

#include <wocky/wocky-utils.h>
#include <telepathy-glib/handle-repo-dynamic.h>
#include <telepathy-glib/dbus.h>

#include <extensions/extensions.h>

#include <uuid.h>

#define DEBUG_FLAG GABBLE_DEBUG_JID

#include "base64.h"
#include "conn-aliasing.h"
#include "connection.h"
#include "debug.h"
#include "namespaces.h"
#include "presence-cache.h"

gchar *
sha1_hex (const gchar *bytes,
          guint len)
{
  gchar *hex = g_compute_checksum_for_string (G_CHECKSUM_SHA1, bytes, len);
  guint i;

  for (i = 0; i < SHA1_HASH_SIZE * 2; i++)
    {
      g_assert (hex[i] != '\0');
      hex[i] = g_ascii_tolower (hex[i]);
    }

  g_assert (hex[SHA1_HASH_SIZE * 2] == '\0');

  return hex;
}

void
sha1_bin (const gchar *bytes,
          guint len,
          guchar out[SHA1_HASH_SIZE])
{
  GChecksum *checksum = g_checksum_new (G_CHECKSUM_SHA1);
  gsize out_len = SHA1_HASH_SIZE;

  g_assert (g_checksum_type_get_length (G_CHECKSUM_SHA1) == SHA1_HASH_SIZE);
  g_checksum_update (checksum, (const guchar *) bytes, len);
  g_checksum_get_digest (checksum, out, &out_len);
  g_assert (out_len == SHA1_HASH_SIZE);
  g_checksum_free (checksum);
}

gchar *
gabble_generate_id (void)
{
  /* generate random UUIDs */
  uuid_t uu;
  gchar *str;

  str = g_new0 (gchar, 37);
  uuid_generate_random (uu);
  uuid_unparse_lower (uu, str);
  return str;
}

static void
lm_message_node_add_nick (LmMessageNode *node, const gchar *nick)
{
  LmMessageNode *nick_node;

  nick_node = lm_message_node_add_child (node, "nick", nick);
  lm_message_node_set_attribute (nick_node, "xmlns", NS_NICK);
}

void
lm_message_node_add_own_nick (LmMessageNode *node,
                              GabbleConnection *connection)
{
  gchar *nick;
  GabbleConnectionAliasSource source;
  TpBaseConnection *base = (TpBaseConnection *) connection;

  source = _gabble_connection_get_cached_alias (connection,
        base->self_handle, &nick);

  if (source > GABBLE_CONNECTION_ALIAS_FROM_JID)
    lm_message_node_add_nick (node, nick);

  g_free (nick);
}

void
lm_message_node_steal_children (LmMessageNode *snatcher,
                                LmMessageNode *mum)
{
  g_return_if_fail (snatcher->children == NULL);

  if (mum->children == NULL)
    return;

  snatcher->children = mum->children;
  mum->children = NULL;
}

/* variant of lm_message_node_get_child() which ignores node namespace
 * prefix */
LmMessageNode *
lm_message_node_get_child_any_ns (LmMessageNode *node, const gchar *name)
{
  NodeIter i;

  for (i = node_iter (node); i; i = node_iter_next (i))
    {
      LmMessageNode *child = node_iter_data (i);

      if (!tp_strdiff (lm_message_node_get_name (child), name))
          return child;
    }

  return NULL;
}

const gchar *
lm_message_node_get_namespace (LmMessageNode *node)
{
  return wocky_node_get_ns (node);
}

const gchar *
lm_message_node_get_name (LmMessageNode *node)
{
  return node->name;
}

gboolean
lm_message_node_has_namespace (LmMessageNode *node,
                               const gchar *ns,
                               const gchar *tag)
{
  return (!tp_strdiff (lm_message_node_get_namespace (node), ns));
}

LmMessageNode *
lm_message_node_get_child_with_namespace (LmMessageNode *node,
                                          const gchar *name,
                                          const gchar *ns)
{
  LmMessageNode *found;
  NodeIter i;

  found = wocky_node_get_child_ns (node, name, ns);
  if (found != NULL)
    return found;

  for (i = node_iter (node); i; i = node_iter_next (i))
    {
      LmMessageNode *child = node_iter_data (i);

      found = lm_message_node_get_child_with_namespace (child, name, ns);
      if (found != NULL)
        return found;
    }

  return NULL;
}

/* note: these are only used internally for readability, not part of the API
 */
enum {
    BUILD_END = '\0',
    BUILD_ATTRIBUTE = '@',
    BUILD_CHILD = '(',
    BUILD_CHILD_END = ')',
    BUILD_POINTER = '*',
};

/* lm_message_node_add_build_va
 *
 * Used to implement lm_message_build and lm_message_build_with_sub_type.
 */
static void
lm_message_node_add_build_va (LmMessageNode *node, guint spec, va_list ap)
{
  GSList *stack = NULL;
  guint arg = spec;

  stack = g_slist_prepend (stack, node);

  while (arg != BUILD_END)
    {
      switch (arg)
        {
        case BUILD_ATTRIBUTE:
          {
            gchar *key = va_arg (ap, gchar *);
            gchar *value = va_arg (ap, gchar *);

            g_return_if_fail (key != NULL);
            g_return_if_fail (value != NULL);
            lm_message_node_set_attribute (stack->data, key, value);
          }
          break;

        case BUILD_CHILD:
          {
            gchar *name = va_arg (ap, gchar *);
            gchar *value = va_arg (ap, gchar *);
            LmMessageNode *child;

            g_return_if_fail (name != NULL);
            g_return_if_fail (value != NULL);
            child = lm_message_node_add_child (stack->data, name, value);
            stack = g_slist_prepend (stack, child);
          }
          break;

        case BUILD_CHILD_END:
          {
            GSList *tmp;

            tmp = stack;
            stack = stack->next;
            tmp->next = NULL;
            g_slist_free (tmp);
          }
          break;

        case BUILD_POINTER:
          {
            LmMessageNode **assign_to = va_arg (ap, LmMessageNode **);

            g_return_if_fail (assign_to != NULL);
            *assign_to = stack->data;
          }
          break;

        default:
          g_assert_not_reached ();
        }

      /* Note that we pull out an int-sized value here, whereas our sentinel,
       * NULL, is pointer-sized. However, sizeof (void *) should always be >=
       * sizeof (uint), so this shouldn't cause a problem.
       */
      arg = va_arg (ap, guint);
    }

  g_slist_free (stack);
}

/**
 * lm_message_build:
 *
 * Build an LmMessage from a list of arguments employing an S-expression-like
 * notation. Example:
 *
 * lm_message_build ("bob@jabber.org", LM_MESSAGE_TYPE_IQ,
 *   '(', 'query', 'lala',
 *      '@', 'xmlns', 'http://jabber.org/protocol/foo',
 *   ')',
 *   NULL);
 *
 * --> <iq to="bob@jabber.org">
 *        <query xmlns="http://jabber.org/protocol/foo">lala</query>
 *     </iq>
 */
G_GNUC_NULL_TERMINATED
LmMessage *
lm_message_build (const gchar *to, LmMessageType type, guint spec, ...)
{
  LmMessage *msg;
  va_list ap;

  msg = lm_message_new (to, type);
  va_start (ap, spec);
  lm_message_node_add_build_va (
      wocky_stanza_get_top_node (msg), spec, ap);
  va_end (ap);
  return msg;
}

/**
 * lm_message_build_with_sub_type:
 *
 * As lm_message_build (), but creates a message with an LmMessageSubType.
 */
G_GNUC_NULL_TERMINATED
LmMessage *
lm_message_build_with_sub_type (const gchar *to, LmMessageType type,
    LmMessageSubType sub_type, guint spec, ...)
{
  LmMessage *msg;
  va_list ap;

  msg = lm_message_new_with_sub_type (to, type, sub_type);
  va_start (ap, spec);
  lm_message_node_add_build_va (
      wocky_stanza_get_top_node (msg), spec, ap);
  va_end (ap);
  return msg;
}

/**
 * gabble_decode_jid
 *
 * Parses a JID which may be one of the following forms:
 *
 *  domain
 *  domain/resource
 *  node@domain
 *  node@domain/resource
 *
 * If the JID is valid, returns TRUE and sets the caller's
 * node/domain/resource pointers if they are not NULL. The node and resource
 * pointers will be set to NULL if the respective part is not present in the
 * JID. The node and domain are lower-cased because the Jabber protocol treats
 * them case-insensitively.
 *
 * XXX: Do nodeprep/resourceprep and length checking.
 *
 * See RFC 3920 §3.
 */
gboolean
gabble_decode_jid (const gchar *jid,
                   gchar **node,
                   gchar **domain,
                   gchar **resource)
{
  return wocky_decode_jid (jid, node, domain, resource);
}

/**
 * gabble_get_room_handle_from_jid:
 * @room_repo: The %TP_HANDLE_TYPE_ROOM handle repository
 * @jid: A JID
 *
 * Given a JID seen in the from="" attribute on a stanza, work out whether
 * it's something to do with a MUC, and if so, return its handle.
 *
 * Returns: The handle of the MUC, if the JID refers to either a MUC
 *    we're in, or a contact's channel-specific JID inside a MUC.
 *    Returns 0 if the JID is either invalid, or nothing to do with a
 *    known MUC (typically this will mean it's the global JID of a contact).
 */
TpHandle
gabble_get_room_handle_from_jid (TpHandleRepoIface *room_repo,
                                 const gchar *jid)
{
  TpHandle handle;
  gchar *room;

  room = gabble_remove_resource (jid);
  if (room == NULL)
    return 0;

  handle = tp_handle_lookup (room_repo, room, NULL, NULL);
  g_free (room);
  return handle;
}

#define INVALID_HANDLE(e, f, ...) \
  G_STMT_START { \
  DEBUG (f, ##__VA_ARGS__); \
  g_set_error (e, TP_ERRORS, TP_ERROR_INVALID_HANDLE, f, ##__VA_ARGS__);\
  } G_STMT_END

gchar *
gabble_normalize_room (TpHandleRepoIface *repo,
                       const gchar *jid,
                       gpointer context,
                       GError **error)
{
  GabbleConnection *conn;
  gchar *qualified_name, *resource;

  /* Only look up the canonical room name if we got a GabbleConnection.
   * This should only happen in the test-handles test. */
  if (context != NULL)
    {
      conn = GABBLE_CONNECTION (context);
      qualified_name = gabble_connection_get_canonical_room_name (conn, jid);

      if (qualified_name == NULL)
        {
          INVALID_HANDLE (error,
              "requested room handle %s does not specify a server, but we "
              "have not discovered any local conference servers and no "
              "fallback was provided", jid);
          return NULL;
        }
    }
  else
    {
      qualified_name = g_strdup (jid);
    }

  if (!gabble_decode_jid (qualified_name, NULL, NULL, &resource))
    {
      INVALID_HANDLE (error, "room JID %s is invalid", qualified_name);
      return NULL;
    }

  if (resource != NULL)
    {
      INVALID_HANDLE (error,
          "invalid room JID %s: contains nickname part after '/' too",
          qualified_name);
      g_free (qualified_name);
      g_free (resource);
      return NULL;
    }

  return qualified_name;
}

gchar *
gabble_remove_resource (const gchar *jid)
{
  char *slash = strchr (jid, '/');
  gchar *buf;

  if (slash == NULL)
    return g_strdup (jid);

  /* The user and domain parts can't contain '/', assuming it's valid */
  buf = g_malloc (slash - jid + 1);
  strncpy (buf, jid, slash - jid);
  buf[slash - jid] = '\0';

  return buf;
}

gchar *
gabble_encode_jid (
    const gchar *node,
    const gchar *domain,
    const gchar *resource)
{
  gchar *tmp, *ret;

  g_return_val_if_fail (domain != NULL, NULL);

  if (node != NULL && resource != NULL)
    tmp = g_strdup_printf ("%s@%s/%s", node, domain, resource);
  else if (node != NULL)
    tmp = g_strdup_printf ("%s@%s", node, domain);
  else if (resource != NULL)
    tmp = g_strdup_printf ("%s/%s", domain, resource);
  else
    tmp = g_strdup (domain);

  ret = g_utf8_normalize (tmp, -1, G_NORMALIZE_NFKC);
  g_free (tmp);
  return ret;
}

gchar *
gabble_normalize_contact (TpHandleRepoIface *repo,
                          const gchar *jid,
                          gpointer context,
                          GError **error)
{
  guint mode = GPOINTER_TO_UINT (context);
  gchar *username = NULL, *server = NULL, *resource = NULL;
  gchar *ret = NULL;

  if (!gabble_decode_jid (jid, &username, &server, &resource) || !username)
    {
      INVALID_HANDLE (error,
          "JID %s is invalid or has no node part", jid);
      goto OUT;
    }

  if (mode == GABBLE_JID_ROOM_MEMBER && resource == NULL)
    {
      INVALID_HANDLE (error,
          "JID %s can't be a room member - it has no resource", jid);
      goto OUT;
    }

  if (mode != GABBLE_JID_GLOBAL && resource != NULL)
    {
      ret = gabble_encode_jid (username, server, resource);

      if (mode == GABBLE_JID_ROOM_MEMBER
          || (repo != NULL
              && tp_dynamic_handle_repo_lookup_exact (repo, ret)))
        {
          /* either we know from context that it's a room member, or we
           * already saw that contact in a room. Use ret as our answer
           */
          goto OUT;
        }
      else
        {
          g_free (ret);
        }
    }

  /* if we get here, we suspect it's a global JID, either because the context
   * says it is, or because the context isn't sure and we haven't seen it in
   * use as a room member
   */
  ret = gabble_encode_jid (username, server, NULL);

OUT:
  g_free (username);
  g_free (server);
  g_free (resource);
  return ret;
}

/**
 * lm_message_node_extract_properties
 *
 * Map a XML node to a properties hash table
 * (used to parse a subset of the OLPC and tubes protocol)
 *
 * Example:
 *
 * <node>
 *   <prop name="prop1" type="str">prop1_value</prop>
 *   <prop name="prop2" type="uint">7</prop>
 * </node>
 *
 * lm_message_node_extract_properties (node, "prop");
 *
 * --> { "prop1" : "prop1_value", "prop2" : 7 }
 *
 * Returns a hash table mapping names to GValue of the specified type.
 * Valid types are: str, int, uint, bytes.
 *
 */
GHashTable *
lm_message_node_extract_properties (LmMessageNode *node,
                                    const gchar *prop)
{
  GHashTable *properties;
  NodeIter i;

  properties = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
      (GDestroyNotify) tp_g_value_slice_free);

  if (node == NULL)
    return properties;

  for (i = node_iter (node); i; i = node_iter_next (i))
    {
      LmMessageNode *child = node_iter_data (i);
      const gchar *name;
      const gchar *type;
      const gchar *value;
      GValue *gvalue;

      if (0 != strcmp (child->name, prop))
        continue;

      name = lm_message_node_get_attribute (child, "name");

      if (!name)
        continue;

      type = lm_message_node_get_attribute (child, "type");
      value = lm_message_node_get_value (child);

      if (type == NULL || value == NULL)
        continue;

      if (0 == strcmp (type, "bytes"))
        {
          GArray *arr;
          GString *decoded;

          decoded = base64_decode (value);
          if (!decoded)
            continue;

          arr = g_array_new (FALSE, FALSE, sizeof (guchar));
          g_array_append_vals (arr, decoded->str, decoded->len);
          gvalue = g_slice_new0 (GValue);
          g_value_init (gvalue, DBUS_TYPE_G_UCHAR_ARRAY);
          g_value_take_boxed (gvalue, arr);
          g_hash_table_insert (properties, g_strdup (name), gvalue);
          g_string_free (decoded, TRUE);
        }
      else if (0 == strcmp (type, "str"))
        {
          gvalue = g_slice_new0 (GValue);
          g_value_init (gvalue, G_TYPE_STRING);
          g_value_set_string (gvalue, value);
          g_hash_table_insert (properties, g_strdup (name), gvalue);
        }
      else if (0 == strcmp (type, "int"))
        {
          gvalue = g_slice_new0 (GValue);
          g_value_init (gvalue, G_TYPE_INT);
          g_value_set_int (gvalue, strtol (value, NULL, 10));
          g_hash_table_insert (properties, g_strdup (name), gvalue);
        }
      else if (0 == strcmp (type, "uint"))
        {
          gvalue = g_slice_new0 (GValue);
          g_value_init (gvalue, G_TYPE_UINT);
          g_value_set_uint (gvalue, strtoul (value, NULL, 10));
          g_hash_table_insert (properties, g_strdup (name), gvalue);
        }
      else if (0 == strcmp (type, "bool"))
        {
          gboolean val;

          if (!tp_strdiff (value, "0") || !tp_strdiff (value, "false"))
            {
              val = FALSE;
            }
          else if (!tp_strdiff (value, "1") || !tp_strdiff (value, "true"))
            {
              val = TRUE;
            }
          else
            {
              DEBUG ("invalid boolean value: %s", value);
              continue;
            }

          gvalue = g_slice_new0 (GValue);
          g_value_init (gvalue, G_TYPE_BOOLEAN);
          g_value_set_boolean (gvalue, val);
          g_hash_table_insert (properties, g_strdup (name), gvalue);
        }
    }

  return properties;
}

struct _set_child_from_property_data
{
  LmMessageNode *node;
  const gchar *prop;
};

static void
set_child_from_property (gpointer key,
                         gpointer value,
                         gpointer user_data)
{
  GValue *gvalue = value;
  struct _set_child_from_property_data *data =
    (struct _set_child_from_property_data *) user_data;
  LmMessageNode *child;
  const char *type = NULL;

  if (G_VALUE_TYPE (gvalue) == G_TYPE_STRING)
    {
      type = "str";
    }
  else if (G_VALUE_TYPE (gvalue) == DBUS_TYPE_G_UCHAR_ARRAY)
    {
      type = "bytes";
    }
  else if (G_VALUE_TYPE (gvalue) == G_TYPE_INT)
    {
      type = "int";
    }
  else if (G_VALUE_TYPE (gvalue) == G_TYPE_UINT)
    {
      type = "uint";
    }
  else if (G_VALUE_TYPE (gvalue) == G_TYPE_BOOLEAN)
    {
      type = "bool";
    }
  else
    {
      /* a type we don't know how to handle: ignore it */
      DEBUG ("property with unknown type \"%s\"",
          g_type_name (G_VALUE_TYPE (gvalue)));
      return;
    }

  child = lm_message_node_add_child (data->node, data->prop, "");

  if (G_VALUE_TYPE (gvalue) == G_TYPE_STRING)
    {
      lm_message_node_set_value (child,
        g_value_get_string (gvalue));
    }
  else if (G_VALUE_TYPE (gvalue) == DBUS_TYPE_G_UCHAR_ARRAY)
    {
      GArray *arr;
      gchar *str;

      type = "bytes";
      arr = g_value_get_boxed (gvalue);
      str = base64_encode (arr->len, arr->data, FALSE);
      lm_message_node_set_value (child, str);

      g_free (str);
    }
  else if (G_VALUE_TYPE (gvalue) == G_TYPE_INT)
    {
      gchar *str;

      str = g_strdup_printf ("%d", g_value_get_int (gvalue));
      lm_message_node_set_value (child, str);

      g_free (str);
    }
  else if (G_VALUE_TYPE (gvalue) == G_TYPE_UINT)
    {
      gchar *str;

      str = g_strdup_printf ("%u", g_value_get_uint (gvalue));
      lm_message_node_set_value (child, str);

      g_free (str);
    }
  else if (G_VALUE_TYPE (gvalue) == G_TYPE_BOOLEAN)
    {
      /* we output as "0" or "1" despite the canonical representation for
       * xs:boolean being "false" or "true", for compatibility with older
       * Gabble versions (OLPC Trial-3) */
      lm_message_node_set_value (child,
          g_value_get_boolean (gvalue) ? "1" : "0");
    }
  else
    {
      g_assert_not_reached ();
    }

  lm_message_node_set_attribute (child, "name", key);
  lm_message_node_set_attribute (child, "type", type);
}

/**
 *
 * lm_message_node_set_children_from_properties
 *
 * Map a properties hash table to a XML node.
 *
 * Example:
 *
 * properties = { "prop1" : "prop1_value", "prop2" : 7 }
 *
 * lm_message_node_add_children_from_properties (node, properties, "prop");
 *
 * --> <node>
 *       <prop name="prop1" type="str">prop1_value</prop>
 *       <prop name="prop2" type="uint">7</prop>
 *     </node>
 *
 */
void
lm_message_node_add_children_from_properties (LmMessageNode *node,
                                              GHashTable *properties,
                                              const gchar *prop)
{
  struct _set_child_from_property_data data;

  data.node = node;
  data.prop = prop;

  g_hash_table_foreach (properties, set_child_from_property, &data);
}

/**
 * lm_iq_message_make_result:
 * @iq_message: A LmMessage containing an IQ stanza to acknowledge
 *
 * Creates a result IQ stanza to acknowledge @iq_message.
 *
 * Returns: A newly-created LmMessage containing the result IQ stanza.
 */
LmMessage *
lm_iq_message_make_result (LmMessage *iq_message)
{
  LmMessage *result;
  LmMessageNode *iq, *result_iq;
  const gchar *from_jid, *id;

  g_assert (lm_message_get_type (iq_message) == LM_MESSAGE_TYPE_IQ);
  g_assert (lm_message_get_sub_type (iq_message) == LM_MESSAGE_SUB_TYPE_GET ||
            lm_message_get_sub_type (iq_message) == LM_MESSAGE_SUB_TYPE_SET);

  iq = lm_message_get_node (iq_message);
  id = lm_message_node_get_attribute (iq, "id");

  if (id == NULL)
    {
      NODE_DEBUG (iq, "can't acknowledge IQ with no id");
      return NULL;
    }

  from_jid = lm_message_node_get_attribute (iq, "from");

  result = lm_message_new_with_sub_type (from_jid, LM_MESSAGE_TYPE_IQ,
                                         LM_MESSAGE_SUB_TYPE_RESULT);
  result_iq = lm_message_get_node (result);
  lm_message_node_set_attribute (result_iq, "id", id);

  return result;
}

typedef struct {
    GObject *instance;
    GObject *user_data;
    gulong handler_id;
} WeakHandlerCtx;

static WeakHandlerCtx *
whc_new (GObject *instance,
         GObject *user_data)
{
  WeakHandlerCtx *ctx = g_slice_new0 (WeakHandlerCtx);

  ctx->instance = instance;
  ctx->user_data = user_data;

  return ctx;
}

static void
whc_free (WeakHandlerCtx *ctx)
{
  g_slice_free (WeakHandlerCtx, ctx);
}

static void user_data_destroyed_cb (gpointer, GObject *);

static void
instance_destroyed_cb (gpointer ctx_,
                       GObject *where_the_instance_was)
{
  WeakHandlerCtx *ctx = ctx_;

  DEBUG ("instance for %p destroyed; cleaning up", ctx);

  /* No need to disconnect the signal here, the instance has gone away. */
  g_object_weak_unref (ctx->user_data, user_data_destroyed_cb, ctx);
  whc_free (ctx);
}

static void
user_data_destroyed_cb (gpointer ctx_,
                        GObject *where_the_user_data_was)
{
  WeakHandlerCtx *ctx = ctx_;

  DEBUG ("user_data for %p destroyed; disconnecting", ctx);

  g_signal_handler_disconnect (ctx->instance, ctx->handler_id);
  g_object_weak_unref (ctx->instance, instance_destroyed_cb, ctx);
  whc_free (ctx);
}

/**
 * gabble_signal_connect_weak:
 * @instance: the instance to connect to.
 * @detailed_signal: a string of the form "signal-name::detail".
 * @c_handler: the GCallback to connect.
 * @user_data: an object to pass as data to c_handler calls.
 *
 * Connects a #GCallback function to a signal for a particular object, as if
 * with g_signal_connect(). Additionally, arranges for the signal handler to be
 * disconnected if @user_data is destroyed.
 *
 * This is intended to be a convenient way for objects to use themselves as
 * user_data for callbacks without having to explicitly disconnect all the
 * handlers in their finalizers.
 */
void
gabble_signal_connect_weak (gpointer instance,
                            const gchar *detailed_signal,
                            GCallback c_handler,
                            GObject *user_data)
{
  GObject *instance_obj = G_OBJECT (instance);
  WeakHandlerCtx *ctx = whc_new (instance_obj, user_data);

  DEBUG ("connecting to %p:%s with context %p", instance, detailed_signal, ctx);

  ctx->handler_id = g_signal_connect (instance, detailed_signal, c_handler,
      user_data);

  g_object_weak_ref (instance_obj, instance_destroyed_cb, ctx);
  g_object_weak_ref (user_data, user_data_destroyed_cb, ctx);
}

typedef struct {
    GSourceFunc function;
    GObject *object;
    guint source_id;
} WeakIdleCtx;

static void
idle_weak_ref_notify (gpointer data,
                      GObject *dead_object)
{
  g_source_remove (GPOINTER_TO_UINT (data));
}

static void
idle_removed (gpointer data)
{
  WeakIdleCtx *ctx = (WeakIdleCtx *) data;

  g_slice_free (WeakIdleCtx, ctx);
}

static gboolean
idle_callback (gpointer data)
{
  WeakIdleCtx *ctx = (WeakIdleCtx *) data;

  if (ctx->function ((gpointer) ctx->object))
    {
      return TRUE;
    }
  else
    {
      g_object_weak_unref (
          ctx->object, idle_weak_ref_notify, GUINT_TO_POINTER (ctx->source_id));
      return FALSE;
    }
}

/* Like g_idle_add(), but cancel the callback if the provided object is
 * finalized.
 */
guint
gabble_idle_add_weak (GSourceFunc function,
                      GObject *object)
{
  WeakIdleCtx *ctx;

  ctx = g_slice_new0 (WeakIdleCtx);
  ctx->function = function;
  ctx->object = object;
  ctx->source_id = g_idle_add_full (
      G_PRIORITY_DEFAULT_IDLE, idle_callback, ctx, idle_removed);

  g_object_weak_ref (
      object, idle_weak_ref_notify, GUINT_TO_POINTER (ctx->source_id));
  return ctx->source_id;
}

typedef struct {
    gchar *key;
    gchar *value;
} Attribute;

const gchar *
lm_message_node_get_attribute_with_namespace (LmMessageNode *node,
    const gchar *attribute,
    const gchar *ns)
{
  return wocky_node_get_attribute_ns (node, attribute, ns);
}

GPtrArray *
gabble_g_ptr_array_copy (GPtrArray *source)
{
  GPtrArray *ret = g_ptr_array_sized_new (source->len);
  guint i;

  for (i = 0; i < source->len; i++)
    g_ptr_array_add (ret, g_ptr_array_index (source, i));

  return ret;
}

WockyBareContact *
ensure_bare_contact_from_jid (GabbleConnection *conn,
    const gchar *jid)
{
  WockyContactFactory *contact_factory;

  contact_factory = wocky_session_get_contact_factory (conn->session);
  return wocky_contact_factory_ensure_bare_contact (contact_factory, jid);
}

#define TWICE(x) x, x

static gboolean
jingle_pick_resource_or_bare_jid (GabblePresence *presence,
    GabbleCapabilitySet *caps, const gchar **resource)
{
  const gchar *ret;

  if (gabble_presence_has_resources (presence))
    {
      ret = gabble_presence_pick_resource_by_caps (presence,
          PREFER_PHONES,
          gabble_capability_set_predicate_at_least, caps);

      if (resource != NULL)
        *resource = ret;

      return (ret != NULL);
    }
  else if (gabble_capability_set_at_least (
        gabble_presence_peek_caps (presence), caps))
    {
      if (resource != NULL)
        *resource = NULL;

      return TRUE;
    }
  else
    {
      return FALSE;
    }
}

gboolean
jingle_pick_best_resource (GabbleConnection *conn,
    TpHandle peer,
    gboolean want_audio,
    gboolean want_video,
    const char **transport_ns,
    JingleDialect *dialect,
    const gchar **resource_out)
{
  /* We prefer gtalk-p2p to ice, because it can use tcp and https relays (if
   * available). */
  static const GabbleFeatureFallback transports[] = {
        { TRUE, TWICE (NS_GOOGLE_TRANSPORT_P2P) },
        { TRUE, TWICE (NS_JINGLE_TRANSPORT_ICEUDP) },
        { TRUE, TWICE (NS_JINGLE_TRANSPORT_RAWUDP) },
        { FALSE, NULL, NULL }
  };
  GabblePresence *presence;
  GabbleCapabilitySet *caps;
  const gchar *resource = NULL;
  gboolean success = FALSE;

  presence = gabble_presence_cache_get (conn->presence_cache, peer);

  if (presence == NULL)
    {
      DEBUG ("contact %d has no presence available", peer);
      return FALSE;
    }

  *dialect = JINGLE_DIALECT_ERROR;
  *transport_ns = NULL;

  g_return_val_if_fail (want_audio || want_video, FALSE);

  /* from here on, goto FINALLY to free this, instead of returning early */
  caps = gabble_capability_set_new ();

  /* Try newest Jingle standard */
  gabble_capability_set_add (caps, NS_JINGLE_RTP);

  if (want_audio)
    gabble_capability_set_add (caps, NS_JINGLE_RTP_AUDIO);
  if (want_video)
    gabble_capability_set_add (caps, NS_JINGLE_RTP_VIDEO);

  if (jingle_pick_resource_or_bare_jid (presence, caps, &resource))
    {
      *dialect = JINGLE_DIALECT_V032;
      goto CHOOSE_TRANSPORT;
    }

  /* Else try older Jingle draft */
  gabble_capability_set_clear (caps);

  if (want_audio)
    gabble_capability_set_add (caps, NS_JINGLE_DESCRIPTION_AUDIO);
  if (want_video)
    gabble_capability_set_add (caps, NS_JINGLE_DESCRIPTION_VIDEO);

  if (jingle_pick_resource_or_bare_jid (presence, caps, &resource))
    {
      *dialect = JINGLE_DIALECT_V015;
      goto CHOOSE_TRANSPORT;
    }

  /* The Google dialects can't do video alone. */
  if (!want_audio)
    {
      DEBUG ("No resource which supports video alone available");
      goto FINALLY;
    }

  /* Okay, let's try GTalk 0.3, possibly with video. */
  gabble_capability_set_clear (caps);
  gabble_capability_set_add (caps, NS_GOOGLE_FEAT_VOICE);

  if (want_video)
    gabble_capability_set_add (caps, NS_GOOGLE_FEAT_VIDEO);

  if (jingle_pick_resource_or_bare_jid (presence, caps, &resource))
    {
      *dialect = JINGLE_DIALECT_GTALK3;
      goto CHOOSE_TRANSPORT;
    }

  if (want_video)
    {
      DEBUG ("No resource which supports audio+video available");
      goto FINALLY;
    }

  /* Maybe GTalk 0.4 will save us all... ? */
  gabble_capability_set_clear (caps);
  gabble_capability_set_add (caps, NS_GOOGLE_FEAT_VOICE);
  gabble_capability_set_add (caps, NS_GOOGLE_TRANSPORT_P2P);

  if (jingle_pick_resource_or_bare_jid (presence, caps, &resource))
    {
      *dialect = JINGLE_DIALECT_GTALK4;
      goto CHOOSE_TRANSPORT;
    }

  /* Nope, nothing we can do. */
  goto FINALLY;

CHOOSE_TRANSPORT:

  if (resource_out != NULL)
    *resource_out = resource;

  success = TRUE;

  if (*dialect == JINGLE_DIALECT_GTALK4 || *dialect == JINGLE_DIALECT_GTALK3)
    {
      /* the GTalk dialects only support google p2p as transport protocol. */
      *transport_ns = NS_GOOGLE_TRANSPORT_P2P;
    }
  else if (resource == NULL)
    {
      *transport_ns = gabble_presence_pick_best_feature (presence, transports,
          gabble_capability_set_predicate_has);
    }
  else
    {
      *transport_ns = gabble_presence_resource_pick_best_feature (presence,
        resource, transports, gabble_capability_set_predicate_has);
    }

  if (*transport_ns == NULL)
    success = FALSE;

FINALLY:
  gabble_capability_set_free (caps);
  return success;
}

const gchar *
jingle_pick_best_content_type (GabbleConnection *conn,
  TpHandle peer,
  const gchar *resource,
  JingleMediaType type)
{
  GabblePresence *presence;
  const GabbleFeatureFallback content_types[] = {
      /* if $thing is supported, then use it */
        { TRUE, TWICE (NS_JINGLE_RTP) },
        { type == JINGLE_MEDIA_TYPE_VIDEO,
            TWICE (NS_JINGLE_DESCRIPTION_VIDEO) },
        { type == JINGLE_MEDIA_TYPE_AUDIO,
            TWICE (NS_JINGLE_DESCRIPTION_AUDIO) },
      /* odd Google ones: if $thing is supported, use $other_thing */
        { type == JINGLE_MEDIA_TYPE_AUDIO,
          NS_GOOGLE_FEAT_VOICE, NS_GOOGLE_SESSION_PHONE },
        { type == JINGLE_MEDIA_TYPE_VIDEO,
          NS_GOOGLE_FEAT_VIDEO, NS_GOOGLE_SESSION_VIDEO },
        { FALSE, NULL, NULL }
  };

  presence = gabble_presence_cache_get (conn->presence_cache, peer);

  if (presence == NULL)
    {
      DEBUG ("contact %d has no presence available", peer);
      return NULL;
    }

  if (resource == NULL)
    {
      return gabble_presence_pick_best_feature (presence, content_types,
          gabble_capability_set_predicate_has);
    }
  else
    {
      return gabble_presence_resource_pick_best_feature (presence, resource,
          content_types, gabble_capability_set_predicate_has);
    }
}

GPtrArray *
gabble_call_candidates_to_array (GList *candidates)
{
  GPtrArray *arr;
  GList *c;

  arr = g_ptr_array_sized_new (g_list_length (candidates));

  for (c = candidates; c != NULL; c = g_list_next (c))
    {
        JingleCandidate *cand = (JingleCandidate *) c->data;
        GValueArray *a;
        GHashTable *info;

        info = tp_asv_new (
          "Protocol", G_TYPE_UINT, cand->protocol,
          "Type", G_TYPE_UINT, cand->type,
          "Foundation", G_TYPE_STRING, cand->id,
          "Priority", G_TYPE_UINT,
            (guint) cand->preference * 65536,
          "Username", G_TYPE_STRING, cand->username,
          "Password", G_TYPE_STRING, cand->password,
          NULL);

         a = tp_value_array_build (4,
            G_TYPE_UINT, cand->component,
            G_TYPE_STRING, cand->address,
            G_TYPE_UINT, cand->port,
            GABBLE_HASH_TYPE_CANDIDATE_INFO, info,
            G_TYPE_INVALID);

        g_ptr_array_add (arr, a);
  }

  return arr;
}

gchar *
gabble_peer_to_jid (GabbleConnection *conn,
    TpHandle peer,
    const gchar *resource)
{
  TpHandleRepoIface *repo = tp_base_connection_get_handles (
    TP_BASE_CONNECTION (conn), TP_HANDLE_TYPE_CONTACT);
  const gchar *target = tp_handle_inspect (repo, peer);

  if (resource == NULL)
    return g_strdup (target);

  return g_strdup_printf ("%s/%s", target, resource);
}

GabbleDiscoIdentity *
gabble_disco_identity_new (const gchar *category,
    const gchar *type,
    const gchar *lang,
    const gchar *name)
{
  GabbleDiscoIdentity *ret;

  g_return_val_if_fail (category != NULL, NULL);
  g_return_val_if_fail (type != NULL, NULL);

  ret = g_slice_new (GabbleDiscoIdentity);
  ret->category = g_strdup (category);
  ret->type = g_strdup (type);
  ret->lang = g_strdup (lang);
  ret->name = g_strdup (name);
  return ret;
}

GabbleDiscoIdentity *
gabble_disco_identity_copy (const GabbleDiscoIdentity *source)
{
  GabbleDiscoIdentity *ret = g_new (GabbleDiscoIdentity, 1);

  ret->category = g_strdup (source->category);
  ret->type = g_strdup (source->type);
  ret->lang = g_strdup (source->lang);
  ret->name = g_strdup (source->name);
  return ret;
}

const gchar *
gabble_disco_identity_get_category (GabbleDiscoIdentity *identity)
{
  return identity->category;
}

const gchar *
gabble_disco_identity_get_type (GabbleDiscoIdentity *identity)
{
  return identity->type;
}

const gchar *
gabble_disco_identity_get_lang (GabbleDiscoIdentity *identity)
{
  return identity->lang;
}

const gchar *
gabble_disco_identity_get_name (GabbleDiscoIdentity *identity)
{
  return identity->name;
}

void
gabble_disco_identity_free (GabbleDiscoIdentity *identity)
{
  if (identity == NULL)
    return;

  g_free (identity->category);
  g_free (identity->type);
  g_free (identity->lang);
  g_free (identity->name);
  g_slice_free (GabbleDiscoIdentity, identity);
}

/**
 * gabble_disco_identity_array_new:
 *
 * Creates a new array of GabbleDiscoIdentity objects.
 *
 * Returns: A newly instantiated array.
 * See: gabble_disco_identity_array_free()
 */
GPtrArray *
gabble_disco_identity_array_new (void)
{
  return g_ptr_array_new_with_free_func (
      (GDestroyNotify) gabble_disco_identity_free);
}

/**
 * gabble_disco_identity_array_copy():
 * @source: The source array to be copied.
 *
 * Copies an array of GabbleDiscoIdentity objects. The returned array contains
 * new copies of the contents of the source array.
 *
 * Returns: A newly instantiated array with new copies of the contents of the
 *          source array.
 * See: gabble_disco_identity_array_new()
 */
GPtrArray *
gabble_disco_identity_array_copy (const GPtrArray *source)
{
  GPtrArray *ret;
  guint i;

  if (!source)
    return NULL;

  ret = g_ptr_array_sized_new (source->len);
  g_ptr_array_set_free_func (ret, (GDestroyNotify) gabble_disco_identity_free);
  for (i = 0; i < source->len; ++i)
    {
      g_ptr_array_add (ret,
         gabble_disco_identity_copy (g_ptr_array_index (source, i)));
    }
  return ret;
}

/**
 * gabble_disco_identity_array_free():
 * @arr: Array to be freed.
 *
 * Frees an array of GabbleDiscoIdentity objects created with
 * gabble_disco_identity_array_new() or returned by
 * gabble_disco_identity_array_copy().
 *
 * Note that if this method is called with an array created with
 * g_ptr_array_new, the caller should also free the array contents.
 *
 * See: gabble_disco_identity_array_new(), gabble_disco_identity_array_copy()
 */
void
gabble_disco_identity_array_free (GPtrArray *arr)
{
  if (!arr)
    return;

  g_ptr_array_free (arr, TRUE);
}

/**
 * gabble_simple_async_succeed_or_fail_in_idle:
 * @self: the source object for an asynchronous function
 * @callback: a callback to call when @todo things have been done
 * @user_data: user data for the callback
 * @source_tag: the source tag for a #GSimpleAsyncResult
 * @error: (allow-none): %NULL to indicate success, or an error on failure
 *
 * Create a new #GSimpleAsyncResult and schedule it to call its callback
 * in an idle. If @error is %NULL, report success with
 * tp_simple_async_report_success_in_idle(); if @error is non-%NULL,
 * use g_simple_async_report_gerror_in_idle().
 */
void
gabble_simple_async_succeed_or_fail_in_idle (gpointer self,
    GAsyncReadyCallback callback,
    gpointer user_data,
    gpointer source_tag,
    const GError *error)
{
  if (error == NULL)
    {
      tp_simple_async_report_success_in_idle (self, callback, user_data,
          source_tag);
    }
  else
    {
      /* not const-correct yet: GNOME #622004 */
      g_simple_async_report_gerror_in_idle (self, callback, user_data,
          (GError *) error);
    }
}

/**
 * gabble_simple_async_countdown_new:
 * @self: the source object for an asynchronous function
 * @callback: a callback to call when @todo things have been done
 * @user_data: user data for the callback
 * @source_tag: the source tag for a #GSimpleAsyncResult
 * @todo: number of things to do before calling @callback (at least 1)
 *
 * Create a new #GSimpleAsyncResult that will call its callback when a number
 * of asynchronous operations have happened.
 *
 * An internal counter is initialized to @todo, incremented with
 * gabble_simple_async_countdown_inc() or decremented with
 * gabble_simple_async_countdown_dec().
 *
 * When that counter reaches zero, if an error has been set with
 * g_simple_async_result_set_from_error() or similar, the operation fails;
 * otherwise, it succeeds.
 *
 * The caller must not use the operation result functions, such as
 * g_simple_async_result_get_op_res_gssize() - this async result is only
 * suitable for "void" async methods which return either success or a #GError,
 * i.e. the same signature as g_async_initable_init_async().
 *
 * Returns: (transfer full): a counter
 */
GSimpleAsyncResult *
gabble_simple_async_countdown_new (gpointer self,
    GAsyncReadyCallback callback,
    gpointer user_data,
    gpointer source_tag,
    gssize todo)
{
  GSimpleAsyncResult *simple;

  g_return_val_if_fail (todo >= 1, NULL);

  simple = g_simple_async_result_new (self, callback, user_data, source_tag);
  /* We (ab)use the op_res member as a count of things to do. When
   * it reaches zero, the operation completes with any error that has been
   * set, or with success. */
  g_simple_async_result_set_op_res_gssize (simple, todo);

  /* we keep one extra reference as long as the counter is nonzero */
  g_object_ref (simple);

  return simple;
}

/**
 * gabble_simple_async_countdown_inc:
 * @simple: a result created by gabble_simple_async_countdown_new()
 *
 * Increment the counter in @simple, indicating that an additional async
 * operation has been started. An additional call to
 * gabble_simple_async_countdown_dec() will be needed to make @simple
 * call its callback.
 */
void
gabble_simple_async_countdown_inc (GSimpleAsyncResult *simple)
{
  gssize todo = g_simple_async_result_get_op_res_gssize (simple);

  g_return_if_fail (todo >= 1);
  g_simple_async_result_set_op_res_gssize (simple, todo + 1);
}

/**
 * gabble_simple_async_countdown_dec:
 * @simple: a result created by gabble_simple_async_countdown_new()
 *
 * Decrement the counter in @simple. If the number of things to do has
 * reached zero, schedule @simple to call its callback in an idle, then
 * unref it.
 *
 * When one of the asynchronous operations needed for @simple succeeds,
 * this should be signalled by a call to this function.
 *
 * When one of the asynchronous operations needed for @simple fails,
 * this should be signalled by a call to g_simple_async_result_set_from_error()
 * (or one of the similar functions), followed by a call to this function.
 * If more than one async operation fails in this way, the #GError from the
 * last failure will be used.
 */
void
gabble_simple_async_countdown_dec (GSimpleAsyncResult *simple)
{
  gssize todo = g_simple_async_result_get_op_res_gssize (simple);

  g_simple_async_result_set_op_res_gssize (simple, --todo);

  if (todo <= 0)
    {
      g_simple_async_result_complete_in_idle (simple);
      g_object_unref (simple);
    }
}
