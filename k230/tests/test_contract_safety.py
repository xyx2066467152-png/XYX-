"""Independent protocol-direction and terminal transport failure regressions."""

import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

from config import CONFIG
from events import AUTH_SUCCESS, ERROR, make_event
from module_runtime import IdentityModule, STATE_ERROR
import uart_protocol as p


class Wire:
    def __init__(self):
        self.incoming = bytearray()
        self.outgoing = bytearray()
        self.fail = False

    def read(self, count):
        result = bytes(self.incoming[:count])
        del self.incoming[:count]
        return result or None

    def write(self, data):
        if self.fail:
            raise OSError("injected failure")
        self.outgoing.extend(data)
        return len(data)


class ContractSafetyTests(unittest.TestCase):
    def test_peer_error_never_causes_error_reply_loop(self):
        uart = Wire()
        module = IdentityModule(uart, dict(CONFIG))
        peer_error = p.encode_frame(p.CMD_ERROR, p.pack_message(
            p.CMD_ERROR, error_code=p.ERROR_PROTOCOL, detail=p.CMD_ERROR), seq=7)
        for now in (0, 500, 1000, 1500):
            uart.incoming.extend(peer_error)
            module.poll(now)
        self.assertEqual(uart.outgoing, b"")
        self.assertEqual(module.last_error, 0)

    def test_module_origin_messages_are_ignored_even_with_malformed_payload(self):
        module_commands = (p.CMD_MODULE_READY, p.CMD_AUTH_SUCCESS, p.CMD_UNKNOWN,
                           p.CMD_NO_FACE, p.CMD_ERROR, p.CMD_PONG, p.CMD_STATUS)
        for index, cmd in enumerate(module_commands):
            with self.subTest(cmd=cmd):
                uart = Wire()
                module = IdentityModule(uart, dict(CONFIG))
                uart.incoming.extend(p.encode_frame(cmd, b"malformed", seq=index))
                module.poll(0)
                self.assertEqual(uart.outgoing, b"")
                self.assertEqual(module.last_error, 0)

    def test_failed_success_write_is_explicit_error_and_not_repeated_without_departure(self):
        # No acknowledgement is promised. A locally failed write must never look
        # like a successful transport or silently re-arm an occupied presence.
        uart = Wire()
        module = IdentityModule(uart, dict(CONFIG))
        event = make_event(AUTH_SUCCESS, user_id=1, similarity=0.90)
        module.observe(event, 0)
        module.observe(event, 75)
        uart.fail = True
        with self.assertRaises(OSError):
            module.observe(event, 150)
        module.record_link_failure(p.ERROR_UART)
        self.assertEqual(module.state, STATE_ERROR)
        self.assertEqual(module.last_event, 4)
        self.assertEqual(module.last_error, p.ERROR_UART)
        self.assertEqual(module.last_user_id, 0)
        self.assertEqual(uart.outgoing, b"")
        self.assertEqual(module.filter.update(event, 250), [])

    def test_error_reporting_does_not_rearm_confirmed_identity(self):
        uart = Wire()
        module = IdentityModule(uart, dict(CONFIG))
        event = make_event(AUTH_SUCCESS, user_id=1, similarity=0.90)
        for now in (0, 75, 150):
            module.observe(event, now)
        module.observe(make_event(ERROR, error_code=p.ERROR_AI), 200)
        for now in (300, 450, 600, 2200, 2400):
            module.observe(event, now)
        frames = p.FrameParser().feed(uart.outgoing, 0)
        self.assertEqual([f["cmd"] for f in frames], [p.CMD_AUTH_SUCCESS, p.CMD_ERROR])


if __name__ == "__main__":
    unittest.main()
