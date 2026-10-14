"""Behavioral tests for identity confirmation, independent of the camera."""

import unittest

from confirmation import ConfirmationFilter
from config import CONFIG
from events import (
    AUTH_SUCCESS, ERROR, NO_FACE, UNKNOWN, REASON_MULTIPLE_FACES,
    REASON_UNMAPPED_IDENTITY, make_event, validate_event,
)


def settings(**changes):
    result = {
        "success_frames": 3,
        "success_min_ms": 150,
        "unknown_frames": 3,
        "unknown_min_ms": 150,
        "leave_frames": 3,
        "leave_min_ms": 300,
        "cooldown_ms": 2000,
        "max_frame_gap_ms": 1000,
        "error_repeat_ms": 5000,
    }
    result.update(changes)
    return result


class EventTests(unittest.TestCase):
    def test_events_have_uniform_fields_and_independent_copies(self):
        original = make_event(AUTH_SUCCESS, 1, 0.91)
        copy = validate_event(original)
        original["user_id"] = 2
        self.assertEqual(copy["user_id"], 1)
        self.assertEqual(set(copy), {
            "type", "user_id", "similarity", "reason", "error_code", "detail",
        })
        self.assertEqual(make_event(UNKNOWN)["reason"], 1)
        self.assertEqual(make_event(ERROR, error_code=255, detail=65535)["detail"], 65535)

    def test_invalid_identity_and_nonfinite_similarity_rejected(self):
        for user_id in (0, -1, 65536, True, "USER01"):
            with self.subTest(user_id=user_id), self.assertRaises(ValueError):
                make_event(AUTH_SUCCESS, user_id, 0.9)
        for similarity in (-0.01, 1.01, float("nan"), float("inf"), True, "0.9"):
            with self.subTest(similarity=similarity), self.assertRaises(ValueError):
                make_event(AUTH_SUCCESS, 1, similarity)
        self.assertEqual(make_event(AUTH_SUCCESS, 65535, 1)["similarity"], 1.0)

    def test_wrong_event_field_combinations_rejected(self):
        invalid = [
            lambda: make_event("MATCH"),
            lambda: make_event(NO_FACE, similarity=0.1),
            lambda: make_event(UNKNOWN, user_id=1),
            lambda: make_event(UNKNOWN, reason=4),
            lambda: make_event(ERROR),
            lambda: make_event(ERROR, error_code=256),
            lambda: make_event(ERROR, error_code=1, similarity=0.8),
            lambda: make_event(AUTH_SUCCESS, 1, 0.9, reason=1),
            lambda: make_event(AUTH_SUCCESS, 1, 0.9, detail=1),
        ]
        for operation in invalid:
            with self.subTest(operation=operation), self.assertRaises(ValueError):
                operation()
        with self.assertRaises(ValueError):
            validate_event({"type": NO_FACE})
        extra = make_event(NO_FACE)
        extra["face_count"] = 0
        with self.assertRaises(ValueError):
            validate_event(extra)


class ConfirmationTests(unittest.TestCase):
    def setUp(self):
        self.filter = ConfirmationFilter(settings())
        self.success = make_event(AUTH_SUCCESS, 1, 0.91)
        self.no_face = make_event(NO_FACE)

    def feed(self, event, times):
        result = []
        for timestamp in times:
            result.extend(self.filter.update(event, timestamp))
        return result

    def authenticate(self, start=0):
        return self.feed(self.success, (start, start + 75, start + 150))

    def leave(self, start):
        return self.feed(self.no_face, (start, start + 150, start + 300))

    def test_requires_frames_and_elapsed_time(self):
        self.assertEqual(self.feed(self.success, (0, 20, 40, 60)), [])
        self.assertEqual(self.filter.update(self.success, 150), [self.success])
        self.filter = ConfirmationFilter(settings())
        self.assertEqual(self.feed(self.success, (0, 150)), [])
        self.assertEqual(self.filter.update(self.success, 151), [self.success])

    def test_same_user_is_reported_once_even_after_cooldown(self):
        self.assertEqual(self.authenticate(), [self.success])
        self.assertEqual(self.feed(self.success, range(200, 10000, 200)), [])

    def test_production_profile_ignores_brief_detection_loss_but_rearms_after_departure(self):
        keys = ("success_frames", "success_min_ms", "unknown_frames",
                "unknown_min_ms", "leave_frames", "leave_min_ms",
                "cooldown_ms", "max_frame_gap_ms", "error_repeat_ms")
        filter = ConfirmationFilter({key: CONFIG[key] for key in keys})
        success = make_event(AUTH_SUCCESS, 1, 0.85)
        no_face = make_event(NO_FACE)
        self.assertEqual(filter.update(success, 0), [])
        self.assertEqual(filter.update(success, 75), [])
        self.assertEqual(filter.update(success, 150), [success])
        for timestamp in range(250, 1950, 100):
            self.assertEqual(filter.update(no_face, timestamp), [])
        for timestamp in (2000, 2100, 2200):
            self.assertEqual(filter.update(success, timestamp), [])
        departures = []
        for timestamp in range(2300, 5500, 100):
            departures.extend(filter.update(no_face, timestamp))
        self.assertEqual(departures, [no_face])
        self.assertEqual(filter.update(success, 5600), [])
        self.assertEqual(filter.update(success, 5675), [])
        self.assertEqual(filter.update(success, 5750), [success])

    def test_different_user_requires_confirmed_departure(self):
        self.authenticate()
        other = make_event(AUTH_SUCCESS, 2, 0.93)
        self.assertEqual(self.feed(other, range(200, 5000, 200)), [])

    def test_one_missed_frame_and_error_do_not_rearm_success(self):
        self.authenticate()
        self.assertEqual(self.filter.update(self.no_face, 200), [])
        fault = make_event(ERROR, error_code=1)
        self.assertEqual(self.filter.update(fault, 250), [fault])
        self.assertEqual(self.feed(self.success, (300, 400, 500, 1300, 2100, 2900)), [])
        self.assertEqual(self.feed(make_event(UNKNOWN), (3000, 3100, 3200)), [])

    def test_leave_then_same_user_can_authenticate_again(self):
        self.assertEqual(self.authenticate(), [self.success])
        self.assertEqual(self.leave(500), [self.no_face])
        self.assertEqual(self.feed(self.no_face, (900, 1000, 1100)), [])
        self.assertEqual(self.authenticate(2300), [self.success])

    def test_cooldown_waits_for_a_still_visible_confirmed_face(self):
        self.authenticate()
        self.leave(200)
        self.assertEqual(self.authenticate(600), [])
        self.assertEqual(self.feed(self.success, (1000, 1500, 2149)), [])
        self.assertEqual(self.filter.update(self.success, 2150), [self.success])

    def test_cooldown_does_not_report_a_departed_candidate(self):
        self.authenticate()
        self.leave(200)
        self.assertEqual(self.authenticate(600), [])
        self.assertEqual(self.leave(800), [self.no_face])
        self.assertEqual(self.feed(self.no_face, (1700, 2300)), [])
        self.assertEqual(self.authenticate(2400), [self.success])

    def test_alternating_identities_do_not_form_a_streak(self):
        other = make_event(AUTH_SUCCESS, 2, 0.92)
        for index in range(10):
            event = self.success if index % 2 == 0 else other
            self.assertEqual(self.filter.update(event, index * 100), [])
        self.assertEqual(self.authenticate(1000), [self.success])

    def test_unknown_can_upgrade_to_success(self):
        unknown = make_event(UNKNOWN, similarity=0.7)
        self.assertEqual(self.feed(unknown, (0, 75, 150)), [unknown])
        self.assertEqual(self.feed(unknown, (200, 300, 400)), [])
        self.assertEqual(self.authenticate(500), [self.success])
        self.assertEqual(self.feed(unknown, (700, 800, 900)), [])

    def test_multiple_faces_and_unmapped_identity_never_authenticate(self):
        multiple = make_event(UNKNOWN, reason=REASON_MULTIPLE_FACES)
        self.assertEqual(self.feed(multiple, (0, 75, 150)), [multiple])
        unmapped = make_event(UNKNOWN, similarity=0.95, reason=REASON_UNMAPPED_IDENTITY)
        self.assertEqual(self.feed(unmapped, (200, 300, 400)), [])
        self.assertEqual(self.leave(500), [self.no_face])
        self.assertEqual(self.feed(unmapped, (900, 1000, 1100)), [unmapped])

    def test_unknown_or_error_interrupts_unconfirmed_success(self):
        self.feed(self.success, (0, 75))
        self.filter.update(make_event(UNKNOWN), 100)
        self.assertEqual(self.feed(self.success, (150, 225)), [])
        fault = make_event(ERROR, error_code=3)
        self.assertEqual(self.filter.update(fault, 250), [fault])
        self.assertEqual(self.feed(self.success, (300, 375)), [])
        self.assertEqual(self.filter.update(self.success, 450), [self.success])

    def test_absence_requires_consecutive_frames_and_elapsed_time(self):
        self.authenticate()
        self.assertEqual(self.feed(self.no_face, (200, 250, 300)), [])
        self.filter.update(self.success, 350)
        self.assertEqual(self.feed(self.no_face, (400, 550)), [])
        self.assertEqual(self.filter.update(self.no_face, 700), [self.no_face])

    def test_initial_no_face_emits_once_after_confirmation(self):
        self.assertEqual(self.leave(0), [self.no_face])
        self.assertEqual(self.feed(self.no_face, (400, 500, 600)), [])

    def test_frame_gap_restarts_confirmation_without_rearming_latch(self):
        self.feed(self.success, (0, 75))
        self.assertEqual(self.feed(self.success, (2000, 2075)), [])
        self.assertEqual(self.filter.update(self.success, 2150), [self.success])
        self.assertEqual(self.feed(self.success, (1000000, 1000075, 1000150)), [])
        self.assertEqual(self.filter.update(self.no_face, 2000000), [])

    def test_error_repeat_is_throttled_per_code(self):
        first = make_event(ERROR, error_code=1, detail=1)
        different_detail = make_event(ERROR, error_code=1, detail=2)
        other = make_event(ERROR, error_code=2)
        self.assertEqual(self.filter.update(first, 0), [first])
        self.assertEqual(self.filter.update(different_detail, 100), [])
        self.assertEqual(self.filter.update(other, 200), [other])
        self.assertEqual(self.filter.update(first, 300), [])
        self.assertEqual(self.filter.update(different_detail, 5000), [different_detail])

    def test_error_throttle_can_be_disabled(self):
        self.filter = ConfirmationFilter(settings(error_repeat_ms=0))
        fault = make_event(ERROR, error_code=1)
        self.assertEqual(self.filter.update(fault, 0), [fault])
        self.assertEqual(self.filter.update(fault, 1), [fault])

    def test_invalid_input_does_not_change_streak_or_time(self):
        self.feed(self.success, (0, 75))
        for time in (-1, 74, True, 1.0):
            with self.subTest(time=time), self.assertRaises(ValueError):
                self.filter.update(self.success, time)
        with self.assertRaises(ValueError):
            self.filter.update({"type": NO_FACE}, 1000)
        self.assertEqual(self.filter.update(self.success, 150), [self.success])

    def test_config_is_validated_and_copied(self):
        original = settings()
        self.filter = ConfirmationFilter(original)
        original["success_frames"] = 1
        self.assertEqual(self.filter.update(self.success, 0), [])
        for field, value in (("success_frames", 0), ("leave_frames", True),
                             ("max_frame_gap_ms", 0), ("cooldown_ms", -1),
                             ("success_min_ms", 1.0)):
            with self.subTest(field=field), self.assertRaises(ValueError):
                ConfirmationFilter(settings(**{field: value}))
        missing = settings()
        del missing["leave_frames"]
        with self.assertRaises(ValueError):
            ConfirmationFilter(missing)
        with self.assertRaises(ValueError):
            ConfirmationFilter(settings(magic_number=3))

    def test_emitted_event_does_not_alias_the_callers_event(self):
        self.feed(self.success, (0, 75))
        reported = self.filter.update(self.success, 150)[0]
        self.success["similarity"] = 0.1
        self.assertEqual(reported["similarity"], 0.91)


if __name__ == "__main__":
    unittest.main()
