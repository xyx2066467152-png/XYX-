"""Protocol tests require CPython standard library only; no hardware involved."""

import pathlib
import binascii
import random
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import uart_protocol as p


def parsed(cmd, data=b"", seq=0):
    return p.FrameParser().feed(p.encode_frame(cmd, data, seq), 0)[0]


class ProtocolTests(unittest.TestCase):
    def test_crc_standard_vector(self):
        self.assertEqual(p.crc16(b"123456789"), 0x29B1)

    def test_crc_matches_independent_cpython_reference(self):
        rng = random.Random(230)
        for length in range(128):
            data = bytes(rng.randrange(256) for _ in range(length))
            self.assertEqual(p.crc16(data), binascii.crc_hqx(data, 0xFFFF))

    def test_published_golden_vectors(self):
        ready = p.pack_message(p.CMD_MODULE_READY, api_major=1, api_minor=0,
                               feature_dimension=512, threshold=0.75)
        success = p.pack_message(p.CMD_AUTH_SUCCESS, user_id=1, similarity=0.9)
        cases = [
            (p.CMD_MODULE_READY, ready, 0, "AA550101000006010000024C1DDB62"),
            (p.CMD_AUTH_SUCCESS, success, 1, "AA550110010004010028237812"),
            (p.CMD_PING, b"", 0x1234, "AA550180341200111A"),
            (p.CMD_PONG, b"", 0x1234, "AA55018234120079F7"),
        ]
        for cmd, data, seq, expected in cases:
            self.assertEqual(p.encode_frame(cmd, data, seq), bytes.fromhex(expected))

    def test_normal_frame_exact_endian(self):
        data = p.pack_message(p.CMD_AUTH_SUCCESS, user_id=0x1234,
                              similarity=0.875)
        self.assertEqual(data, b"\x34\x12\x2e\x22")
        frame = p.encode_frame(p.CMD_AUTH_SUCCESS, data, seq=0x4567)
        self.assertEqual(frame[:7], b"\xaa\x55\x01\x10\x67\x45\x04")
        self.assertEqual(frame[-2:], bytes((p.crc16(frame[2:-2]) & 255,
                                          p.crc16(frame[2:-2]) >> 8)))
        message = p.decode_message(parsed(p.CMD_AUTH_SUCCESS, data, 0x4567))
        self.assertEqual(message["user_id"], 0x1234)
        self.assertEqual(message["seq"], 0x4567)
        self.assertEqual(message["similarity_q"], 8750)

    def test_bad_crc_and_resynchronization(self):
        broken = bytearray(p.encode_frame(p.CMD_PING, seq=3))
        broken[-1] ^= 1
        parser = p.FrameParser()
        frames = parser.feed(broken + p.encode_frame(p.CMD_GET_STATUS, seq=4), 0)
        self.assertEqual([f["seq"] for f in frames], [4])
        self.assertEqual(parser.stats["crc_errors"], 1)

    def test_truncated_frame_timeout(self):
        parser = p.FrameParser()
        frame = p.encode_frame(p.CMD_AUTH_SUCCESS, b"\x01\x00\x40\x1f")
        self.assertEqual(parser.feed(frame[:-1], 0), [])
        self.assertEqual(parser.expire(199), [])
        self.assertEqual(parser.expire(200), [])
        self.assertEqual(parser.buffered_bytes, 0)
        self.assertEqual(parser.stats["partial_timeouts"], 1)
        self.assertEqual(len(parser.feed(p.encode_frame(p.CMD_PING), 201)), 1)

    def test_late_tail_cannot_complete_expired_frame(self):
        parser = p.FrameParser()
        frame = p.encode_frame(p.CMD_AUTH_SUCCESS, b"\x01\x00\x40\x1f")
        self.assertEqual(parser.feed(frame[:8], 0), [])
        self.assertEqual(parser.feed(frame[8:], 200), [])
        self.assertEqual(parser.stats["frames"], 0)
        received = parser.feed(p.encode_frame(p.CMD_GET_STATUS, seq=7), 201)
        self.assertEqual([f["seq"] for f in received], [7])

    def test_timeout_recovers_good_frame_behind_false_long_candidate(self):
        parser = p.FrameParser()
        incomplete = b"\xaa\x55\x01\x90\x00\x00\x20"
        self.assertEqual(parser.feed(incomplete + p.encode_frame(p.CMD_PING), 0), [])
        recovered = parser.expire(200)
        self.assertEqual([f["cmd"] for f in recovered], [p.CMD_PING])

    def test_slow_trickle_does_not_extend_timeout(self):
        parser = p.FrameParser()
        frame = p.encode_frame(p.CMD_PING)
        self.assertEqual(parser.feed(frame[:2], 0), [])
        self.assertEqual(parser.feed(frame[2:3], 150), [])
        self.assertEqual(parser.expire(200), [])
        self.assertEqual(parser.stats["partial_timeouts"], 1)

    def test_concatenated_frames(self):
        stream = b"".join(p.encode_frame(p.CMD_PING, seq=n) for n in range(30))
        parser = p.FrameParser()
        self.assertEqual([f["seq"] for f in parser.feed(stream, 0)], list(range(30)))
        self.assertEqual(parser.buffered_bytes, 0)

    def test_random_chunking_preserves_long_mixed_stream(self):
        rng = random.Random(103)
        stream = bytearray()
        expected = []
        for seq in range(200):
            data = bytes(rng.randrange(256) for _ in range(seq % 33))
            stream.extend(b"\x00noise")
            stream.extend(p.encode_frame(0x90, data, seq))
            expected.append((seq, data))
        parser = p.FrameParser()
        actual = []
        index = 0
        while index < len(stream):
            count = rng.randrange(1, 37)
            actual.extend((f["seq"], f["data"])
                          for f in parser.feed(stream[index:index + count], 0))
            index += count
        self.assertEqual(actual, expected)
        self.assertEqual(parser.stats["frames"], 200)
        self.assertLessEqual(parser.stats["max_buffer_seen"], p.MAX_FRAME_LENGTH)

    def test_every_possible_split(self):
        data = p.pack_message(p.CMD_AUTH_SUCCESS, user_id=1, similarity=0.9)
        frame = p.encode_frame(p.CMD_AUTH_SUCCESS, data, seq=99)
        for split in range(1, len(frame)):
            with self.subTest(split=split):
                parser = p.FrameParser()
                self.assertEqual(parser.feed(frame[:split], 0), [])
                received = parser.feed(frame[split:], 1)
                self.assertEqual(len(received), 1)
                self.assertEqual(received[0]["seq"], 99)

    def test_one_byte_chunks_and_split_header(self):
        parser = p.FrameParser()
        received = []
        frame = p.encode_frame(p.CMD_PING, seq=7)
        for index, byte in enumerate(frame):
            received += parser.feed(bytes((byte,)), index)
        self.assertEqual([f["seq"] for f in received], [7])

    def test_illegal_length_resynchronizes(self):
        parser = p.FrameParser()
        illegal = b"\xaa\x55\x01\x10\x00\x00\x21"
        frames = parser.feed(illegal + p.encode_frame(p.CMD_PING), 0)
        self.assertEqual(len(frames), 1)
        self.assertEqual(parser.stats["invalid_lengths"], 1)
        with self.assertRaises(ValueError):
            p.encode_frame(p.CMD_PING, b"a" * 33)

    def test_unknown_cmd_is_validated_and_retained(self):
        parser = p.FrameParser()
        frame = parser.feed(p.encode_frame(0x90, b"abc", seq=12), 0)[0]
        self.assertFalse(frame["known"])
        self.assertEqual(frame["data"], b"abc")
        self.assertEqual(parser.stats["unknown_commands"], 1)
        message = p.decode_message(frame)
        self.assertEqual(message["name"], "UNKNOWN_CMD")
        self.assertFalse(message["known"])

    def test_noise_duplicate_aa_and_embedded_header(self):
        parser = p.FrameParser()
        good = p.encode_frame(0x90, b"\xaa\x55\x01\x00")
        frames = parser.feed(b"noise\x00\xaa" + good, 0)
        self.assertEqual(len(frames), 1)
        self.assertEqual(frames[0]["data"], b"\xaa\x55\x01\x00")
        self.assertEqual(parser.stats["noise_bytes"], 7)

    def test_wrong_version_resynchronizes(self):
        parser = p.FrameParser()
        frame = bytearray(p.encode_frame(p.CMD_PING))
        frame[2] = 2
        frames = parser.feed(frame + p.encode_frame(p.CMD_PING, seq=1), 0)
        self.assertEqual([f["seq"] for f in frames], [1])
        self.assertEqual(parser.stats["unsupported_versions"], 1)

    def test_maximum_length_and_large_noise_have_bounded_pending_ram(self):
        parser = p.FrameParser()
        maximum = p.encode_frame(0x90, b"\xaa" * p.MAX_DATA_LENGTH)
        self.assertEqual(len(maximum), p.MAX_FRAME_LENGTH)
        frames = parser.feed(b"x" * 10000 + maximum + b"\xaa" * 10000, 0)
        self.assertEqual(len(frames), 1)
        self.assertLessEqual(parser.stats["max_buffer_seen"], p.MAX_FRAME_LENGTH)
        self.assertEqual(parser.buffered_bytes, 1)

    def test_all_message_schemas_round_trip(self):
        cases = [
            (p.CMD_MODULE_READY, {"api_major": 1, "api_minor": 0,
                                  "feature_dimension": 512, "threshold": 0.75}),
            (p.CMD_AUTH_SUCCESS, {"user_id": 65535, "similarity": 1.0}),
            (p.CMD_UNKNOWN, {"reason": p.UNKNOWN_UNMATCHED}),
            (p.CMD_NO_FACE, {}),
            (p.CMD_ERROR, {"error_code": p.ERROR_AI, "detail": 123}),
            (p.CMD_PING, {}), (p.CMD_GET_STATUS, {}), (p.CMD_PONG, {}),
            (p.CMD_STATUS, {"state": 2, "last_event": p.CMD_AUTH_SUCCESS,
                           "last_user_id": 1, "last_similarity": 0.9,
                           "last_error": 0}),
        ]
        for cmd, fields in cases:
            with self.subTest(cmd=cmd):
                data = p.pack_message(cmd, **fields)
                self.assertEqual(len(data), p.MESSAGE_LENGTHS[cmd])
                self.assertTrue(p.decode_message(parsed(cmd, data))["known"])

    def test_payload_length_errors(self):
        for cmd, length in p.MESSAGE_LENGTHS.items():
            with self.subTest(cmd=cmd):
                with self.assertRaises(ValueError):
                    p.decode_message(parsed(cmd, b"\x00" * (length + 1)))

    def test_semantic_payload_errors(self):
        invalid = [
            (p.CMD_AUTH_SUCCESS, b"\x00\x00\x00\x00"),
            (p.CMD_AUTH_SUCCESS, b"\x01\x00\x11\x27"),
            (p.CMD_UNKNOWN, b"\x00"),
            (p.CMD_ERROR, b"\x00\x00\x00\x00"),
            (p.CMD_MODULE_READY, b"\x01\x00\x00\x00\x4c\x1d"),
            (p.CMD_STATUS, b"\x02\x10\x01\x00\x11\x27\x00\x00"),
        ]
        for cmd, data in invalid:
            with self.subTest(cmd=cmd, data=data):
                with self.assertRaises(ValueError):
                    p.decode_message(parsed(cmd, data))

    def test_pack_rejects_invalid_fields(self):
        for user_id in (0, 65536, -1, True, 1.5):
            with self.subTest(user_id=user_id):
                with self.assertRaises(ValueError):
                    p.pack_message(p.CMD_AUTH_SUCCESS, user_id=user_id, similarity=0.9)
        for similarity in (-0.01, 1.01, float("nan"), float("inf"), True, "0.9"):
            with self.subTest(similarity=similarity):
                with self.assertRaises(ValueError):
                    p.pack_message(p.CMD_AUTH_SUCCESS, user_id=1, similarity=similarity)
        with self.assertRaises(ValueError):
            p.pack_message(p.CMD_PING, typo=1)
        with self.assertRaises(ValueError):
            p.pack_message(p.CMD_AUTH_SUCCESS, user_id=1)

    def test_encode_rejects_invalid_values(self):
        for cmd, seq, data in ((256, 0, b""), (-1, 0, b""),
                               (p.CMD_PING, 65536, b""),
                               (p.CMD_PING, -1, b""),
                               (p.CMD_PING, 0, "not bytes")):
            with self.assertRaises(ValueError):
                p.encode_frame(cmd, data, seq)


class FakeUART:
    def __init__(self, reads=None, max_write=None, write_result=None):
        self.reads = list(reads or [])
        self.written = bytearray()
        self.max_write = max_write
        self.write_result = write_result
        self.read_count = 0

    def read(self, size):
        self.read_count += 1
        if not self.reads:
            return None
        data = self.reads.pop(0)
        if len(data) > size:
            self.reads.insert(0, data[size:])
            data = data[:size]
        return data

    def write(self, data):
        if self.write_result is not None:
            return self.write_result
        count = min(len(data), self.max_write) if self.max_write else len(data)
        self.written.extend(data[:count])
        return count


class TransportTests(unittest.TestCase):
    def test_async_sequence_and_request_echo(self):
        uart = FakeUART()
        transport = p.ProtocolUART(uart)
        self.assertEqual(transport.send(p.CMD_NO_FACE), 0)
        self.assertEqual(transport.send(p.CMD_PONG, seq=400), 400)
        self.assertEqual(transport.send(p.CMD_NO_FACE), 1)
        frames = p.FrameParser().feed(uart.written, 0)
        self.assertEqual([f["seq"] for f in frames], [0, 400, 1])

    def test_async_sequence_wrap(self):
        uart = FakeUART()
        transport = p.ProtocolUART(uart)
        transport._next_seq = 65535
        self.assertEqual(transport.send(p.CMD_NO_FACE), 65535)
        self.assertEqual(transport.send(p.CMD_NO_FACE), 0)

    def test_partial_uart_write_is_completed(self):
        uart = FakeUART(max_write=2)
        transport = p.ProtocolUART(uart)
        transport.send_message(p.CMD_AUTH_SUCCESS, user_id=1, similarity=0.9)
        self.assertEqual(len(p.FrameParser().feed(uart.written, 0)), 1)

    def test_write_timeout_and_retry_limit_are_errors(self):
        with self.assertRaises(OSError):
            p.ProtocolUART(FakeUART(write_result=0)).send(p.CMD_NO_FACE)
        with self.assertRaises(OSError):
            p.ProtocolUART(FakeUART(max_write=1), max_write_calls=2).send(p.CMD_NO_FACE)

    def test_send_message_uses_schema_validation(self):
        uart = FakeUART()
        transport = p.ProtocolUART(uart)
        with self.assertRaises(ValueError):
            transport.send_message(p.CMD_AUTH_SUCCESS, user_id=0, similarity=0.9)
        self.assertEqual(uart.written, b"")

    def test_poll_split_frames_and_budget(self):
        first = p.encode_frame(p.CMD_PING, seq=1)
        second = p.encode_frame(p.CMD_GET_STATUS, seq=2)
        uart = FakeUART([first[:3], first[3:], second])
        transport = p.ProtocolUART(uart, max_reads_per_poll=2)
        self.assertEqual([f["seq"] for f in transport.poll(0)], [1])
        self.assertEqual(uart.read_count, 2)
        self.assertEqual([f["seq"] for f in transport.poll(1)], [2])

    def test_poll_checks_timeout_without_received_bytes(self):
        uart = FakeUART([p.encode_frame(p.CMD_PING)[:-1]])
        transport = p.ProtocolUART(uart)
        self.assertEqual(transport.poll(0), [])
        self.assertEqual(transport.poll(200), [])
        self.assertEqual(transport.parser.stats["partial_timeouts"], 1)


if __name__ == "__main__":
    unittest.main()
