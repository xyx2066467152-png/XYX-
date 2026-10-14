"""Scored-model event conversion, configuration and wrapping-clock checks."""
import importlib.util
import os
import sys
import types
import unittest
from unittest.mock import patch

try:
    import numpy as host_numpy
except ImportError:
    host_numpy = None

from clock_adapter import MonotonicClock
from config import CONFIG, validate_config
from events import AUTH_SUCCESS, ERROR, NO_FACE, UNKNOWN
from identity_result import result_event


class IdentityResultTests(unittest.TestCase):
    def test_default_threshold_and_explicit_serialization_mapping(self):
        self.assertEqual(CONFIG['face_threshold'], 0.75)
        self.assertEqual(CONFIG['user_ids']['USER01'], 1)
        validate_config(dict(CONFIG))
        self.assertEqual(result_event(1, [('USER01', 0.75)], CONFIG['user_ids'], 0.75), {
            'type': AUTH_SUCCESS, 'user_id': 1, 'similarity': 0.75,
            'reason': 0, 'error_code': 0, 'detail': 0,
        })

    def test_threshold_boundary_matches_official_greater_or_equal(self):
        ids = {'USER01': 1}
        rejected = result_event(1, [('USER01', 0.749999)], ids, 0.75)
        self.assertEqual(rejected['type'], UNKNOWN)
        self.assertEqual(rejected['similarity'], 0.749999)
        for score in (0.75, 0.750001, 1.0):
            with self.subTest(score=score):
                self.assertEqual(result_event(1, [('USER01', score)], ids, 0.75)['type'], AUTH_SUCCESS)

    def test_no_face_multiple_faces_and_unmapped_names(self):
        ids = {'USER01': 1}
        self.assertEqual(result_event(0, [], ids, 0.75)['type'], NO_FACE)
        multiple = result_event(2, [('USER01', 0.99), ('USER01', 0.99)], ids, 0.75)
        self.assertEqual((multiple['type'], multiple['reason']), (UNKNOWN, 2))
        unmatched = result_event(1, [(None, 0.95)], ids, 0.75)
        self.assertEqual((unmatched['type'], unmatched['reason']), (UNKNOWN, 1))
        self.assertEqual(unmatched['similarity'], 0.95)
        unmapped = result_event(1, [('not_serialized', 0.95)], ids, 0.75)
        self.assertEqual((unmapped['type'], unmapped['reason']), (UNKNOWN, 3))
        self.assertEqual(unmapped['similarity'], 0.95)

    def test_inconsistent_or_nonfinite_model_output_is_error(self):
        ids = {'USER01': 1}
        self.assertEqual(result_event(1, [], ids, 0.75)['type'], ERROR)
        for score in (float('nan'), float('inf'), -0.1, 1.1, True, '0.9'):
            with self.subTest(score=score):
                self.assertEqual(result_event(1, [('USER01', score)], ids, 0.75)['type'], ERROR)
        for face_count in (-1, True, 1.0):
            with self.subTest(face_count=face_count), self.assertRaises(ValueError):
                result_event(face_count, [], ids, 0.75)

    def test_mapping_and_uart_profile_are_not_permissions_or_doors(self):
        config = dict(CONFIG)
        config['user_ids'] = {'USER01': 1, 'second': 1}
        with self.assertRaises(ValueError):
            validate_config(config)
        config['user_ids'] = {'USER01': 0}
        with self.assertRaises(ValueError):
            validate_config(config)
        config['user_ids'] = {'USER01': 1}
        config['uart_tx_pin'] = 41
        with self.assertRaises(ValueError):
            validate_config(config)
        for key in CONFIG:
            self.assertNotIn(key, ('permissions', 'unlock', 'servo', 'oled', 'rfid'))

    def test_default_uart2_profile_matches_selected_integration_wiring(self):
        self.assertEqual((CONFIG['uart_id'], CONFIG['uart_tx_pin'], CONFIG['uart_rx_pin']),
                         (2, 44, 45))
        self.assertEqual(CONFIG['uart_baudrate'], 115200)
        self.assertEqual(CONFIG['face_threshold'], 0.75)
        validate_config(dict(CONFIG))

    def test_uart2_swapped_tx_rx_is_rejected_before_hardware_startup(self):
        config = dict(CONFIG)
        config['uart_tx_pin'], config['uart_rx_pin'] = 45, 44
        with self.assertRaises(ValueError):
            validate_config(config)

    def test_wire_configuration_is_rejected_before_hardware_startup(self):
        invalid = (
            ('api_major', 256), ('api_minor', -1), ('api_major', True),
            ('feature_dimension', 65536), ('uart_id', True),
        )
        for field, value in invalid:
            with self.subTest(field=field, value=value), self.assertRaises(ValueError):
                config = dict(CONFIG)
                config[field] = value
                validate_config(config)


class FakeTicks:
    def __init__(self, tick=900, period=1024):
        self.tick = tick
        self.period = period

    def ticks_ms(self):
        return self.tick

    def ticks_diff(self, later, earlier):
        half = self.period // 2
        return ((later - earlier + half) % self.period) - half

    def advance(self, amount):
        self.tick = (self.tick + amount) % self.period


class ClockTests(unittest.TestCase):
    def test_wrapping_hardware_clock_extends_to_monotonic_elapsed(self):
        ticks = FakeTicks()
        clock = MonotonicClock(ticks)
        self.assertEqual(clock.now_ms(), 0)
        for expected in (200, 400, 600, 800, 1000, 1200):
            ticks.advance(200)
            self.assertEqual(clock.now_ms(), expected)

    def test_half_period_gap_is_rejected_without_mutating_elapsed(self):
        ticks = FakeTicks()
        clock = MonotonicClock(ticks)
        ticks.advance(100)
        self.assertEqual(clock.now_ms(), 100)
        ticks.advance(512)
        with self.assertRaises(RuntimeError):
            clock.now_ms()
        self.assertEqual(clock.elapsed, 100)


@unittest.skipIf(host_numpy is None, 'optional host NumPy is unavailable')
class AIMathTests(unittest.TestCase):
    """Exercise unchanged shifted-cosine math without importing board drivers.

    This checks host-side arithmetic only. It does not validate ulab buffers,
    KPU inference, model compatibility, or camera behavior.
    """
    def setUp(self):
        vendor = types.ModuleType('vendor_face_models')
        vendor.FaceDetApp = object
        vendor.FaceRegistrationApp = object
        ulab = types.ModuleType('ulab')
        ulab.numpy = host_numpy
        filename = os.path.join(os.path.dirname(os.path.dirname(__file__)), 'ai_adapter.py')
        specification = importlib.util.spec_from_file_location('_ai_math_test', filename)
        adapter = importlib.util.module_from_spec(specification)
        with patch.dict(sys.modules, {'ulab': ulab, 'ulab.numpy': host_numpy,
                                     'vendor_face_models': vendor}):
            specification.loader.exec_module(adapter)
        self.recognizer = object.__new__(adapter.FaceRecognizer)
        self.recognizer.config = dict(CONFIG)
        enrolled = host_numpy.zeros(CONFIG['feature_dimension'])
        enrolled[0] = 1.0
        self.recognizer.database = [('USER01', enrolled)]

    def test_official_normalized_shifted_cosine_endpoints(self):
        for first, second, expected in ((1.0, 0.0, 1.0), (0.0, 1.0, 0.5),
                                         (-1.0, 0.0, 0.0), (0.5, 0.75**0.5, 0.75)):
            with self.subTest(expected=expected):
                vector = host_numpy.zeros(CONFIG['feature_dimension'])
                vector[0], vector[1] = first, second
                name, score = self.recognizer._match(vector)
                self.assertAlmostEqual(score, expected, places=12)
                event = result_event(1, [(name, score)], CONFIG['user_ids'], 0.75)
                self.assertEqual(event['type'], AUTH_SUCCESS if expected >= 0.75 else UNKNOWN)

    def test_invalid_model_features_cannot_produce_identity(self):
        invalid = [host_numpy.zeros(CONFIG['feature_dimension']),
                   host_numpy.ones(CONFIG['feature_dimension'] - 1),
                   host_numpy.full(CONFIG['feature_dimension'], float('nan')),
                   host_numpy.full(CONFIG['feature_dimension'], float('inf'))]
        for feature in invalid:
            with self.subTest(feature=feature[0]), self.assertRaises(ValueError):
                self.recognizer._match(feature)

    def test_close_deinitializes_both_models_despite_one_failure(self):
        calls = []

        class Model:
            def __init__(self, name, fail=False):
                self.name, self.fail = name, fail

            def deinit(self):
                calls.append(self.name)
                if self.fail:
                    raise RuntimeError('model deinit failed')

        self.recognizer.face_reg = Model('registration', True)
        self.recognizer.face_det = Model('detection')
        self.assertEqual(self.recognizer.close(), ['model deinit failed'])
        self.assertEqual(calls, ['registration', 'detection'])
        self.assertEqual(self.recognizer.database, [])
        self.assertEqual(self.recognizer.close(), [])


if __name__ == '__main__':
    unittest.main()
