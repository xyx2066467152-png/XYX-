"""Identity + filter + UART integration, using fake peripherals only."""
import os
import sys
import types
import unittest
from unittest.mock import patch

import clock_adapter
import hardware_uart
import run_auth
from config import CONFIG
from events import AUTH_SUCCESS, ERROR, NO_FACE, UNKNOWN, make_event
from module_runtime import IdentityModule, STATE_ERROR, STATE_RUNNING, STATE_STARTING
from uart_protocol import (
    CMD_AUTH_SUCCESS, CMD_ERROR, CMD_GET_STATUS, CMD_MODULE_READY,
    CMD_NO_FACE, CMD_PING, CMD_PONG, CMD_STATUS, CMD_UNKNOWN,
    ERROR_AI, ERROR_BAD_PAYLOAD, ERROR_CAMERA, ERROR_CLEANUP, ERROR_CONFIG, ERROR_DATABASE,
    ERROR_MODEL, ERROR_UNKNOWN_CMD, ERROR_UART,
    FrameParser, decode_message, encode_frame,
)


class FakeUART:
    def __init__(self):
        self.incoming = bytearray()
        self.outgoing = bytearray()
        self.read_calls = 0
        self.fail_write = False
        self.deinitialized = False

    def inject(self, data):
        self.incoming.extend(data)

    def read(self, size):
        self.read_calls += 1
        if not self.incoming:
            return None
        data = bytes(self.incoming[:size])
        del self.incoming[:size]
        return data

    def write(self, data):
        if self.fail_write:
            raise OSError('test UART failure')
        self.outgoing.extend(data)
        return len(data)

    def deinit(self):
        self.deinitialized = True

    def messages(self):
        output = [decode_message(frame) for frame in FrameParser().feed(self.outgoing, 0)]
        self.outgoing = bytearray()
        return output


class ModuleIntegrationTests(unittest.TestCase):
    def setUp(self):
        self.uart = FakeUART()
        self.module = IdentityModule(self.uart, dict(CONFIG))

    def observe(self, event, times):
        for now in times:
            self.module.observe(event, now)

    def request_status(self, now, seq=42):
        self.uart.inject(encode_frame(CMD_GET_STATUS, seq=seq))
        self.module.poll(now)
        return self.uart.messages()[-1]

    def test_ready_ping_status_and_reply_sequence(self):
        self.module.ready()
        ready = self.uart.messages()
        self.assertEqual(len(ready), 1)
        self.assertEqual(ready[0]['name'], 'MODULE_READY')
        self.assertEqual(ready[0]['threshold'], 0.75)
        self.assertEqual(ready[0]['feature_dimension'], 512)
        self.uart.inject(encode_frame(CMD_PING, seq=65535))
        self.uart.inject(encode_frame(CMD_GET_STATUS, seq=9))
        self.assertEqual(self.module.poll(0), 2)
        messages = self.uart.messages()
        self.assertEqual([(x['cmd'], x['seq']) for x in messages], [(CMD_PONG, 65535), (CMD_STATUS, 9)])
        self.assertEqual(messages[1]['last_user_id'], 0)

    def test_same_person_departure_reentry_and_status(self):
        self.module.ready()
        self.uart.messages()
        event = make_event(AUTH_SUCCESS, 1, 0.91)
        self.observe(event, (0, 75, 150, 500, 900, 1500, 2100))
        first = self.uart.messages()
        self.assertEqual([x['name'] for x in first], ['AUTH_SUCCESS'])
        self.assertEqual(first[0]['user_id'], 1)
        self.assertEqual(first[0]['similarity_q'], 9100)
        departure_start = 2200
        departure_end = departure_start + CONFIG['leave_min_ms']
        self.observe(make_event(NO_FACE), range(departure_start, departure_end + 1, 100))
        departure = self.uart.messages()
        self.assertEqual([x['name'] for x in departure], ['NO_FACE'])
        status = self.request_status(departure_end + 100)
        self.assertEqual((status['last_event'], status['last_user_id'], status['last_similarity_q']), (1, 0, 0))
        reentry = departure_end + 200
        self.observe(event, (reentry, reentry + 75, reentry + 150))
        self.assertEqual([x['name'] for x in self.uart.messages()], ['AUTH_SUCCESS'])

    def test_unknown_is_reported_then_can_upgrade_without_departure(self):
        self.module.ready()
        self.uart.messages()
        self.observe(make_event(UNKNOWN), (0, 75, 150, 250, 350))
        self.observe(make_event(AUTH_SUCCESS, 1, 0.90), (400, 475, 550))
        self.assertEqual([x['cmd'] for x in self.uart.messages()], [CMD_UNKNOWN, CMD_AUTH_SUCCESS])

    def test_bad_requests_throttle_and_known_wrong_direction_is_dropped(self):
        self.module.ready()
        self.uart.messages()
        self.uart.inject(encode_frame(0xEE, seq=7))
        self.uart.inject(encode_frame(CMD_PING, b'bad', seq=8))
        self.module.poll(0)
        first = self.uart.messages()
        self.assertEqual([(x['seq'], x['error_code']) for x in first], [(7, ERROR_UNKNOWN_CMD)])
        self.assertEqual(self.module.dropped_requests, 1)
        self.uart.inject(encode_frame(CMD_PING, b'bad', seq=9))
        self.module.poll(500)
        self.assertEqual(self.uart.messages()[0]['error_code'], ERROR_BAD_PAYLOAD)
        self.uart.inject(encode_frame(CMD_AUTH_SUCCESS, b'\x01\x00\x28\x23', seq=10))
        self.module.poll(1000)
        self.assertEqual(self.uart.messages(), [])
        self.assertEqual(self.module.dropped_requests, 2)
        self.assertEqual(self.module.state, 1)

    def test_reflected_module_frames_never_create_error_reply_loops(self):
        self.module.ready()
        self.uart.messages()
        outbound_commands = (CMD_MODULE_READY, CMD_AUTH_SUCCESS, CMD_UNKNOWN,
                             CMD_NO_FACE, CMD_ERROR, CMD_PONG, CMD_STATUS)
        # Every payload is deliberately wrong; direction is checked first.
        self.uart.inject(b''.join(encode_frame(cmd, b'bad', seq=index)
                                 for index, cmd in enumerate(outbound_commands)))
        self.module.poll(0)
        self.assertEqual(self.uart.messages(), [])
        self.assertEqual(self.module.dropped_requests, len(outbound_commands))
        self.uart.inject(encode_frame(CMD_PING, seq=99))
        self.module.poll(1)
        self.assertEqual(self.uart.messages()[0]['cmd'], CMD_PONG)

    def test_request_flood_has_bounded_reads_and_replies(self):
        self.module.ready()
        self.uart.messages()
        self.uart.inject(b''.join(encode_frame(CMD_PING, seq=i) for i in range(100)))
        self.module.poll(0)
        self.assertLessEqual(self.uart.read_calls, self.module.link.max_reads_per_poll)
        self.assertEqual(len(self.uart.messages()), CONFIG['max_requests_per_poll'])
        self.assertGreater(self.module.dropped_requests, 0)
        self.assertGreater(len(self.uart.incoming), 0)

    def test_throttled_error_status_still_records_current_error(self):
        self.module.ready()
        self.uart.messages()
        fault = make_event(ERROR, error_code=ERROR_AI)
        self.module.observe(fault, 0)
        self.uart.messages()
        self.module.observe(make_event(NO_FACE), 100)
        self.module.observe(fault, 200)
        status = self.request_status(201)
        self.assertEqual(status['state'], STATE_ERROR)
        self.assertEqual(status['last_error'], ERROR_AI)

    def test_ready_write_failure_never_marks_module_ready(self):
        self.uart.fail_write = True
        with self.assertRaises(OSError):
            self.module.ready()
        self.assertEqual(self.module.state, STATE_STARTING)
        self.assertEqual(self.uart.messages(), [])


class FakeClock:
    def __init__(self, time_module):
        self.elapsed = 0

    def now_ms(self):
        value = self.elapsed
        self.elapsed += 100
        return value


class RuntimeIntegrationTests(unittest.TestCase):
    """Execute the real entry point with injected camera/model/serial stand-ins."""

    def harness(self, observations=None, fail_stage=None):
        test = self
        raw = observations or [make_event(NO_FACE)] * 5
        self.closed = []
        self.lifecycle = []

        class DatabaseError(Exception):
            pass

        class Recognizer:
            def __init__(self, settings):
                test.lifecycle.append('model_construct')
                if fail_stage == 'database':
                    raise DatabaseError('invalid feature')
                if fail_stage == 'model':
                    raise RuntimeError('bad model')
                self.raw = iter(raw)

            def observe(self, frame):
                test.lifecycle.append('model_observe')
                if fail_stage == 'ai':
                    raise RuntimeError('inference failure')
                return next(self.raw)

            def close(self):
                test.closed.append('model')
                if fail_stage == 'model_cleanup':
                    return ['model cleanup failure']
                return []

        class Pipeline:
            def __init__(self, **kwargs):
                test.lifecycle.append('camera_construct')

            def create(self, sensor):
                test.lifecycle.append('camera_create')
                if fail_stage == 'camera_init':
                    raise RuntimeError('camera init failure')

            def get_frame(self):
                if fail_stage == 'camera_read':
                    raise RuntimeError('camera read failure')
                return object()

            def destroy(self):
                test.closed.append('camera')
                if fail_stage == 'camera_cleanup':
                    raise RuntimeError('camera cleanup failure')

        class Sensor:
            def __init__(self, **kwargs):
                test.lifecycle.append('sensor_construct')

        adapter = types.ModuleType('ai_adapter')
        adapter.FaceRecognizer = Recognizer
        adapter.DatabaseError = DatabaseError
        pipeline = types.ModuleType('libs.PipeLine')
        pipeline.PipeLine = Pipeline
        sensor = types.ModuleType('media.sensor')
        sensor.Sensor = Sensor
        presentation = types.ModuleType('presentation')
        presentation.draw_preview = lambda *args: None
        return {
            'ai_adapter': adapter, 'libs': types.ModuleType('libs'),
            'libs.PipeLine': pipeline, 'media': types.ModuleType('media'),
            'media.sensor': sensor, 'presentation': presentation,
        }

    def execute(self, *, observations=None, fail_stage=None, uart=None):
        settings = dict(CONFIG)
        settings['loop_sleep_ms'] = 0
        settings['preview_enabled'] = False
        uart = uart or FakeUART()
        with patch.dict(sys.modules, self.harness(observations, fail_stage)), \
             patch.object(os, 'exitpoint', lambda: None, create=True), \
             patch.object(clock_adapter, 'MonotonicClock', FakeClock):
            result = run_auth.run(settings, max_frames=5, uart_override=uart)
        return result, uart.messages()

    def test_models_and_camera_initialize_before_ready_and_identity(self):
        observations = [make_event(AUTH_SUCCESS, 1, 0.91)] * 5
        result, messages = self.execute(observations=observations)
        self.assertIsNone(result['failure'])
        self.assertEqual(result['frames'], 5)
        self.assertEqual([x['name'] for x in messages], ['MODULE_READY', 'AUTH_SUCCESS'])
        self.assertEqual(self.closed, ['model', 'camera'])
        # Static software call-order proof. Real media/KPU behavior is separate.
        self.assertEqual(self.lifecycle[:4], ['camera_construct', 'sensor_construct',
                                             'camera_create', 'model_construct'])
        self.assertLess(self.lifecycle.index('camera_create'),
                        self.lifecycle.index('model_construct'))

    def test_startup_failures_have_error_and_no_false_ready(self):
        for stage, code in (('database', ERROR_DATABASE), ('model', ERROR_MODEL),
                            ('camera_init', ERROR_CAMERA)):
            with self.subTest(stage=stage):
                result, messages = self.execute(fail_stage=stage)
                self.assertIsNotNone(result['failure'])
                self.assertEqual(result['frames'], 0)
                self.assertEqual([x['name'] for x in messages], ['ERROR'])
                self.assertEqual(messages[0]['error_code'], code)
                if stage == 'camera_init':
                    self.assertNotIn('model_construct', self.lifecycle)

    def test_runtime_camera_and_ai_errors_stop_inference_and_cleanup(self):
        for stage, code in (('ai', ERROR_AI), ('camera_read', ERROR_CAMERA)):
            with self.subTest(stage=stage):
                result, messages = self.execute(fail_stage=stage)
                self.assertEqual(result['frames'], 0)
                self.assertEqual([x['name'] for x in messages], ['MODULE_READY', 'ERROR'])
                self.assertEqual(messages[-1]['error_code'], code)
                self.assertEqual(self.closed, ['model', 'camera'])

    def test_uart_failure_is_recorded_without_recursive_error_send(self):
        uart = FakeUART()
        uart.fail_write = True
        result, messages = self.execute(uart=uart)
        self.assertIsNotNone(result['failure'])
        self.assertEqual(result['last_error'], ERROR_UART)
        self.assertEqual(result['state'], STATE_ERROR)
        self.assertEqual(messages, [])
        self.assertEqual(self.closed, ['model', 'camera'])

    def test_invalid_startup_config_returns_diagnostics_without_opening_uart(self):
        settings = dict(CONFIG)
        settings['success_frames'] = 0
        with patch.object(hardware_uart, 'open_uart') as opener:
            result = run_auth.run(settings, max_frames=5)
        opener.assert_not_called()
        self.assertEqual(result['frames'], 0)
        self.assertEqual(result['state'], STATE_ERROR)
        self.assertEqual(result['last_error'], ERROR_CONFIG)
        self.assertIsNotNone(result['failure'])

    def test_uart_open_failure_returns_local_error_without_false_ready(self):
        with patch.object(clock_adapter, 'MonotonicClock', FakeClock), \
             patch.object(hardware_uart, 'open_uart', side_effect=OSError('UART unavailable')):
            result = run_auth.run(dict(CONFIG), max_frames=5)
        self.assertEqual(result['frames'], 0)
        self.assertEqual(result['state'], STATE_ERROR)
        self.assertEqual(result['last_error'], ERROR_UART)
        self.assertIn('unavailable', result['failure'])

    def test_model_and_camera_cleanup_failures_are_protocol_errors(self):
        for stage in ('model_cleanup', 'camera_cleanup'):
            with self.subTest(stage=stage):
                result, messages = self.execute(fail_stage=stage)
                self.assertEqual(result['frames'], 5)
                self.assertEqual(result['last_error'], ERROR_CLEANUP)
                self.assertEqual(messages[-1]['name'], 'ERROR')
                self.assertEqual(messages[-1]['error_code'], ERROR_CLEANUP)
                self.assertEqual(self.closed, ['model', 'camera'])
                self.assertTrue(result['cleanup_errors'])

    def test_uart_deinit_failure_does_not_discard_completed_report(self):
        uart = FakeUART()
        uart.deinit = lambda: (_ for _ in ()).throw(OSError('deinit failure'))
        settings = dict(CONFIG)
        settings['preview_enabled'] = False
        settings['loop_sleep_ms'] = 0
        with patch.dict(sys.modules, self.harness()), \
             patch.object(os, 'exitpoint', lambda: None, create=True), \
             patch.object(clock_adapter, 'MonotonicClock', FakeClock), \
             patch.object(hardware_uart, 'open_uart', return_value=uart):
            result = run_auth.run(settings, max_frames=5)
        self.assertEqual(result['frames'], 5)
        self.assertEqual(result['state'], STATE_ERROR)
        self.assertEqual(result['last_error'], ERROR_CLEANUP)
        self.assertIn('deinit failure', result['cleanup_errors'])
        self.assertEqual(uart.messages()[0]['name'], 'MODULE_READY')


if __name__ == '__main__':
    unittest.main()
