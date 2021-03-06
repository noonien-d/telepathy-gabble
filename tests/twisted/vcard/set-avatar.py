"""
Tests the very simple case of "setting your own avatar".
"""

from twisted.words.xish import xpath
from servicetest import call_async, assertEquals
from gabbletest import (
    exec_test, expect_and_handle_get_vcard, expect_and_handle_set_vcard,
    )
import base64
import functools

def test(q, bus, conn, stream, image_data, mime_type):
    call_async(q, conn.Avatars, 'SetAvatar', image_data, mime_type)

    expect_and_handle_get_vcard(q, stream)

    def check(vcard):
        assertEquals(mime_type,
            xpath.queryForString('/vCard/PHOTO/TYPE', vcard))

        binval = xpath.queryForString('/vCard/PHOTO/BINVAL', vcard)

        # <http://xmpp.org/extensions/xep-0153.html#bizrules-image> says:
        #
        #  5. The image data MUST conform to the base64Binary datatype and thus
        #     be encoded in accordance with Section 6.8 of RFC 2045, which
        #     recommends that base64 data should have lines limited to at most
        #     76 characters in length.
        lines = binval.split('\n')
        for line in lines:
            assert len(line) <= 76, line

        assertEquals(image_data, base64.decodestring(binval))

    expect_and_handle_set_vcard(q, stream, check=check)

    q.expect('dbus-return', method='SetAvatar')

def test_little_avatar(q, bus, conn, stream):
    test(q, bus, conn, stream, image_data='Guy.brush',
        mime_type='image/x-mighty-pirate')

def test_massive_avatar(q, bus, conn, stream):
    """Regression test for
    <https://bugs.freedesktop.org/show_bug.cgi?id=57080>, where a too-small
    buffer was allocated if the base64-encoded avatar spanned multiple lines.

    """
    avatar = '\x89PNG\r\n\x1a\n\x00\x00\x00\rIHDR\x00\x00\x00\xec\x00\x00\x00\x94\x04\x03\x00\x00\x00b\x8c\xd8\x8b\x00\x00\x00\x18PLTEap\x00\xff\x99\x99\xff\xff\xff\xff3\x99\xff\xcc\x99\x99\x99\x99\xff\x99\xff\x00\x00\x00\xcdt\xca\x11\x00\x00\x00\x01tRNS\x00@\xe6\xd8f\x00\x00\x00\x01bKGD\x00\x88\x05\x1dH\x00\x00\x00\tpHYs\x00\x00\x0b\x13\x00\x00\x0b\x13\x01\x00\x9a\x9c\x18\x00\x00\x00\x07tIME\x07\xdc\x0b\x0e\x0f"%\xd9\xcd\xe5\x8c\x00\x00\x01\x88IDATx\xda\xed\xdb=n\xc20\x14\xc0\xf1l\xcc\x0c\\ K\x0f\x10\t.P\x95\xb9\x95\x10\x07\xa0jW\x96\xe0\xeb\x97\x90 ?\xdb\x0f;\x1f.i\xa3\xff\x9bBb\xfb\x17$\xec\xbc\xd8\xa6(ba\xa6E12`aa\xc7\xb2\xd7z\xaf\xd3\xe2\xda\xc2\x19\x16vN\xd68\xe6\xdb\xd7\xb0\xf8\xb4U\xf7\x83\xfa\x11,,l\x1eVm\xf8TU\xd5\xd6\x1eo\xc3\x12\xb0\xb0\xb03\xb1\n2$`aa\xe7c\xe3\xf1\xdd\xbeU^\xc2[\x14\xec\xaa-t\x86\x85\xfd\x93lC\xd6\x87[\x1c-\xedu \xe3\x16\x82\x85\x85\x9d\xca\xea\xb3\x93QVd\xa8\x82\xedZ\xbd\x84\xec\xc1)\x00\x0b\x0b\x9b\x83\x15\xa7\xee\xf2\x88\xe1\xc2k\xc5\x1d&`a\x9f\xc0\x1a\xf1#\x175\\\xb6ID\xfb\xb0\xf1\x80\x85\x85\xcd\xc1Z\xb9\xf6\xe4\xd8p\xd1\xdc\xc4n )\xbf\x19,\xecrX\x99@\x8aK\x1d\xfbp\xbe\xd1\xeb\xb3\x1feY\xbe\xa87R\xb6W`aa\x97\xc1\xda\xb9W\xd3g\x94\xd2_2\xfd|\xbc\xfb\xa8d\x12\xb0\xb0\xbf\xc7\xfa}Ie\xc5\n\xc4\x9d}_\xdfb\x93z\xce\xae\xbb\x80\x85\x85\xcd\xcf\x86\x9b[\xf4Er\x85=\xba\xd9\xb68\xacaaa\x9f\xc8F7\x93\xea\x8fz%\x06\xadh\xc2\xc2\xe6`\xdd\x9c5\x1d\xfb\x9e\x136\x89\xcd-\xb0\xb0\xb0cYwM\xdb\xe4\x8c\xc4n4X\xd8e\xb1\xb6\xf4\xca}`\x86\x9dO)\xe0e\x8e\xe9\xed\x9c\xb0\xb0\xb0\x13\xd8\xb0\x87\x8b\xb9\xd7\xc7\xd5\x95B\xfd\xffP\x0e\x0b\xfb?\xd9\x1f\xd9\xb2to\xf9Q\xd6\xee\x00\x00\x00\x00IEND\xaeB`\x82'
    test(q, bus, conn, stream, image_data=avatar, mime_type='image/png')

if __name__ == '__main__':
    exec_test(test_little_avatar)
    exec_test(test_massive_avatar)
