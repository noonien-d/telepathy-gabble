
#include "config.h"

#include <string.h>

#include <glib-object.h>

#include "src/util.h"
#include "src/message-util.h"

/* Test the most basic <message> possible. */
static void
test1 (void)
{
  LmMessage *msg;
  gboolean ret;
  const gchar *from;
  time_t stamp;
  TpChannelTextMessageType type;
  TpChannelTextSendError send_error;
  TpDeliveryStatus delivery_status;
  const gchar *id;
  const gchar *body;
  gint state;

  msg = wocky_stanza_build (WOCKY_STANZA_TYPE_MESSAGE, WOCKY_STANZA_SUB_TYPE_NONE,
        "foo@bar.com", NULL,
        '@', "id", "a867c060-bd3f-4ecc-a38f-3e306af48e4c",
        NULL);
  ret = gabble_message_util_parse_incoming_message (
      msg, &from, &stamp, &type, &id, &body, &state, &send_error,
      &delivery_status);
  g_assert (ret == TRUE);
  g_assert (0 == strcmp (id, "a867c060-bd3f-4ecc-a38f-3e306af48e4c"));
  g_assert (0 == strcmp (from, "foo@bar.com"));
  g_assert (stamp == 0);
  g_assert (type == TP_CHANNEL_TEXT_MESSAGE_TYPE_NOTICE);
  g_assert (body == NULL);
  g_assert (state == -1);
  g_assert (send_error == GABBLE_TEXT_CHANNEL_SEND_NO_ERROR);
  g_object_unref (msg);
}

/* A <message> with a simple body. Parsed as a NOTICE because it doesn't have
 * a 'type' attribute.
 */
static void
test2 (void)
{
  LmMessage *msg;
  gboolean ret;
  const gchar *from;
  time_t stamp;
  TpChannelTextMessageType type;
  TpChannelTextSendError send_error;
  TpDeliveryStatus delivery_status;
  const gchar *id;
  const gchar *body;
  gint state;

  msg = wocky_stanza_build (WOCKY_STANZA_TYPE_MESSAGE, WOCKY_STANZA_SUB_TYPE_NONE,
        "foo@bar.com", NULL,
        '@', "id", "a867c060-bd3f-4ecc-a38f-3e306af48e4c",
        '(', "body", '$', "hello", ')',
        NULL);
  ret = gabble_message_util_parse_incoming_message (
      msg, &from, &stamp, &type, &id, &body, &state, &send_error,
      &delivery_status);
  g_assert (ret == TRUE);
  g_assert (0 == strcmp (id, "a867c060-bd3f-4ecc-a38f-3e306af48e4c"));
  g_assert (0 == strcmp (from, "foo@bar.com"));
  g_assert (stamp == 0);
  g_assert (type == TP_CHANNEL_TEXT_MESSAGE_TYPE_NOTICE);
  g_assert (0 == strcmp (body, "hello"));
  g_assert (state == -1);
  g_assert (send_error == GABBLE_TEXT_CHANNEL_SEND_NO_ERROR);
  g_object_unref (msg);
}

/* Simple type="chat" message. */
static void
test3 (void)
{
  LmMessage *msg;
  gboolean ret;
  const gchar *from;
  time_t stamp;
  TpChannelTextMessageType type;
  TpChannelTextSendError send_error;
  TpDeliveryStatus delivery_status;
  const gchar *id;
  const gchar *body;
  gint state;

  msg = wocky_stanza_build (WOCKY_STANZA_TYPE_MESSAGE, WOCKY_STANZA_SUB_TYPE_CHAT,
        "foo@bar.com", NULL,
        '@', "id", "a867c060-bd3f-4ecc-a38f-3e306af48e4c",
        '(', "body", '$', "hello", ')',
        NULL);
  ret = gabble_message_util_parse_incoming_message (
      msg, &from, &stamp, &type, &id, &body, &state, &send_error,
      &delivery_status);
  g_assert (ret == TRUE);
  g_assert (0 == strcmp (id, "a867c060-bd3f-4ecc-a38f-3e306af48e4c"));
  g_assert (0 == strcmp (from, "foo@bar.com"));
  g_assert (stamp == 0);
  g_assert (type == TP_CHANNEL_TEXT_MESSAGE_TYPE_NORMAL);
  g_assert (0 == strcmp (body, "hello"));
  g_assert (state == -1);
  g_assert (send_error == GABBLE_TEXT_CHANNEL_SEND_NO_ERROR);
  g_object_unref (msg);
}

/* A simple error. */
static void
test_error (void)
{
  LmMessage *msg;
  gboolean ret;
  const gchar *from;
  time_t stamp;
  TpChannelTextMessageType type;
  TpChannelTextSendError send_error;
  TpDeliveryStatus delivery_status;
  const gchar *id;
  const gchar *body;
  gint state;

  msg = wocky_stanza_build (
      WOCKY_STANZA_TYPE_MESSAGE, WOCKY_STANZA_SUB_TYPE_ERROR,
      "foo@bar.com", NULL,
      '@', "id", "a867c060-bd3f-4ecc-a38f-3e306af48e4c",
      '(', "error", '$', "oops", ')',
      NULL);
  ret = gabble_message_util_parse_incoming_message (
      msg, &from, &stamp, &type, &id, &body, &state, &send_error,
      &delivery_status);
  g_assert (ret == TRUE);
  g_assert (0 == strcmp (id, "a867c060-bd3f-4ecc-a38f-3e306af48e4c"));
  g_assert (0 == strcmp (from, "foo@bar.com"));
  g_assert (stamp == 0);
  g_assert (type == TP_CHANNEL_TEXT_MESSAGE_TYPE_NOTICE);
  g_assert (body == NULL);
  g_assert (state == -1);
  g_assert (send_error == TP_CHANNEL_TEXT_SEND_ERROR_UNKNOWN);
  g_assert (delivery_status == TP_DELIVERY_STATUS_PERMANENTLY_FAILED);
  g_object_unref (msg);
}

/* A more complicated error, described in XEP-0086 as a "simple error response".
 */
static void
test_another_error (void)
{
  LmMessage *msg;
  gboolean ret;
  const gchar *from;
  time_t stamp;
  TpChannelTextMessageType type;
  TpChannelTextSendError send_error;
  TpDeliveryStatus delivery_status;
  const gchar *id;
  const gchar *body;
  gint state;
  const gchar *message = "Wherefore art thou, Romeo?";

  msg = wocky_stanza_build (
      WOCKY_STANZA_TYPE_MESSAGE, WOCKY_STANZA_SUB_TYPE_ERROR,
      "romeo@montague.net/garden", "juliet@capulet.com/balcony",
      '@', "id", "a867c060-bd3f-4ecc-a38f-3e306af48e4c",
      '(', "body", '$', message, ')',
      '(', "error",
        '@', "code", "404",
        '@', "type", "cancel",
        '(', "item-not-found",
          ':', "urn:ietf:params:xml:ns:xmpp-stanzas",
        ')',
      ')',
      NULL);
  ret = gabble_message_util_parse_incoming_message (
      msg, &from, &stamp, &type, &id, &body, &state, &send_error,
      &delivery_status);
  g_assert (ret == TRUE);
  g_assert (0 == strcmp (id, "a867c060-bd3f-4ecc-a38f-3e306af48e4c"));
  g_assert (0 == strcmp (from, "romeo@montague.net/garden"));
  g_assert (stamp == 0);
  g_assert (type == TP_CHANNEL_TEXT_MESSAGE_TYPE_NOTICE);
  g_assert (!tp_strdiff (body, message));
  g_assert (state == -1);
  g_assert (send_error == TP_CHANNEL_TEXT_SEND_ERROR_INVALID_CONTACT);
  g_assert (delivery_status == TP_DELIVERY_STATUS_PERMANENTLY_FAILED);
  g_object_unref (msg);
}

/* One million, seven hundred seventy-one thousand, five hundred sixty-one
 * errors.
 */
static void
test_yet_another_error (void)
{
  LmMessage *msg;
  gboolean ret;
  const gchar *from;
  time_t stamp;
  TpChannelTextMessageType type;
  TpChannelTextSendError send_error;
  TpDeliveryStatus delivery_status;
  const gchar *id;
  const gchar *body;
  gint state;
  const gchar *message = "Its trilling seems to have a tranquilizing effect on "
                         "the human nervous system.";

  msg = wocky_stanza_build (
      WOCKY_STANZA_TYPE_MESSAGE, WOCKY_STANZA_SUB_TYPE_ERROR,
      "other@starfleet.us/Enterprise",
      "spock@starfleet.us/Enterprise",
      '@', "id", "a867c060-bd3f-4ecc-a38f-3e306af48e4c",
      '(', "body", '$', message, ')',
      '(', "error",
        '@', "code", "404",
        '@', "type", "wait",
        '(', "recipient-unavailable",
          ':', "urn:ietf:params:xml:ns:xmpp-stanzas",
        ')',
      ')',
      NULL);
  ret = gabble_message_util_parse_incoming_message (
      msg, &from, &stamp, &type, &id, &body, &state, &send_error,
      &delivery_status);
  g_assert (ret == TRUE);
  g_assert (0 == strcmp (id, "a867c060-bd3f-4ecc-a38f-3e306af48e4c"));
  g_assert (0 == strcmp (from, "other@starfleet.us/Enterprise"));
  g_assert (stamp == 0);
  g_assert (type == TP_CHANNEL_TEXT_MESSAGE_TYPE_NOTICE);
  g_assert (!tp_strdiff (body, message));
  g_assert (state == -1);
  g_assert (send_error == TP_CHANNEL_TEXT_SEND_ERROR_OFFLINE);
  g_assert (delivery_status == TP_DELIVERY_STATUS_TEMPORARILY_FAILED);
  g_object_unref (msg);
}

static void
test_google_offline (void)
{
  LmMessage *msg;
  gboolean ret;
  const gchar *from;
  time_t stamp;
  TpChannelTextMessageType type;
  TpChannelTextSendError send_error;
  TpDeliveryStatus delivery_status;
  const gchar *id;
  const gchar *body;
  gint state;

  msg = wocky_stanza_build (WOCKY_STANZA_TYPE_MESSAGE, WOCKY_STANZA_SUB_TYPE_NONE,
      "foo@bar.com", NULL,
      '@', "id", "a867c060-bd3f-4ecc-a38f-3e306af48e4c",
      '(', "body", '$', "hello", ')',
      '(', "x",
         ':', "jabber:x:delay",
         '@', "stamp", "20070927T13:24:14",
      ')',
      '(', "time",
         ':', "google:timestamp",
         '@', "ms", "1190899454656",
      ')',
      NULL);
  ret = gabble_message_util_parse_incoming_message (
      msg, &from, &stamp, &type, &id, &body, &state, &send_error,
      &delivery_status);
  g_assert (ret == TRUE);
  g_assert (0 == strcmp (id, "a867c060-bd3f-4ecc-a38f-3e306af48e4c"));
  g_assert (0 == strcmp (from, "foo@bar.com"));
  g_assert (stamp == 1190899454);
  g_assert (type == TP_CHANNEL_TEXT_MESSAGE_TYPE_NORMAL);
  g_assert (0 == strcmp (body, "hello"));
  g_assert (state == -1);
  g_assert (send_error == GABBLE_TEXT_CHANNEL_SEND_NO_ERROR);
  g_object_unref (msg);
}

int
main (
    int argc,
    char *argv[])
{
  g_type_init ();
  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/parse-message/1", test1);
  g_test_add_func ("/parse-message/2", test2);
  g_test_add_func ("/parse-message/3", test3);
  g_test_add_func ("/parse-message/error", test_error);
  g_test_add_func ("/parse-message/another-error", test_another_error);
  g_test_add_func ("/parse-message/yet-another-error", test_yet_another_error);
  g_test_add_func ("/parse-message/google-offline", test_google_offline);
  return g_test_run ();
}

