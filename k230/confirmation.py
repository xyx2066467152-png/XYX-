"""Confirm identity observations and suppress repeated presence reports.

The caller supplies a centralized configuration and an elapsed monotonic
millisecond clock. No UART or hardware work happens here. This is an identity
report filter, not an access-control state machine.
"""

from events import AUTH_SUCCESS, ERROR, NO_FACE, UNKNOWN, make_event, validate_event

_COUNT_KEYS = ("success_frames", "unknown_frames", "leave_frames")
_TIME_KEYS = (
    "success_min_ms", "unknown_min_ms", "leave_min_ms", "cooldown_ms",
    "max_frame_gap_ms", "error_repeat_ms",
)


def _validated_config(config):
    if not isinstance(config, dict):
        raise ValueError("confirmation config must be a dictionary")
    expected = _COUNT_KEYS + _TIME_KEYS
    if len(config) != len(expected):
        raise ValueError("confirmation config has missing or unknown keys")
    result = {}
    for key in expected:
        if key not in config:
            raise ValueError("confirmation parameter missing: " + key)
        value = config[key]
        if isinstance(value, bool) or not isinstance(value, int):
            raise ValueError(key + " must be an integer")
        minimum = 1 if key in _COUNT_KEYS or key == "max_frame_gap_ms" else 0
        if value < minimum:
            raise ValueError(key + " is below its allowed minimum")
        result[key] = value
    return result


class ConfirmationFilter:
    """Turn raw observations into stable, bounded-rate identity events.

    A presence episode permits at most one AUTH_SUCCESS. UNKNOWN can be
    reported first and then upgraded to AUTH_SUCCESS. Only confirmed NO_FACE
    re-arms authentication. Errors, inconsistent identities, and multiple
    faces never re-arm an episode. Cooldown survives departure and therefore
    limits successive episodes as well.
    """

    def __init__(self, config):
        self.config = _validated_config(config)
        self._last_update_ms = None
        self._last_success_ms = None
        self._last_error_ms = {}
        self._success_latched = False
        self._unknown_reported = False
        self._no_face_reported = False
        self._reset_candidate()
        self._reset_absence()

    def _reset_candidate(self):
        self._candidate_key = None
        self._candidate_start_ms = None
        self._candidate_frames = 0

    def _reset_absence(self):
        self._absence_start_ms = None
        self._absence_frames = 0

    def _observe_candidate(self, key, now_ms, required_frames, minimum_ms):
        if self._candidate_key != key:
            self._candidate_key = key
            self._candidate_start_ms = now_ms
            self._candidate_frames = 1
        else:
            self._candidate_frames = min(required_frames, self._candidate_frames + 1)
        return (self._candidate_frames >= required_frames
                and now_ms - self._candidate_start_ms >= minimum_ms)

    def update(self, event, now_ms):
        """Return a list of confirmed events, usually empty or one item.

        now_ms must be a nonnegative, nondecreasing integer elapsed clock.
        The board clock adapter must accumulate ticks_diff() deltas before
        calling this method; raw wrapping ticks_ms() values are unsuitable.
        Invalid events or backward timestamps raise before state is changed.
        """
        observation = validate_event(event)
        if (isinstance(now_ms, bool) or not isinstance(now_ms, int)
                or now_ms < 0):
            raise ValueError("now_ms must be a nonnegative integer")
        if self._last_update_ms is not None and now_ms < self._last_update_ms:
            raise ValueError("now_ms must not move backward")
        if (self._last_update_ms is not None
                and now_ms - self._last_update_ms > self.config["max_frame_gap_ms"]):
            # No inferred observation fills a gap; confirmation starts again.
            self._reset_candidate()
            self._reset_absence()
        self._last_update_ms = now_ms
        event_type = observation["type"]

        if event_type == ERROR:
            self._reset_candidate()
            self._reset_absence()
            code = observation["error_code"]
            previous = self._last_error_ms.get(code)
            if (previous is None
                    or now_ms - previous >= self.config["error_repeat_ms"]):
                self._last_error_ms[code] = now_ms
                return [observation]
            return []

        if event_type == NO_FACE:
            self._reset_candidate()
            if self._absence_start_ms is None:
                self._absence_start_ms = now_ms
                self._absence_frames = 1
            else:
                self._absence_frames = min(
                    self.config["leave_frames"], self._absence_frames + 1,
                )
            if (not self._no_face_reported
                    and self._absence_frames >= self.config["leave_frames"]
                    and now_ms - self._absence_start_ms >= self.config["leave_min_ms"]):
                self._success_latched = False
                self._unknown_reported = False
                self._no_face_reported = True
                return [make_event(NO_FACE)]
            return []

        self._reset_absence()
        self._no_face_reported = False
        if self._success_latched:
            self._reset_candidate()
            return []

        if event_type == AUTH_SUCCESS:
            stable = self._observe_candidate(
                (AUTH_SUCCESS, observation["user_id"]), now_ms,
                self.config["success_frames"], self.config["success_min_ms"],
            )
            cooled_down = (self._last_success_ms is None
                          or now_ms - self._last_success_ms >= self.config["cooldown_ms"])
            if stable and cooled_down:
                self._success_latched = True
                self._last_success_ms = now_ms
                self._reset_candidate()
                return [observation]
            return []

        if self._unknown_reported:
            self._reset_candidate()
            return []
        stable = self._observe_candidate(
            (UNKNOWN, observation["reason"]), now_ms,
            self.config["unknown_frames"], self.config["unknown_min_ms"],
        )
        if stable:
            self._unknown_reported = True
            self._reset_candidate()
            return [observation]
        return []
