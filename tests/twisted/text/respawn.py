"""
Test text channel being recreated because there are still pending messages.
"""

import dbus

from twisted.words.xish import domish

from gabbletest import exec_test
from servicetest import call_async, EventPattern, assertEquals, wrap_channel
import constants as cs

def test(q, bus, conn, stream):
    self_handle = conn.Properties.Get(cs.CONN, "SelfHandle")

    jid = 'foo@bar.com'
    foo_handle = conn.get_contact_handle_sync(jid)

    call_async(q, conn.Requests, 'CreateChannel', {
        cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_TEXT,
        cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
        cs.TARGET_HANDLE: foo_handle })

    ret, new_sig = q.expect_many(
        EventPattern('dbus-return', method='CreateChannel'),
        EventPattern('dbus-signal', signal='NewChannels'),
        )

    text_chan = wrap_channel(bus.get_object(conn.bus_name, ret.value[0]), 'Text')
    chan_iface = dbus.Interface(text_chan, cs.CHANNEL)

    assert len(new_sig.args) == 1
    assert len(new_sig.args[0]) == 1        # one channel
    assert len(new_sig.args[0][0]) == 2     # two struct members
    assert new_sig.args[0][0][0] == ret.value[0]
    emitted_props = new_sig.args[0][0][1]
    assert emitted_props[cs.CHANNEL_TYPE] == cs.CHANNEL_TYPE_TEXT
    assert emitted_props[cs.TARGET_HANDLE_TYPE] == cs.HT_CONTACT
    assert emitted_props[cs.TARGET_HANDLE] == foo_handle
    assert emitted_props[cs.TARGET_ID] == jid
    assert emitted_props[cs.REQUESTED] == True
    assert emitted_props[cs.INITIATOR_HANDLE] == self_handle
    assert emitted_props[cs.INITIATOR_ID] == 'test@localhost'

    channel_props = text_chan.GetAll(
        cs.CHANNEL, dbus_interface=dbus.PROPERTIES_IFACE)
    assert channel_props['TargetID'] == jid,\
            (channel_props['TargetID'], jid)
    assert channel_props['Requested'] == True
    assert channel_props['InitiatorHandle'] == self_handle,\
            (channel_props['InitiatorHandle'], self_handle)
    assert channel_props['InitiatorID'] == 'test@localhost',\
            channel_props['InitiatorID']

    text_chan.send_msg_sync('hey')

    event = q.expect('stream-message')

    elem = event.stanza
    assert elem.name == 'message'
    assert elem['type'] == 'chat'
    body = list(event.stanza.elements())[0]
    assert body.name == 'body'
    assert body.children[0] == u'hey'

    # <message type="chat"><body>hello</body</message>
    m = domish.Element((None, 'message'))
    m['from'] = 'foo@bar.com/Pidgin'
    m['type'] = 'chat'
    m.addElement('body', content='hello')
    stream.send(m)

    event = q.expect('dbus-signal', signal='MessageReceived')

    msg = event.args[0]
    assertEquals(foo_handle, msg[0]['message-sender'])
    assertEquals('hello', msg[1]['content'])

    messages = text_chan.Properties.Get(cs.CHANNEL_IFACE_MESSAGES, 'PendingMessages')
    assertEquals([msg], messages)

    # close the channel without acking the message; it comes back

    call_async(q, chan_iface, 'Close')

    old, new = q.expect_many(
            EventPattern('dbus-signal', signal='Closed'),
            EventPattern('dbus-signal', signal='ChannelClosed'),
            )
    assert old.path == text_chan.object_path,\
            (old.path, text_chan.object_path)
    assert new.args[0] == text_chan.object_path,\
            (new.args[0], text_chan.object_path)

    event = q.expect('dbus-signal', signal='NewChannels')
    path, props = event.args[0][0]
    assertEquals(text_chan.object_path, path)
    assertEquals(cs.CHANNEL_TYPE_TEXT, props[cs.CHANNEL_TYPE])
    assertEquals(cs.HT_CONTACT, props[cs.TARGET_HANDLE_TYPE])
    assertEquals(foo_handle, props[cs.TARGET_HANDLE])

    event = q.expect('dbus-return', method='Close')

    # it now behaves as if the message had initiated it

    channel_props = text_chan.GetAll(
        cs.CHANNEL, dbus_interface=dbus.PROPERTIES_IFACE)
    assert channel_props['TargetID'] == jid,\
            (channel_props['TargetID'], jid)
    assert channel_props['Requested'] == False
    assert channel_props['InitiatorHandle'] == foo_handle,\
            (channel_props['InitiatorHandle'], foo_handle)
    assert channel_props['InitiatorID'] == 'foo@bar.com',\
            channel_props['InitiatorID']

    # the message is still there

    messages = text_chan.Properties.Get(cs.CHANNEL_IFACE_MESSAGES, 'PendingMessages')
    msg[0]['rescued'] = True
    assertEquals([msg], messages)

    # acknowledge it

    text_chan.Text.AcknowledgePendingMessages([msg[0]['pending-message-id']])

    messages = text_chan.Properties.Get(cs.CHANNEL_IFACE_MESSAGES, 'PendingMessages')
    assertEquals([], messages)

    # close the channel again

    call_async(q, chan_iface, 'Close')

    event = q.expect('dbus-signal', signal='Closed')
    assert event.path == text_chan.object_path,\
            (event.path, text_chan.object_path)

    event = q.expect('dbus-return', method='Close')

    # assert that it stays dead this time!

    try:
        chan_iface.GetChannelType()
    except dbus.DBusException:
        pass
    else:
        raise AssertionError("Why won't it die?")

if __name__ == '__main__':
    exec_test(test)
