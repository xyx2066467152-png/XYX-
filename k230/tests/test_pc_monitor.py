"""Software tests only: real UART wiring and real STM32 are not involved."""

import builtins
import contextlib
import io
import itertools
import json
from pathlib import Path
import sys
import tempfile
import types
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import pc_uart_monitor as monitor
from uart_protocol import (
    CMD_AUTH_SUCCESS, CMD_GET_STATUS, CMD_NO_FACE, CMD_PING, CMD_PONG,
    CMD_STATUS, FrameParser, encode_frame, pack_message,
)


def success(seq=4):
    return encode_frame(CMD_AUTH_SUCCESS,
                        pack_message(CMD_AUTH_SUCCESS, user_id=1, similarity=0.90), seq=seq)


def records_of(records, kind):
    return [record for record in records if record["record"] == kind]


class MonitorSessionTests(unittest.TestCase):
    def setUp(self):
        self.records = []
        self.session = monitor.MonitorSession(self.records.append, mode="software_test")

    def test_fragmented_frame_is_decoded_once(self):
        wire = success()
        for now, value in enumerate(wire):
            self.session.feed(bytes((value,)), now)
        frames = records_of(self.records, "frame")
        self.assertEqual(len(frames), 1)
        self.assertEqual(frames[0]["message"]["user_id"], 1)
        self.assertEqual(frames[0]["message"]["similarity"], 0.90)
        self.assertEqual(frames[0]["mode"], "software_test")

    def test_crc_error_does_not_hide_next_unknown_command(self):
        damaged = bytearray(success())
        damaged[-1] ^= 0x01
        unknown = encode_frame(0x44, b"\xaa\x55", seq=9)
        self.session.feed(bytes(damaged) + unknown + success(seq=10), 0)
        frames = records_of(self.records, "frame")
        self.assertEqual([frame["cmd"] for frame in frames], [0x44, CMD_AUTH_SUCCESS])
        self.assertFalse(frames[0]["known"])
        self.assertEqual(frames[0]["message"]["name"], "UNKNOWN_CMD")
        self.assertEqual(self.session.parser.stats["crc_errors"], 1)
        self.assertTrue(any(record["counter"] == "crc_errors"
                            for record in records_of(self.records, "diagnostic")))

    def test_checksum_valid_bad_payload_is_reported_and_receiving_continues(self):
        malformed = encode_frame(CMD_AUTH_SUCCESS, b"\x01", seq=1)
        self.session.feed(malformed + success(seq=2), 0)
        frames = records_of(self.records, "frame")
        self.assertIn("payload_error", frames[0])
        self.assertIsNone(frames[0]["message"])
        self.assertEqual(frames[1]["message"]["user_id"], 1)
        self.assertEqual(self.session.payload_errors, 1)

    def test_reply_matches_both_sequence_and_command(self):
        request = self.session.build_request(CMD_PING, 20)
        seq = int.from_bytes(request[4:6], "little")
        status = encode_frame(CMD_STATUS, pack_message(CMD_STATUS, state=2,
                              last_event=0, last_user_id=0, last_similarity=0,
                              last_error=0), seq=seq)
        self.session.feed(status, 30)
        self.assertIn(seq, self.session.pending)
        self.session.feed(encode_frame(CMD_PONG, seq=seq), 40)
        frames = records_of(self.records, "frame")
        self.assertEqual(frames[0]["request_match"], "unmatched_response")
        self.assertEqual(frames[1]["request_match"], "matched")
        self.assertEqual(frames[1]["request_cmd"], CMD_PING)
        self.assertEqual(frames[1]["round_trip_ms"], 20)
        self.assertFalse(self.session.pending)

    def test_malformed_reply_does_not_complete_pending_request(self):
        request = self.session.build_request(CMD_PING, 0)
        seq = int.from_bytes(request[4:6], "little")
        self.session.feed(encode_frame(CMD_PONG, b"unexpected", seq=seq), 1)
        self.assertEqual(records_of(self.records, "frame")[0]["request_match"], "invalid_response")
        self.assertIn(seq, self.session.pending)
        self.session.feed(encode_frame(CMD_PONG, seq=seq), 2)
        self.assertFalse(self.session.pending)

    def test_success_event_with_query_sequence_is_not_a_reply(self):
        self.session.build_request(CMD_GET_STATUS, 0)
        self.session.feed(success(seq=0), 1)
        self.assertIn(0, self.session.pending)
        self.assertNotIn("request_match", records_of(self.records, "frame")[0])

    def test_request_timeout_is_emitted_once(self):
        self.session.build_request(CMD_PING, 0)
        self.session.poll(monitor.REQUEST_TIMEOUT_MS - 1)
        self.assertFalse(records_of(self.records, "request_timeout"))
        self.session.poll(monitor.REQUEST_TIMEOUT_MS)
        self.session.poll(monitor.REQUEST_TIMEOUT_MS + 1)
        self.assertEqual(len(records_of(self.records, "request_timeout")), 1)
        self.assertFalse(self.session.pending)

    def test_sequence_wrap_avoids_existing_pending_sequence(self):
        self.session.next_seq = 65535
        self.session.build_request(CMD_PING, 0)
        self.session.build_request(CMD_PING, 0)
        self.session.next_seq = 0
        self.session.build_request(CMD_GET_STATUS, 0)
        self.assertEqual(list(self.session.pending), [65535, 0, 1])

    def test_end_of_stream_reports_truncation_without_fake_request_timeout(self):
        self.session.build_request(CMD_PING, 0)
        self.session.feed(success()[:-2], 1)
        self.session.finish(2)
        self.assertEqual(self.session.parser.stats["partial_timeouts"], 1)
        self.assertFalse(records_of(self.records, "frame"))
        self.assertFalse(records_of(self.records, "request_timeout"))
        cancelled = records_of(self.records, "request_cancelled")
        self.assertEqual(cancelled[0]["reason"], "session_ended")
        self.assertEqual(cancelled[0]["timestamp_ms"], 2)
        diagnostic = records_of(self.records, "diagnostic")[-1]
        self.assertEqual(diagnostic["reason"], "end_of_stream")
        self.assertEqual(diagnostic["timestamp_ms"], 2)

    def test_replay_captures_exact_raw_bytes_and_split_frames(self):
        wire = success() + encode_frame(CMD_NO_FACE, seq=5)
        destination = io.BytesIO()
        summary = monitor.replay_stream(io.BytesIO(wire), self.session,
                                        chunk_size=2, capture=destination)
        self.assertEqual(destination.getvalue(), wire)
        self.assertEqual(summary["rx_frames"], 2)
        self.assertEqual(summary["rx_bytes"], len(wire))
        self.assertEqual(summary["payload_errors"], 0)

    def test_only_query_commands_can_be_sent(self):
        with self.assertRaises(ValueError):
            self.session.build_request(CMD_AUTH_SUCCESS, 0)


class MonitorIOTests(unittest.TestCase):
    def test_short_serial_writes_are_completed(self):
        captured = bytearray()

        class Writer:
            def write(self, data):
                amount = min(2, len(data))
                captured.extend(data[:amount])
                return amount

        wire = success()
        monitor.write_all_uart(Writer(), wire)
        self.assertEqual(bytes(captured), wire)

    def test_stalled_or_invalid_serial_write_fails(self):
        for result in (0, None, -1, 1000000):
            writer = types.SimpleNamespace(write=lambda data, result=result: result)
            with self.subTest(result=result), self.assertRaises(IOError):
                monitor.write_all_uart(writer, b"test")

    def test_unknown_payload_is_json_serializable(self):
        output = io.StringIO()
        session = monitor.MonitorSession(mode="software_test")
        with contextlib.redirect_stdout(output):
            session.feed(encode_frame(0x44, b"\xaa\x55", seq=9), 0)
        records = [json.loads(line) for line in output.getvalue().splitlines()]
        frame = records_of(records, "frame")[0]
        self.assertEqual(frame["message"]["data"], "aa55")
        self.assertEqual(frame["data_hex"], "aa55")

    def test_demo_cli_does_not_import_pyserial(self):
        output = io.StringIO()
        actual_import = builtins.__import__

        def forbid_serial(name, *args, **kwargs):
            if name == "serial" or name.startswith("serial."):
                raise AssertionError("Offline CLI tried to import pyserial")
            return actual_import(name, *args, **kwargs)

        with patch("builtins.__import__", side_effect=forbid_serial), contextlib.redirect_stdout(output):
            result = monitor.main(["--demo", "--chunk-size", "1"])
        self.assertEqual(result, 0)
        records = [json.loads(line) for line in output.getvalue().splitlines()]
        self.assertEqual(records[0]["mode"], "simulated_demo")
        self.assertFalse(records[0]["hardware_test"])
        self.assertEqual(records[-1]["rx_frames"], 5)
        self.assertEqual(records[-1]["parser_stats"]["crc_errors"], 0)

    def test_missing_pyserial_is_clear_and_does_not_crash(self):
        output = io.StringIO()
        actual_import = builtins.__import__

        def missing_serial(name, *args, **kwargs):
            if name == "serial" or name.startswith("serial."):
                raise ImportError("not installed")
            return actual_import(name, *args, **kwargs)

        with patch("builtins.__import__", side_effect=missing_serial), contextlib.redirect_stdout(output):
            result = monitor.main(["--list-ports"])
        self.assertEqual(result, 2)
        error = json.loads(output.getvalue())
        self.assertIn("pyserial", error["message"])
        self.assertIn("python -m pip install", error["message"])

    def test_replay_cli_handles_crc_unknown_and_truncated_input(self):
        damaged = bytearray(success())
        damaged[-1] ^= 0x01
        wire = bytes(damaged) + encode_frame(0x44, b"data", seq=9) + success(seq=10)[:-2]
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "stream.bin"
            source.write_bytes(wire)
            output = io.StringIO()
            with contextlib.redirect_stdout(output):
                result = monitor.main(["--replay", str(source), "--chunk-size", "3"])
        self.assertEqual(result, 0)
        records = [json.loads(line) for line in output.getvalue().splitlines()]
        self.assertFalse(records[0]["hardware_test"])
        summary = records[-1]
        self.assertEqual(summary["rx_frames"], 1)
        self.assertEqual(summary["parser_stats"]["crc_errors"], 1)
        self.assertEqual(summary["parser_stats"]["partial_timeouts"], 1)

    def test_missing_replay_file_returns_json_error(self):
        with tempfile.TemporaryDirectory() as directory:
            output = io.StringIO()
            with contextlib.redirect_stdout(output):
                result = monitor.main(["--replay", str(Path(directory) / "missing.bin")])
        self.assertEqual(result, 2)
        records = [json.loads(line) for line in output.getvalue().splitlines()]
        self.assertEqual(records[-1]["record"], "error")

    def test_capture_never_overwrites_existing_file(self):
        with tempfile.TemporaryDirectory() as directory:
            destination = Path(directory) / "existing.bin"
            destination.write_bytes(b"keep original")
            with contextlib.redirect_stdout(io.StringIO()):
                result = monitor.main(["--demo", "--capture", str(destination)])
            self.assertEqual(result, 2)
            self.assertEqual(destination.read_bytes(), b"keep original")

    def test_offline_query_flags_are_rejected(self):
        with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as caught:
            monitor.main(["--demo", "--ping"])
        self.assertEqual(caught.exception.code, 2)

    def test_simulated_serial_io_queries_and_fragmented_responses(self):
        class FakeUART:
            def __init__(self):
                self.closed = False
                self.input = bytearray(success(seq=19))
                self.request_parser = FrameParser()
                self.written = bytearray()

            def __enter__(self):
                return self

            def __exit__(self, *args):
                self.closed = True

            def read(self, amount):
                take = min(amount, 2, len(self.input))
                data = bytes(self.input[:take])
                del self.input[:take]
                return data

            def write(self, data):
                take = min(3, len(data))
                piece = data[:take]
                self.written.extend(piece)
                for request in self.request_parser.feed(piece, 0):
                    if request["cmd"] == CMD_PING:
                        self.input.extend(encode_frame(CMD_PONG, seq=request["seq"]))
                    elif request["cmd"] == CMD_GET_STATUS:
                        self.input.extend(encode_frame(CMD_STATUS, pack_message(CMD_STATUS,
                            state=2, last_event=CMD_AUTH_SUCCESS, last_user_id=1,
                            last_similarity=0.90, last_error=0), seq=request["seq"]))
                return take

        port = FakeUART()
        fake_serial = types.SimpleNamespace(Serial=lambda **kwargs: port)
        output = io.StringIO()
        with patch.object(monitor, "import_pyserial", return_value=fake_serial), \
                patch.object(monitor.time, "monotonic", side_effect=itertools.count(0, 0.001)), \
                contextlib.redirect_stdout(output):
            result = monitor.main(["--port", "SIMULATED", "--ping", "--get-status",
                                   "--duration", "0.2", "--chunk-size", "2"])
        records = [json.loads(line) for line in output.getvalue().splitlines()]
        self.assertEqual(result, 0)
        self.assertTrue(port.closed)
        matches = [record for record in records_of(records, "frame")
                   if record.get("request_match") == "matched"]
        self.assertEqual([record["seq"] for record in matches], [0, 1])
        self.assertEqual([record["request_cmd"] for record in matches], [CMD_PING, CMD_GET_STATUS])
        self.assertEqual(records[-1]["rx_frames"], 3)
        self.assertEqual(records[-1]["pending_requests"], 0)


if __name__ == "__main__":
    unittest.main()
