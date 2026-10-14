"""Bounded UART V1 framing shared by CanMV MicroPython and the PC monitor.

Wire: AA 55, VERSION, CMD, SEQ_LE16, LEN, DATA, CRC_LE16.
CRC-16/CCITT-FALSE covers VERSION through DATA (header excluded).
This module knows identity messages, not access permissions or actuators.
"""

try:
    from time import ticks_diff as _ticks_diff
except ImportError:
    def _ticks_diff(now, then):
        return now - then


FRAME_HEADER = b"\xaa\x55"
PROTOCOL_VERSION = 1
MAX_DATA_LENGTH = 32
FRAME_OVERHEAD = 9
MAX_FRAME_LENGTH = FRAME_OVERHEAD + MAX_DATA_LENGTH
PARTIAL_TIMEOUT_MS = 200
UART_READ_CHUNK = 128
UART_MAX_READS_PER_POLL = 4
UART_MAX_WRITE_CALLS = 8
SIMILARITY_SCALE = 10000

CMD_MODULE_READY = 0x01
CMD_AUTH_SUCCESS = 0x10
CMD_UNKNOWN = 0x11
CMD_AUTH_FAIL = CMD_UNKNOWN
CMD_NO_FACE = 0x12
CMD_ERROR = 0x7F
CMD_PING = 0x80
CMD_GET_STATUS = 0x81
CMD_PONG = 0x82
CMD_STATUS = 0x83

UNKNOWN_UNMATCHED = 1
UNKNOWN_MULTIPLE_FACES = 2
UNKNOWN_UNMAPPED_IDENTITY = 3

ERROR_AI = 0x0001
ERROR_UART = 0x0002
ERROR_DATABASE = 0x0003
ERROR_CONFIG = 0x0004
ERROR_CAMERA = 0x0005
ERROR_MODEL = 0x0006
ERROR_CLEANUP = 0x0007
ERROR_PROTOCOL = 0x0010
ERROR_UNKNOWN_CMD = 0x0011
ERROR_BAD_PAYLOAD = 0x0012
ERROR_INTERNAL = 0x00FF

COMMAND_NAMES = {
    CMD_MODULE_READY: "MODULE_READY",
    CMD_AUTH_SUCCESS: "AUTH_SUCCESS",
    CMD_UNKNOWN: "UNKNOWN",
    CMD_NO_FACE: "NO_FACE",
    CMD_ERROR: "ERROR",
    CMD_PING: "PING",
    CMD_GET_STATUS: "GET_STATUS",
    CMD_PONG: "PONG",
    CMD_STATUS: "STATUS",
}
MESSAGE_LENGTHS = {
    CMD_MODULE_READY: 6,
    CMD_AUTH_SUCCESS: 4,
    CMD_UNKNOWN: 1,
    CMD_NO_FACE: 0,
    CMD_ERROR: 4,
    CMD_PING: 0,
    CMD_GET_STATUS: 0,
    CMD_PONG: 0,
    CMD_STATUS: 8,
}


def _integer(value, low, high, label):
    if isinstance(value, bool) or not isinstance(value, int):
        raise ValueError(label + " must be an integer")
    if value < low or value > high:
        raise ValueError(label + " is out of range")
    return value


def _u16(value):
    return bytes((value & 0xFF, (value >> 8) & 0xFF))


def _read_u16(data, offset):
    return data[offset] | (data[offset + 1] << 8)


def _scaled(value, label):
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ValueError(label + " must be numeric")
    # The comparisons also reject NaN; infinity fails the range check.
    if not (0 <= value <= 1):
        raise ValueError(label + " must be between 0 and 1")
    return int(value * SIMILARITY_SCALE + 0.5)


def crc16(data):
    """CRC-16/CCITT-FALSE; check vector b'123456789' -> 0x29B1."""
    crc = 0xFFFF
    for value in data:
        crc ^= value << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


def encode_frame(cmd, data=b"", seq=0):
    """Encode one frame. Unknown command bytes are permitted for diagnostics."""
    _integer(cmd, 0, 255, "cmd")
    _integer(seq, 0, 65535, "seq")
    if not isinstance(data, (bytes, bytearray, memoryview)):
        raise ValueError("data must be bytes-like")
    if len(data) > MAX_DATA_LENGTH:
        raise ValueError("data exceeds MAX_DATA_LENGTH")
    body = bytes((PROTOCOL_VERSION, cmd)) + _u16(seq)
    body += bytes((len(data),)) + bytes(data)
    return FRAME_HEADER + body + _u16(crc16(body))


def pack_message(cmd, **fields):
    """Encode a message DATA field using named, validated application fields.

    Similarity and threshold are floats in [0, 1], not pre-scaled integers.
    Unexpected fields are rejected, so a misspelled field cannot silently vanish.
    """
    _integer(cmd, 0, 255, "cmd")
    def take(name, default=None, optional=False):
        if name in fields:
            return fields.pop(name)
        if optional:
            return default
        raise ValueError("missing field: " + name)

    if cmd == CMD_MODULE_READY:
        major = _integer(take("api_major"), 0, 255, "api_major")
        minor = _integer(take("api_minor"), 0, 255, "api_minor")
        dimension = _integer(take("feature_dimension"), 1, 65535,
                             "feature_dimension")
        threshold = _scaled(take("threshold"), "threshold")
        data = bytes((major, minor)) + _u16(dimension) + _u16(threshold)
    elif cmd == CMD_AUTH_SUCCESS:
        user_id = _integer(take("user_id"), 1, 65535, "user_id")
        similarity = _scaled(take("similarity"), "similarity")
        data = _u16(user_id) + _u16(similarity)
    elif cmd == CMD_UNKNOWN:
        reason = _integer(take("reason"), UNKNOWN_UNMATCHED,
                          UNKNOWN_UNMAPPED_IDENTITY, "reason")
        data = bytes((reason,))
    elif cmd == CMD_ERROR:
        code = _integer(take("error_code"), 1, 65535, "error_code")
        detail = _integer(take("detail", 0, True), 0, 65535, "detail")
        data = _u16(code) + _u16(detail)
    elif cmd == CMD_STATUS:
        state = _integer(take("state"), 0, 255, "state")
        last_event = _integer(take("last_event"), 0, 255, "last_event")
        last_user = _integer(take("last_user_id"), 0, 65535, "last_user_id")
        similarity = _scaled(take("last_similarity"), "last_similarity")
        last_error = _integer(take("last_error"), 0, 65535, "last_error")
        data = bytes((state, last_event)) + _u16(last_user)
        data += _u16(similarity) + _u16(last_error)
    elif cmd in (CMD_NO_FACE, CMD_PING, CMD_GET_STATUS, CMD_PONG):
        data = b""
    else:
        raise ValueError("unknown message cmd")
    if fields:
        raise ValueError("unexpected fields: " + ", ".join(sorted(fields)))
    return data


def decode_message(frame):
    """Decode a parsed frame. CRC validation belongs to FrameParser.

    Known commands with invalid payloads raise ValueError. Unknown commands are
    returned with name='UNKNOWN_CMD' and raw data, so clients can decide policy.
    """
    if frame.get("version") != PROTOCOL_VERSION:
        raise ValueError("unsupported protocol version")
    cmd = _integer(frame.get("cmd"), 0, 255, "cmd")
    seq = _integer(frame.get("seq"), 0, 65535, "seq")
    data = frame.get("data")
    if not isinstance(data, (bytes, bytearray, memoryview)):
        raise ValueError("data must be bytes-like")
    if len(data) > MAX_DATA_LENGTH:
        raise ValueError("data exceeds MAX_DATA_LENGTH")
    result = {"cmd": cmd, "name": COMMAND_NAMES.get(cmd, "UNKNOWN_CMD"),
              "seq": seq}
    if cmd not in MESSAGE_LENGTHS:
        result["data"] = bytes(data)
        result["known"] = False
        return result
    if len(data) != MESSAGE_LENGTHS[cmd]:
        raise ValueError("incorrect payload length for " + result["name"])
    result["known"] = True
    if cmd == CMD_MODULE_READY:
        dimension = _read_u16(data, 2)
        threshold_q = _read_u16(data, 4)
        if dimension == 0 or threshold_q > SIMILARITY_SCALE:
            raise ValueError("invalid MODULE_READY payload")
        result.update({"api_major": data[0], "api_minor": data[1],
                       "feature_dimension": dimension,
                       "threshold_q": threshold_q,
                       "threshold": threshold_q / SIMILARITY_SCALE})
    elif cmd == CMD_AUTH_SUCCESS:
        user_id = _read_u16(data, 0)
        similarity_q = _read_u16(data, 2)
        if user_id == 0 or similarity_q > SIMILARITY_SCALE:
            raise ValueError("invalid AUTH_SUCCESS payload")
        result.update({"user_id": user_id, "similarity_q": similarity_q,
                       "similarity": similarity_q / SIMILARITY_SCALE})
    elif cmd == CMD_UNKNOWN:
        if data[0] not in (UNKNOWN_UNMATCHED, UNKNOWN_MULTIPLE_FACES,
                          UNKNOWN_UNMAPPED_IDENTITY):
            raise ValueError("invalid UNKNOWN reason")
        result["reason"] = data[0]
    elif cmd == CMD_ERROR:
        code = _read_u16(data, 0)
        if code == 0:
            raise ValueError("ERROR requires a nonzero error code")
        result.update({"error_code": code, "detail": _read_u16(data, 2)})
    elif cmd == CMD_STATUS:
        similarity_q = _read_u16(data, 4)
        if similarity_q > SIMILARITY_SCALE:
            raise ValueError("invalid STATUS similarity")
        result.update({"state": data[0], "last_event": data[1],
                       "last_user_id": _read_u16(data, 2),
                       "last_similarity_q": similarity_q,
                       "last_similarity": similarity_q / SIMILARITY_SCALE,
                       "last_error": _read_u16(data, 6)})
    return result


class FrameParser:
    """Incremental byte-stream parser; pending storage never exceeds 41 bytes.

    A partial frame expires from its first byte, not from each received byte.
    At expiry the first byte is discarded and the remainder is scanned, which
    recovers a good frame hidden behind a truncated bogus candidate.
    """

    def __init__(self, partial_timeout_ms=PARTIAL_TIMEOUT_MS):
        _integer(partial_timeout_ms, 1, 60000, "partial_timeout_ms")
        self.partial_timeout_ms = partial_timeout_ms
        self._buffer = bytearray()
        self._started_ms = None
        self.stats = {"frames": 0, "crc_errors": 0, "invalid_lengths": 0,
                      "unsupported_versions": 0, "noise_bytes": 0,
                      "partial_timeouts": 0, "unknown_commands": 0,
                      "max_buffer_seen": 0}

    @property
    def buffered_bytes(self):
        return len(self._buffer)

    def reset(self):
        self._buffer = bytearray()
        self._started_ms = None

    def _scan(self, now_ms):
        frames = []
        while self._buffer:
            header = self._buffer.find(FRAME_HEADER)
            if header < 0:
                keep = 1 if self._buffer[-1] == FRAME_HEADER[0] else 0
                discard = len(self._buffer) - keep
                self.stats["noise_bytes"] += discard
                if discard:
                    self._buffer = self._buffer[discard:]
                    self._started_ms = None
                break
            if header:
                self.stats["noise_bytes"] += header
                self._buffer = self._buffer[header:]
                self._started_ms = None
            if len(self._buffer) < 3:
                break
            if self._buffer[2] != PROTOCOL_VERSION:
                self.stats["unsupported_versions"] += 1
                self._buffer = self._buffer[1:]
                self._started_ms = None
                continue
            if len(self._buffer) < 7:
                break
            payload_length = self._buffer[6]
            if payload_length > MAX_DATA_LENGTH:
                self.stats["invalid_lengths"] += 1
                self._buffer = self._buffer[1:]
                self._started_ms = None
                continue
            frame_length = FRAME_OVERHEAD + payload_length
            if len(self._buffer) < frame_length:
                break
            received_crc = _read_u16(self._buffer, frame_length - 2)
            calculated_crc = crc16(self._buffer[2:frame_length - 2])
            if received_crc != calculated_crc:
                self.stats["crc_errors"] += 1
                self._buffer = self._buffer[1:]
                self._started_ms = None
                continue
            cmd = self._buffer[3]
            known = cmd in COMMAND_NAMES
            frames.append({"version": self._buffer[2], "cmd": cmd,
                           "seq": _read_u16(self._buffer, 4),
                           "data": bytes(self._buffer[7:frame_length - 2]),
                           "known": known})
            self.stats["frames"] += 1
            if not known:
                self.stats["unknown_commands"] += 1
            self._buffer = self._buffer[frame_length:]
            self._started_ms = None
        if self._buffer and self._started_ms is None:
            self._started_ms = now_ms
        elif not self._buffer:
            self._started_ms = None
        return frames

    def expire(self, now_ms):
        if self._started_ms is None:
            return []
        if _ticks_diff(now_ms, self._started_ms) < self.partial_timeout_ms:
            return []
        self.stats["partial_timeouts"] += 1
        self._buffer = self._buffer[1:]
        self._started_ms = None
        return self._scan(now_ms)

    def feed(self, data, now_ms):
        if not isinstance(data, (bytes, bytearray, memoryview)):
            raise ValueError("data must be bytes-like")
        frames = self.expire(now_ms)
        # Process per byte so even huge noise input cannot grow pending storage.
        for value in data:
            self._buffer.append(value)
            if len(self._buffer) > self.stats["max_buffer_seen"]:
                self.stats["max_buffer_seen"] = len(self._buffer)
            frames.extend(self._scan(now_ms))
        return frames


class ProtocolUART:
    """UART adapter. All product serial reads/writes stay behind this interface.

    Inject a machine.UART object or a fake with read(size)/write(bytes). The UART
    must have bounded read/write timeouts. Failed writes raise OSError, allowing
    the runtime to record ERROR_UART without recursive error-send attempts.
    """

    def __init__(self, uart, parser=None, read_chunk=UART_READ_CHUNK,
                 max_reads_per_poll=UART_MAX_READS_PER_POLL,
                 max_write_calls=UART_MAX_WRITE_CALLS):
        if uart is None:
            raise ValueError("uart is required")
        self.uart = uart
        self.parser = parser if parser is not None else FrameParser()
        self.read_chunk = _integer(read_chunk, 1, 1024, "read_chunk")
        self.max_reads_per_poll = _integer(max_reads_per_poll, 1, 32,
                                          "max_reads_per_poll")
        self.max_write_calls = _integer(max_write_calls, 1, 64,
                                       "max_write_calls")
        self._next_seq = 0

    def send(self, cmd, data=b"", seq=None):
        async_message = seq is None
        sequence = self._next_seq if async_message else seq
        frame = encode_frame(cmd, data, sequence)
        offset = 0
        for _ in range(self.max_write_calls):
            written = self.uart.write(frame[offset:])
            # MicroPython UART.write() returns a byte count or None on timeout.
            if written is None or written == 0:
                raise OSError("UART write timed out")
            if not isinstance(written, int) or written < 0:
                raise OSError("invalid UART write count")
            if written > len(frame) - offset:
                raise OSError("UART write count exceeds remaining frame")
            offset += written
            if offset == len(frame):
                if async_message:
                    self._next_seq = (sequence + 1) & 0xFFFF
                return sequence
        raise OSError("UART incomplete write after bounded retries")

    def send_message(self, cmd, seq=None, **fields):
        return self.send(cmd, pack_message(cmd, **fields), seq)

    def poll(self, now_ms):
        frames = self.parser.expire(now_ms)
        for _ in range(self.max_reads_per_poll):
            data = self.uart.read(self.read_chunk)
            if not data:
                break
            if len(data) > self.read_chunk:
                raise OSError("UART.read returned more than the requested size")
            frames.extend(self.parser.feed(data, now_ms))
        return frames
