"""Explicit-port UART transport checks for board_uart_lab.py.

Start this receiver before starting the board lab. The lab's MODULE_READY is a
synthetic protocol fixture. These checks prove UART/protocol behavior only;
they do not prove camera recognition, identity authentication or STM32 behavior.
No AUTH_SUCCESS message is fabricated or transmitted by this client.
"""

import argparse
import json
from pathlib import Path
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from pc_uart_monitor import MonitorSession, import_pyserial, json_output, write_all_uart
from uart_protocol import (
    CMD_MODULE_READY, CMD_AUTH_SUCCESS, CMD_UNKNOWN, CMD_NO_FACE, CMD_ERROR,
    CMD_PING, CMD_GET_STATUS, CMD_PONG, CMD_STATUS, ERROR_UNKNOWN_CMD,
    ERROR_BAD_PAYLOAD, encode_frame, pack_message,
)


DEFAULT_TOTAL_TIMEOUT_SECONDS = 20.0
READY_TIMEOUT_SECONDS = 5.0
RESPONSE_TIMEOUT_SECONDS = 1.5
QUIET_CHECK_SECONDS = 0.25
BAD_REQUEST_SPACING_SECONDS = 0.50
SPLIT_DELAY_SECONDS = 0.02
MAX_SPLIT_DELAY_MS = 40.0
SERIAL_READ_TIMEOUT_SECONDS = 0.025
SERIAL_WRITE_TIMEOUT_SECONDS = 0.5
UNKNOWN_TEST_COMMAND = 0xEE


class UartLinkCheck:
    """Run a bounded suite over an already-open UART; injectable clock for tests."""

    def __init__(self, port, capture, timeout=DEFAULT_TOTAL_TIMEOUT_SECONDS,
                 clock=time.monotonic, sleep=time.sleep, emit=json_output):
        self.port = port
        self.capture = capture
        self.clock = clock
        self.sleep = sleep
        self.emit = emit
        self.started = clock()
        self.deadline = self.started + timeout
        self.next_seq = 1
        self.records = []
        self.frames = []
        self.steps = []
        self.session = MonitorSession(self._record, mode="real_uart_transport_lab")

    def _record(self, record):
        self.records.append(record)
        if record["record"] == "frame":
            self.frames.append(record)
        self.emit(record)

    def _now_ms(self):
        return int((self.clock() - self.started) * 1000)

    def _remaining(self):
        return max(0.0, self.deadline - self.clock())

    def _pump(self):
        if not self._remaining():
            return
        data = self.port.read(256)
        now_ms = self._now_ms()
        if data:
            self.capture.write(data)
            self.capture.flush()
            self.session.feed(data, now_ms)
        self.session.poll(now_ms)

    def _wait(self, predicate, seconds=RESPONSE_TIMEOUT_SECONDS):
        end = min(self.deadline, self.clock() + seconds)
        while self.clock() < end:
            if predicate():
                return True
            self._pump()
        return bool(predicate())

    def _quiet(self, seconds=QUIET_CHECK_SECONDS):
        started = self.clock()
        end = min(self.deadline, started + seconds)
        while self.clock() < end:
            self._pump()
        return self.clock() - started

    def _pause(self, seconds):
        if self._remaining() < seconds:
            raise TimeoutError("Global UART check deadline reached")
        self.sleep(seconds)

    def _send(self, cmd, data=b""):
        if not self._remaining():
            raise TimeoutError("Global UART check deadline reached")
        seq = self.next_seq
        self.next_seq = (self.next_seq + 1) & 0xFFFF
        wire = encode_frame(cmd, data, seq=seq)
        write_all_uart(self.port, wire)
        self._record({"record": "lab_transmission", "cmd": cmd, "seq": seq,
                      "wire_hex": wire.hex(), "timestamp_ms": self._now_ms()})
        return seq

    def _responses(self, cmd, seq, start=0):
        return [frame for frame in self.frames[start:]
                if frame["cmd"] == cmd and frame["seq"] == seq and
                frame.get("message") is not None]

    def _step(self, name, passed, **details):
        step = {"name": name, "passed": bool(passed), "timestamp_ms": self._now_ms()}
        step.update(details)
        self.steps.append(step)
        self._record(dict(step, record="lab_step"))
        return bool(passed)

    def run(self):
        ready_found = self._wait(
            lambda: any(frame["cmd"] == CMD_MODULE_READY and frame.get("message")
                        for frame in self.frames), READY_TIMEOUT_SECONDS)
        ready = [frame["message"] for frame in self.frames
                 if frame["cmd"] == CMD_MODULE_READY and frame.get("message")]
        if not self._step("module_ready_fixture_received", ready_found,
                          fixture=ready[0] if ready else None,
                          note="Synthetic board-lab fixture; not proof of AI initialization"):
            return self.result()

        mark = len(self.frames)
        seq = self._send(CMD_PING)
        passed = self._wait(lambda: bool(self._responses(CMD_PONG, seq, mark)))
        self._step("ping_sequence_echo", passed, request_seq=seq)

        mark = len(self.frames)
        seq = self._send(CMD_GET_STATUS)
        passed = self._wait(lambda: bool(self._responses(CMD_STATUS, seq, mark)))
        responses = self._responses(CMD_STATUS, seq, mark)
        status = responses[0]["message"] if responses else None
        clean_status = bool(status and status["last_event"] == 0 and
                            status["last_user_id"] == 0 and status["last_error"] == 0)
        self._step("status_sequence_echo_no_identity", passed and clean_status,
                   request_seq=seq, status=status)

        mark = len(self.frames)
        seq = self.next_seq
        self.next_seq = (self.next_seq + 1) & 0xFFFF
        wire = encode_frame(CMD_PING, seq=seq)
        write_all_uart(self.port, wire[:3])
        split_start = self.clock()
        self._pause(SPLIT_DELAY_SECONDS)
        measured_gap_ms = (self.clock() - split_start) * 1000
        write_all_uart(self.port, wire[3:])
        passed = self._wait(lambda: bool(self._responses(CMD_PONG, seq, mark)))
        self._step("split_ping", passed and measured_gap_ms <= MAX_SPLIT_DELAY_MS,
                   request_seq=seq, split_after_bytes=3, measured_gap_ms=measured_gap_ms)

        mark = len(self.frames)
        burst = []
        expected = []
        for cmd, response in ((CMD_PING, CMD_PONG), (CMD_GET_STATUS, CMD_STATUS),
                              (CMD_PING, CMD_PONG)):
            seq = self.next_seq
            self.next_seq = (self.next_seq + 1) & 0xFFFF
            burst.append(encode_frame(cmd, seq=seq))
            expected.append((response, seq))
        write_all_uart(self.port, b"".join(burst))
        passed = self._wait(lambda: all(self._responses(cmd, seq, mark)
                                        for cmd, seq in expected))
        self._step("continuous_ping_status_burst", passed,
                   expected_responses=[{"cmd": cmd, "seq": seq} for cmd, seq in expected])

        mark = len(self.frames)
        bad_seq, valid_seq = self.next_seq, (self.next_seq + 1) & 0xFFFF
        self.next_seq = (self.next_seq + 2) & 0xFFFF
        bad = bytearray(encode_frame(CMD_PING, seq=bad_seq))
        bad[-1] ^= 0x01
        write_all_uart(self.port, bytes(bad) + encode_frame(CMD_PING, seq=valid_seq))
        found_valid = self._wait(lambda: bool(self._responses(CMD_PONG, valid_seq, mark)))
        quiet_seconds = self._quiet()
        bad_replies = [frame for frame in self.frames[mark:]
                       if frame["seq"] == bad_seq and frame["cmd"] in (CMD_PONG, CMD_ERROR)]
        valid_replies = self._responses(CMD_PONG, valid_seq, mark)
        self._step("bad_crc_then_valid_ping", found_valid and not bad_replies and
                   len(valid_replies) == 1 and quiet_seconds + 1e-9 >= QUIET_CHECK_SECONDS,
                   bad_request_seq=bad_seq, valid_request_seq=valid_seq,
                   bad_reply_count=len(bad_replies), valid_pong_count=len(valid_replies),
                   observed_quiet_seconds=quiet_seconds)

        mark = len(self.frames)
        seq = self._send(UNKNOWN_TEST_COMMAND)
        found = self._wait(lambda: bool(self._responses(CMD_ERROR, seq, mark)))
        errors = self._responses(CMD_ERROR, seq, mark)
        expected_error = bool(errors and errors[0]["message"]["error_code"] == ERROR_UNKNOWN_CMD and
                              errors[0]["message"]["detail"] == UNKNOWN_TEST_COMMAND)
        self._step("unknown_command_error", found and expected_error, request_seq=seq,
                   received=errors[0]["message"] if errors else None)

        # Board request-error throttling is 500 ms, so space two invalid queries.
        self._pause(BAD_REQUEST_SPACING_SECONDS)
        mark = len(self.frames)
        seq = self._send(CMD_PING, b"\x01")
        found = self._wait(lambda: bool(self._responses(CMD_ERROR, seq, mark)))
        errors = self._responses(CMD_ERROR, seq, mark)
        expected_error = bool(errors and errors[0]["message"]["error_code"] == ERROR_BAD_PAYLOAD and
                              errors[0]["message"]["detail"] == CMD_PING)
        self._step("malformed_ping_payload_error", found and expected_error, request_seq=seq,
                   received=errors[0]["message"] if errors else None)

        self._pause(BAD_REQUEST_SPACING_SECONDS)
        mark = len(self.frames)
        seq = self._send(CMD_ERROR, pack_message(CMD_ERROR, error_code=1, detail=0))
        quiet_seconds = self._quiet()
        reflected = [frame for frame in self.frames[mark:]
                     if frame["cmd"] == CMD_ERROR or
                     (frame["seq"] == seq and frame["cmd"] in (CMD_PONG, CMD_STATUS))]
        self._step("peer_error_is_not_echoed", not reflected and
                   quiet_seconds + 1e-9 >= QUIET_CHECK_SECONDS, peer_error_seq=seq,
                   requested_quiet_seconds=QUIET_CHECK_SECONDS,
                   observed_quiet_seconds=quiet_seconds, reflected_count=len(reflected))

        mark = len(self.frames)
        seq = self._send(CMD_PING)
        ping_ok = self._wait(lambda: bool(self._responses(CMD_PONG, seq, mark)))
        status_seq = self._send(CMD_GET_STATUS)
        status_ok = self._wait(lambda: bool(self._responses(CMD_STATUS, status_seq, mark)))
        self._step("valid_queries_still_work_after_invalid_inputs", ping_ok and status_ok,
                   ping_seq=seq, status_seq=status_seq)

        identity_frames = [frame for frame in self.frames
                           if frame["cmd"] in (CMD_AUTH_SUCCESS, CMD_UNKNOWN, CMD_NO_FACE)]
        self._step("lab_did_not_report_identity_observations", not identity_frames,
                   identity_frame_count=len(identity_frames))
        return self.result()

    def result(self):
        return {
            "scope": "real_uart_transport_only",
            "ai_camera_test": False,
            "stm32_test": False,
            "synthetic_module_ready_fixture": True,
            "passed": bool(self.steps) and all(step["passed"] for step in self.steps),
            "elapsed_ms": self._now_ms(),
            "steps": self.steps,
            "received_frames": self.frames,
            "parser_stats": dict(self.session.parser.stats),
            "payload_errors": self.session.payload_errors,
        }


def positive_float(value):
    number = float(value)
    if not 0 < number <= 60:
        raise argparse.ArgumentTypeError("must be in (0, 60] seconds")
    return number


def _json_default(value):
    if isinstance(value, (bytes, bytearray, memoryview)):
        return bytes(value).hex()
    raise TypeError(type(value).__name__)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, help="Explicit physical UART port, for example COM9")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=positive_float, default=DEFAULT_TOTAL_TIMEOUT_SECONDS)
    parser.add_argument("--results", type=Path, required=True, help="NEW JSON result file; no overwrite")
    parser.add_argument("--capture", type=Path, required=True, help="NEW raw RX binary file; no overwrite")
    args = parser.parse_args(argv)
    if args.baud <= 0:
        parser.error("--baud must be positive")
    if args.results.resolve() == args.capture.resolve():
        parser.error("--results and --capture must be distinct")
    if args.results.exists() or args.capture.exists():
        parser.error("Result/capture path already exists; choose NEW paths")
    result = {"scope": "real_uart_transport_only", "passed": False,
              "ai_camera_test": False, "stm32_test": False,
              "synthetic_module_ready_fixture": True, "port": args.port,
              "baud": args.baud, "executed": False}
    checker = None
    try:
        serial = import_pyserial()
        args.results.parent.mkdir(parents=True, exist_ok=True)
        args.capture.parent.mkdir(parents=True, exist_ok=True)
        with args.results.open("x", encoding="utf-8") as result_file, args.capture.open("xb") as capture:
            try:
                with serial.Serial(port=args.port, baudrate=args.baud, bytesize=8, parity="N",
                                   stopbits=1, timeout=SERIAL_READ_TIMEOUT_SECONDS,
                                   write_timeout=SERIAL_WRITE_TIMEOUT_SECONDS,
                                   xonxoff=False, rtscts=False, dsrdtr=False) as port:
                    result["executed"] = True
                    checker = UartLinkCheck(port, capture, timeout=args.timeout)
                    result.update(checker.run())
            except (OSError, RuntimeError, ValueError, TimeoutError, KeyboardInterrupt) as exc:
                if checker is not None:
                    result.update(checker.result())
                result["passed"] = False
                result["error"] = type(exc).__name__ + ": " + str(exc)
            finally:
                if checker is not None:
                    checker.session.finish(checker._now_ms())
                    result["parser_stats"] = dict(checker.session.parser.stats)
                json.dump(result, result_file, ensure_ascii=False, indent=2, default=_json_default)
                result_file.write("\n")
        json_output({"record": "lab_result", **result,
                     "results_file": str(args.results.resolve()),
                     "capture_file": str(args.capture.resolve())})
        return 0 if result["passed"] else 1
    except (OSError, RuntimeError, ValueError) as exc:
        json_output({"record": "lab_error", "message": str(exc), "passed": False,
                     "scope": "real_uart_transport_only"})
        return 2


if __name__ == "__main__":
    sys.exit(main())
