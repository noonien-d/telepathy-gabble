"""
Regression tests for rescinding outstanding subscription requests.
"""

from twisted.words.protocols.jabber.client import IQ

from servicetest import EventPattern, assertEquals, call_async
from gabbletest import exec_test, acknowledge_iq
import constants as cs
import ns

jid = 'marco@barisione.lit'

def test(q, bus, conn, stream, remove, local):
    # Gabble asks for the roster; the server sends back an empty roster.
    event = q.expect('stream-iq', query_ns=ns.ROSTER)
    event.stanza['type'] = 'result'
    stream.send(event.stanza)

    q.expect('dbus-signal', signal='ContactListStateChanged', args=[cs.CONTACT_LIST_STATE_SUCCESS])

    self_handle = conn.Properties.Get(cs.CONN, "SelfHandle")
    h = conn.get_contact_handle_sync(jid)

    # Another client logged into our account (Gajim, say) wants to subscribe to
    # Marco's presence. First, per RFC 3921 it 'SHOULD perform a "roster set"
    # for the new roster item':
    #
    #   <iq type='set'>
    #     <query xmlns='jabber:iq:roster'>
    #       <item jid='marco@barisione.lit'/>
    #     </query>
    #   </iq>
    #
    # 'As a result, the user's server (1) MUST initiate a roster push for the
    # new roster item to all available resources associated with this user that
    # have requested the roster, setting the 'subscription' attribute to a
    # value of "none"':
    iq = IQ(stream, "set")
    item = iq.addElement((ns.ROSTER, 'query')).addElement('item')
    item['jid'] = jid
    item['subscription'] = 'none'
    stream.send(iq)

    # In response, Gabble should add Marco to stored:
    q.expect_many(
            EventPattern('dbus-signal', signal='ContactsChangedWithID',
                args=[{ h: (cs.SUBSCRIPTION_STATE_NO,
                        cs.SUBSCRIPTION_STATE_NO, ''), },
                        { h :jid }, {}],
                ),
            )

    # Gajim sends a <presence type='subscribe'/> to Marco. 'As a result, the
    # user's server MUST initiate a second roster push to all of the user's
    # available resources that have requested the roster, setting [...]
    # ask='subscribe' attribute in the roster item [for Marco]:
    iq = IQ(stream, "set")
    item = iq.addElement((ns.ROSTER, 'query')).addElement('item')
    item['jid'] = jid
    item['subscription'] = 'none'
    item['ask'] = 'subscribe'
    stream.send(iq)

    # In response, Gabble should add Marco to subscribe:remote-pending:
    q.expect_many(
            EventPattern('dbus-signal', signal='ContactsChangedWithID',
                args=[{ h: (cs.SUBSCRIPTION_STATE_ASK,
                        cs.SUBSCRIPTION_STATE_NO, ''),
                        }, { h:jid }, {}],
                ),
            )

    # The user decides that they don't care what Marco's baking after all
    # (maybe they read his blog instead?) and:
    if remove:
        # ...removes him from the roster...
        if local:
            # ...by telling Gabble to remove him from stored.
            call_async(q, conn.ContactList, 'RemoveContacts', [h])

            event = q.expect('stream-iq', iq_type='set', query_ns=ns.ROSTER)
            item = event.query.firstChildElement()
            assertEquals(jid, item['jid'])
            assertEquals('remove', item['subscription'])
        else:
            # ...using the other client.
            pass

        # The server must 'inform all of the user's available resources that
        # have requested the roster of the roster item removal':
        iq = IQ(stream, "set")
        item = iq.addElement((ns.ROSTER, 'query')).addElement('item')
        item['jid'] = jid
        item['subscription'] = 'remove'
        # When Marco found this bug, this roster update included:
        item['ask'] = 'subscribe'
        # which is a bit weird: I don't think the server should send that when
        # the contact's being removed. I think Gabble should ignore it, so I'm
        # including it in the test.
        stream.send(iq)

        # In response, Gabble should announce that Marco has been removed from
        # subscribe:remote-pending and stored:members:
        q.expect_many(
            EventPattern('dbus-signal', signal='ContactsChangedWithID',
                args=[{}, {}, { h: jid }],
                ),
            )

        if local:
            acknowledge_iq(stream, event.stanza)
            q.expect('dbus-return', method='RemoveContacts')
            # FIXME: when we depend on a new enough tp-glib we can expect
            # RemoveMembers to return here in the local case, too
    else:
        # ...rescinds the subscription request...
        if local:
            # ...by telling Gabble to remove him from 'subscribe'.
            call_async(q, conn.ContactList, 'Unsubscribe', [h])

            events = [EventPattern('stream-presence', to=jid,
                presence_type='unsubscribe')]

            events.append(EventPattern('dbus-return',
                method='Unsubscribe'))

            event = q.expect_many(*events)[0]
        else:
            # ...in the other client.
            pass

        # In response, the server sends a roster update:
        iq = IQ(stream, "set")
        item = iq.addElement((ns.ROSTER, 'query')).addElement('item')
        item['jid'] = jid
        item['subscription'] = 'none'
        # no ask='subscribe' any more.
        stream.send(iq)

        # In response, Gabble should announce that Marco has been removed from
        # subscribe:remote-pending. It shouldn't wait for the <presence
        # type='unsubscribed'/> ack before doing so: empirical tests reveal
        # that it's never delivered.
        q.expect_many(
                EventPattern('dbus-signal', signal='ContactsChangedWithID',
                    args=[{ h:
                        (cs.SUBSCRIPTION_STATE_NO, cs.SUBSCRIPTION_STATE_NO,
                            ''),
                        }, { h: jid}, {}],
                    ),
                )

def test_remove_local(q, bus, conn, stream):
    test(q, bus, conn, stream, remove=True, local=True)

def test_unsubscribe_local(q, bus, conn, stream):
    test(q, bus, conn, stream, remove=False, local=True)

def test_remove_remote(q, bus, conn, stream):
    test(q, bus, conn, stream, remove=True, local=False)

def test_unsubscribe_remote(q, bus, conn, stream):
    test(q, bus, conn, stream, remove=False, local=False)

if __name__ == '__main__':
    exec_test(test_remove_local)
    exec_test(test_unsubscribe_local)
    exec_test(test_remove_remote)
    exec_test(test_unsubscribe_remote)
