"""
Test text channel initiated by me, using Requests.EnsureChannel
"""

import dbus

from gabbletest import exec_test
from servicetest import call_async, EventPattern, assertContains
import constants as cs

def test(q, bus, conn, stream):
    self_handle = conn.Properties.Get(cs.CONN, "SelfHandle")

    jids = ['foo@bar.com', 'truc@cafe.fr']
    handles = conn.get_contact_handles_sync(jids)

    properties = conn.GetAll(
        cs.CONN_IFACE_REQUESTS, dbus_interface=cs.PROPERTIES_IFACE)
    assert properties.get('Channels') == [], properties['Channels']

    properties = conn.GetAll(
        cs.CONN, dbus_interface=cs.PROPERTIES_IFACE)
    assert ({cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_TEXT,
             cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
             },
             [cs.TARGET_HANDLE, cs.TARGET_ID],
             ) in properties.get('RequestableChannelClasses'),\
                     properties['RequestableChannelClasses']

    test_ensure_ensure(q, conn, self_handle, jids[0], handles[0])
    test_request_ensure(q, conn, self_handle, jids[1], handles[1])

def test_ensure_ensure(q, conn, self_handle, jid, handle):
    """
    Test ensuring a non-existant channel twice.  The first call should succeed
    with Yours=True; the subsequent call should succeed with Yours=False
    """

    # Check that Ensuring a channel that doesn't exist succeeds
    call_async(q, conn.Requests, 'EnsureChannel', request_props (handle))

    ret, new_sig = q.expect_many(
        EventPattern('dbus-return', method='EnsureChannel'),
        EventPattern('dbus-signal', signal='NewChannel'),
        )

    assert len(ret.value) == 3
    yours, path, emitted_props = ret.value

    # The channel was created in response to the call, and we were the only
    # requestor, so we should get Yours=True
    assert yours, ret.value

    check_props(emitted_props, self_handle, handle, jid)

    assert new_sig.args[0] == path
    assert new_sig.args[1] == emitted_props

    properties = conn.GetAll(
        cs.CONN_IFACE_REQUESTS, dbus_interface=dbus.PROPERTIES_IFACE)

    assertContains((new_sig.args[0], new_sig.args[1]), properties['Channels'])

    # Now try Ensuring a channel which already exists
    call_async(q, conn.Requests, 'EnsureChannel', request_props(handle))
    ret_ = q.expect('dbus-return', method='EnsureChannel')

    assert len(ret_.value) == 3
    yours_, path_, emitted_props_ = ret_.value

    # Someone's already responsible for this channel, so we should get
    # Yours=False
    assert not yours_, ret_.value
    assert path == path_, (path, path_)
    assert emitted_props == emitted_props_, (emitted_props, emitted_props_)


def test_request_ensure(q, conn, self_handle, jid, handle):
    """
    Test Creating a non-existant channel, then Ensuring the same channel.
    The call to Ensure should succeed with Yours=False.
    """

    call_async(q, conn.Requests, 'CreateChannel', request_props(handle))

    ret, new_sig = q.expect_many(
        EventPattern('dbus-return', method='CreateChannel'),
        EventPattern('dbus-signal', signal='NewChannel'),
        )

    assert len(ret.value) == 2
    path, emitted_props = ret.value

    check_props(emitted_props, self_handle, handle, jid)

    assert new_sig.args[0] == path
    assert new_sig.args[1] == emitted_props

    properties = conn.GetAll(
        cs.CONN_IFACE_REQUESTS, dbus_interface=dbus.PROPERTIES_IFACE)

    assertContains((new_sig.args[0], new_sig.args[1]), properties['Channels'])

    # Now try Ensuring that same channel.
    call_async(q, conn.Requests, 'EnsureChannel', request_props(handle))
    ret_ = q.expect('dbus-return', method='EnsureChannel')

    assert len(ret_.value) == 3
    yours_, path_, emitted_props_ = ret_.value

    # Someone's already responsible for this channel, so we should get
    # Yours=False
    assert not yours_, ret_.value
    assert path == path_, (path, path_)
    assert emitted_props == emitted_props_, (emitted_props, emitted_props_)


def check_props(props, self_handle, handle, jid):
    assert props[cs.CHANNEL_TYPE] == cs.CHANNEL_TYPE_TEXT
    assert props[cs.TARGET_HANDLE_TYPE] == cs.HT_CONTACT
    assert props[cs.TARGET_HANDLE] == handle
    assert props[cs.TARGET_ID] == jid
    assert props[cs.REQUESTED] == True
    assert props[cs.INITIATOR_HANDLE] == self_handle
    assert props[cs.INITIATOR_ID] == 'test@localhost'


def request_props(handle):
    return { cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_TEXT,
             cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
             cs.TARGET_HANDLE: handle,
           }


if __name__ == '__main__':
    exec_test(test)

