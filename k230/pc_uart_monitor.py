"""PC receiver for the K230D identity module, with offline replay support.

This program decodes identity reports. It makes no access or door decisions.
``--demo`` and ``--replay`` are explicitly labelled software simulations.
PySerial is imported only when a real serial port is requested.
"""

import argparse
import json
import sys
import time
from pathlib import Path

from uart_protocol import (
    CMD_AUTH_SUCCESS, CMD_ERROR, CMD_GET_STATUS, CMD_MODULE_READY, CMD_NO_FACE,
    CMD_PING, CMD_PONG, CMD_STATUS, CMD_UNKNOWN, PARTIAL_TIMEOUT_MS,
    FrameParser, decode_message, encode_frame, pack_message,
)


REQUEST_RESPONSES = {CMD_PING: CMD_PONG, CMD_GET_STATUS: CMD_STATUS}
REQUEST_TIMEOUT_MS = 3000
REPLAY_EXPIRY_ADVANCE_MS = PARTIAL_TIMEOUT_MS + 1
DEFAULT_READ_SIZE = 256
MAX_READ_SIZE = 4096
SERIAL_READ_TIMEOUT_SECONDS = 0.1
SERIAL_WRITE_TIMEOUT_SECONDS = 1


def _json_default(value):
    if isinstance(value, (bytes, bytearray, memoryview)):
        return bytes(value).hex()
    raise TypeError("Unsupported JSON value: " + type(value).__name__)


def json_output(record):
    """Emit one JSON object per line, suitable for humans and log parsers."""
    print(json.dumps(record, ensure_ascii=False, sort_keys=True, default=_json_default), flush=True)


class MonitorSession:
    """Decode arbitrary chunks and keep request correlation outside the codec."""

    def __init__(self, emit=json_output, mode="serial"):
        self.parser = FrameParser()
        self.emit = emit
        self.mode = mode
        self.next_seq = 0
        self.pending = {}
        self.rx_bytes = 0
        self.rx_frames = 0
        self.payload_errors = 0
        self._stats_snapshot = dict(self.parser.stats)

    def _emit(self, record):
        record = dict(record)
        record["mode"] = self.mode
        self.emit(record)

    def build_request(self, cmd, now_ms):
        """Return a query frame; replies must echo this request's SEQ."""
        if cmd not in REQUEST_RESPONSES:
            raise ValueError("Only PING and GET_STATUS are PC query commands")
        for _ in range(65536):
            seq = self.next_seq
            self.next_seq = (seq + 1) & 0xFFFF
            if seq not in self.pending:
                break
        else:
            raise RuntimeError("All request sequence numbers are occupied")
        frame = encode_frame(cmd, seq=seq)
        self.pending[seq] = {
            "cmd": cmd,
            "expected_cmd": REQUEST_RESPONSES[cmd],
            "sent_ms": now_ms,
        }
        self._emit({"record": "request_prepared", "cmd": cmd, "seq": seq,
                    "expected_response_cmd": REQUEST_RESPONSES[cmd],
                    "timestamp_ms": now_ms})
        return frame

    def _handle_frame(self, frame, now_ms):
        self.rx_frames += 1
        output = {
            "record": "frame",
            "timestamp_ms": now_ms,
            "version": frame["version"],
            "cmd": frame["cmd"],
            "seq": frame["seq"],
            "known": frame["known"],
            "data_hex": bytes(frame["data"]).hex(),
        }
        try:
            output["message"] = decode_message(frame)
        except Exception as exc:
            # A checksum-valid frame can still have an invalid CMD payload.
            # Report that input explicitly and continue receiving subsequent frames.
            self.payload_errors += 1
            output["message"] = None
            output["payload_error"] = str(exc)
        if frame["cmd"] in REQUEST_RESPONSES.values():
            pending = self.pending.get(frame["seq"])
            if pending and pending["expected_cmd"] == frame["cmd"] and output["message"] is not None:
                output["request_match"] = "matched"
                output["request_cmd"] = pending["cmd"]
                output["round_trip_ms"] = now_ms - pending["sent_ms"]
                del self.pending[frame["seq"]]
            elif pending and pending["expected_cmd"] == frame["cmd"]:
                output["request_match"] = "invalid_response"
            else:
                output["request_match"] = "unmatched_response"
        self._emit(output)

    def _emit_parser_diagnostics(self, now_ms, reason=None):
        for key, value in self.parser.stats.items():
            previous = self._stats_snapshot.get(key, 0)
            if (isinstance(value, int) and value > previous and
                    any(token in key.lower() for token in
                        ("crc", "error", "invalid", "timeout", "discard",
                         "unknown", "truncated", "overflow", "version"))):
                record = {"record": "diagnostic", "counter": key,
                          "delta": value - previous, "total": value,
                          "timestamp_ms": now_ms}
                if reason:
                    record["reason"] = reason
                self._emit(record)
        self._stats_snapshot = dict(self.parser.stats)

    def feed(self, data, now_ms):
        self.rx_bytes += len(data)
        frames = self.parser.feed(data, now_ms)
        for frame in frames:
            self._handle_frame(frame, now_ms)
        self._emit_parser_diagnostics(now_ms)
        return len(frames)

    def finish(self, now_ms):
        """Flush a final fragment without pretending a real timeout elapsed."""
        for frame in self.parser.expire(now_ms + REPLAY_EXPIRY_ADVANCE_MS):
            self._handle_frame(frame, now_ms)
        self._emit_parser_diagnostics(now_ms, reason="end_of_stream")
        for seq, pending in list(self.pending.items()):
            self._emit({"record": "request_cancelled", "cmd": pending["cmd"],
                        "seq": seq, "reason": "session_ended", "timestamp_ms": now_ms})
            del self.pending[seq]
        return self.summary()

    def poll(self, now_ms):
        frames = self.parser.expire(now_ms)
        for frame in frames:
            self._handle_frame(frame, now_ms)
        self._emit_parser_diagnostics(now_ms)
        for seq, pending in list(self.pending.items()):
            if now_ms - pending["sent_ms"] >= REQUEST_TIMEOUT_MS:
                self._emit({"record": "request_timeout", "cmd": pending["cmd"],
                            "seq": seq, "timestamp_ms": now_ms})
                del self.pending[seq]
        return len(frames)

    def summary(self):
        record = {"record": "summary", "rx_bytes": self.rx_bytes,
                  "rx_frames": self.rx_frames, "payload_errors": self.payload_errors,
                  "pending_requests": len(self.pending),
                  "parser_stats": dict(self.parser.stats)}
        self._emit(record)
        return record


def replay_stream(stream, session, chunk_size=DEFAULT_READ_SIZE, capture=None):
    """Decode recorded bytes; deterministic timestamps do not imply hardware time."""
    now_ms = 0
    while True:
        data = stream.read(chunk_size)
        if not data:
            break
        if capture is not None:
            capture.write(data)
        session.feed(data, now_ms)
        now_ms += 1
    return session.finish(now_ms)


def write_all_uart(serial_port, frame):
    """Handle short writes, and fail explicitly if the serial writer stalls."""
    offset = 0
    while offset < len(frame):
        written = serial_port.write(frame[offset:])
        if not isinstance(written, int) or written <= 0:
            raise IOError("Serial write made no progress")
        if written > len(frame) - offset:
            raise IOError("Serial writer returned an impossible byte count")
        offset += written


def demo_stream():
    """Return fabricated identity messages; this is not a real camera/UART test."""
    frames = [
        encode_frame(CMD_MODULE_READY, pack_message(CMD_MODULE_READY, api_major=1, api_minor=0,
                                      feature_dimension=512, threshold=0.75), seq=0),
        encode_frame(CMD_AUTH_SUCCESS, pack_message(CMD_AUTH_SUCCESS, user_id=1, similarity=0.90), seq=1),
        encode_frame(CMD_UNKNOWN, pack_message(CMD_UNKNOWN, reason=1), seq=2),
        encode_frame(CMD_NO_FACE, pack_message(CMD_NO_FACE), seq=3),
        encode_frame(CMD_ERROR, pack_message(CMD_ERROR, error_code=1, detail=0), seq=4),
    ]
    return b"".join(frames)


def import_pyserial():
    # Bundled embedded Python may ignore PYTHONPATH; use this project's copy.
    local_dependencies = Path(__file__).resolve().parent / 'pc_dependencies'
    if local_dependencies.is_dir() and str(local_dependencies) not in sys.path:
        sys.path.insert(0, str(local_dependencies))
    try:
        import serial
        import serial.tools.list_ports
        return serial
    except ImportError as exc:
        raise RuntimeError(
            "Real serial mode requires pyserial. Install it with the same Python: "
            "python -m pip install pyserial. Offline --demo/--replay do not require it."
        ) from exc


def positive_int(value):
    parsed = int(value)
    if parsed <= 0:
        raise argparse.ArgumentTypeError("must be positive")
    return parsed


def positive_float(value):
    parsed = float(value)
    if parsed <= 0 or not parsed < float("inf"):
        raise argparse.ArgumentTypeError("must be finite and positive")
    return parsed


def build_argument_parser():
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--list-ports", action="store_true", help="List available serial ports")
    mode.add_argument("--port", help="Explicit physical UART COM port; never selected automatically")
    mode.add_argument("--replay", type=Path, help="Replay a raw binary capture (software test)")
    mode.add_argument("--demo", action="store_true", help="Decode fabricated frames (software simulation)")
    parser.add_argument("--baud", type=positive_int, default=115200)
    parser.add_argument("--ping", action="store_true", help="Send one PING query in serial mode")
    parser.add_argument("--get-status", action="store_true", help="Send one GET_STATUS query in serial mode")
    parser.add_argument("--capture", type=Path, help="Save received raw bytes to a NEW binary file")
    parser.add_argument("--chunk-size", type=positive_int, default=DEFAULT_READ_SIZE)
    parser.add_argument("--duration", type=positive_float, help="Stop serial mode after this many seconds")
    return parser


def run_serial(args, capture):
    serial = import_pyserial()
    session = MonitorSession(mode="serial")
    with serial.Serial(port=args.port, baudrate=args.baud, bytesize=8,
                       parity="N", stopbits=1, timeout=SERIAL_READ_TIMEOUT_SECONDS,
                       write_timeout=SERIAL_WRITE_TIMEOUT_SECONDS,
                       xonxoff=False, rtscts=False, dsrdtr=False) as port:
        start = time.monotonic()
        json_output({"record": "session", "mode": "serial", "port": args.port,
                     "baud": args.baud, "format": "8N1", "flow_control": "none"})
        try:
            for enabled, command in ((args.ping, CMD_PING), (args.get_status, CMD_GET_STATUS)):
                if enabled:
                    now_ms = int((time.monotonic() - start) * 1000)
                    wire = session.build_request(command, now_ms)
                    write_all_uart(port, wire)
                    json_output({"record": "request_sent", "mode": "serial", "cmd": command,
                                 "seq": int.from_bytes(wire[4:6], "little"),
                                 "timestamp_ms": int((time.monotonic() - start) * 1000)})
            while True:
                now_ms = int((time.monotonic() - start) * 1000)
                if args.duration is not None and now_ms >= args.duration * 1000:
                    break
                data = port.read(args.chunk_size)
                now_ms = int((time.monotonic() - start) * 1000)
                if data:
                    if capture is not None:
                        capture.write(data)
                        capture.flush()
                    session.feed(data, now_ms)
                session.poll(now_ms)
        except KeyboardInterrupt:
            json_output({"record": "session_end", "mode": "serial", "reason": "keyboard_interrupt"})
        finally:
            session.finish(int((time.monotonic() - start) * 1000))
    return 0


def main(argv=None):
    parser = build_argument_parser()
    args = parser.parse_args(argv)
    if args.chunk_size > MAX_READ_SIZE:
        parser.error("--chunk-size must be at most " + str(MAX_READ_SIZE))
    if (args.ping or args.get_status or args.duration is not None) and not args.port:
        parser.error("--ping, --get-status and --duration require --port")
    if args.capture and args.list_ports:
        parser.error("--capture cannot be used with --list-ports")
    capture = None
    try:
        if args.list_ports:
            serial = import_pyserial()
            for port in serial.tools.list_ports.comports():
                json_output({"record": "port", "device": port.device,
                             "description": port.description, "hwid": port.hwid})
            return 0
        if args.capture:
            # Refuse overwrites; preserve existing captures by default.
            capture = args.capture.open("xb")
        if args.port:
            return run_serial(args, capture)
        mode = "simulated_demo" if args.demo else "recorded_replay"
        json_output({"record": "session", "mode": mode, "hardware_test": False})
        session = MonitorSession(mode=mode)
        if args.demo:
            import io
            replay_stream(io.BytesIO(demo_stream()), session, args.chunk_size, capture)
        else:
            with args.replay.open("rb") as source:
                replay_stream(source, session, args.chunk_size, capture)
        return 0
    except (OSError, RuntimeError, ValueError) as exc:
        json_output({"record": "error", "message": str(exc)})
        return 2
    finally:
        if capture is not None:
            capture.close()


if __name__ == "__main__":
    sys.exit(main())
