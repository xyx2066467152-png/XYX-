"""Mocked UART tests for the lab client; these are not hardware evidence."""

import io
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from config import CONFIG
from hardware_checks.pc_link_check import UartLinkCheck
from module_runtime import IdentityModule, STATE_READY
from uart_protocol import CMD_ERROR, CMD_MODULE_READY, FrameParser, encode_frame, pack_message


class FakeClock:
    def __init__(self):
        self.seconds = 0.0

    def now(self):
        return self.seconds

    def sleep(self, amount):
        self.seconds += amount


class FakeDeviceUART:
    def __init__(self):
        self.rx = bytearray()
        self.tx = bytearray()

    def read(self, amount):
        if not self.rx:
            return None
        data = bytes(self.rx[:amount])
        del self.rx[:amount]
        return data

    def write(self, data):
        self.tx.extend(data)
        return len(data)


class FakeHostUART:
    def __init__(self, clock, with_ready=True, reflect_error=False):
        self.clock = clock
        self.device = FakeDeviceUART()
        self.module = IdentityModule(self.device, dict(CONFIG))
        self.request_parser = FrameParser()
        self.reflect_error = reflect_error
        if with_ready:
            # Deliberately synthetic protocol fixture, not production ready().
            self.module.link.send_message(CMD_MODULE_READY, api_major=1, api_minor=0,
                                          feature_dimension=512, threshold=0.75)
            self.module.state = STATE_READY

    def read(self, amount):
        self.clock.seconds += 0.01
        self.module.poll(int(self.clock.seconds * 1000))
        take = min(amount, 2, len(self.device.tx))
        data = bytes(self.device.tx[:take])
        del self.device.tx[:take]
        return data

    def write(self, data):
        self.device.rx.extend(data)
        self.module.poll(int(self.clock.seconds * 1000))
        if self.reflect_error:
            for frame in self.request_parser.feed(data, int(self.clock.seconds * 1000)):
                if frame["cmd"] == CMD_ERROR:
                    self.device.tx.extend(encode_frame(CMD_ERROR,
                        pack_message(CMD_ERROR, error_code=1, detail=0), seq=frame["seq"]))
        return len(data)


class LabClientSoftwareTests(unittest.TestCase):
    def test_all_transport_probes_pass_over_mocked_uart(self):
        clock = FakeClock()
        host = FakeHostUART(clock)
        capture = io.BytesIO()
        records = []
        checker = UartLinkCheck(host, capture, clock=clock.now, sleep=clock.sleep,
                                emit=records.append)
        result = checker.run()
        self.assertTrue(result["passed"], result["steps"])
        self.assertEqual(len(result["steps"]), 11)
        self.assertFalse(result["ai_camera_test"])
        self.assertFalse(result["stm32_test"])
        self.assertGreater(len(capture.getvalue()), 0)
        self.assertEqual(checker.session.parser.stats["crc_errors"], 0)
        # The host injected one bad frame into the BOARD parser, not its receiver.
        self.assertEqual(host.module.link.parser.stats["crc_errors"], 1)
        split = [step for step in result["steps"] if step["name"] == "split_ping"][0]
        self.assertLessEqual(split["measured_gap_ms"], 40)

    def test_absent_ready_stops_at_bounded_deadline(self):
        clock = FakeClock()
        checker = UartLinkCheck(FakeHostUART(clock, with_ready=False), io.BytesIO(),
            timeout=1.0, clock=clock.now, sleep=clock.sleep, emit=lambda record: None)
        result = checker.run()
        self.assertFalse(result["passed"])
        self.assertEqual(len(result["steps"]), 1)
        self.assertLessEqual(clock.seconds, 1.02)

    def test_reflected_peer_error_fails_anti_echo_probe(self):
        clock = FakeClock()
        checker = UartLinkCheck(FakeHostUART(clock, reflect_error=True), io.BytesIO(),
            clock=clock.now, sleep=clock.sleep, emit=lambda record: None)
        result = checker.run()
        self.assertFalse(result["passed"])
        probe = [step for step in result["steps"] if step["name"] == "peer_error_is_not_echoed"][0]
        self.assertFalse(probe["passed"])
        self.assertGreater(probe["reflected_count"], 0)


if __name__ == "__main__":
    unittest.main()
