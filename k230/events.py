"""Identity observations shared by the AI adapter and confirmation filter.

This module has no hardware, UART, database, or console dependencies.  Values
are plain dictionaries so they also work in the board's MicroPython runtime.
"""

NO_FACE = "NO_FACE"
UNKNOWN = "UNKNOWN"
AUTH_SUCCESS = "AUTH_SUCCESS"
ERROR = "ERROR"

REASON_UNMATCHED = 1
REASON_MULTIPLE_FACES = 2
REASON_UNMAPPED_IDENTITY = 3

_EVENT_TYPES = (NO_FACE, UNKNOWN, AUTH_SUCCESS, ERROR)
_UNKNOWN_REASONS = (
    REASON_UNMATCHED,
    REASON_MULTIPLE_FACES,
    REASON_UNMAPPED_IDENTITY,
)
_EVENT_KEYS = ("type", "user_id", "similarity", "reason", "error_code", "detail")


def _integer(value, name, minimum, maximum):
    if isinstance(value, bool) or not isinstance(value, int):
        raise ValueError(name + " must be an integer")
    if value < minimum or value > maximum:
        raise ValueError(name + " is outside its allowed range")
    return value


def make_event(event_type, user_id=0, similarity=0.0, reason=None,
               error_code=0, detail=0):
    """Build a validated observation; similarity is normalized to 0..1.

    AUTH_SUCCESS means that the recognition model matched a configured
    identity. It does not mean that a controller should grant access.
    """
    if event_type not in _EVENT_TYPES:
        raise ValueError("unknown event type")
    user_id = _integer(user_id, "user_id", 0, 65535)
    error_code = _integer(error_code, "error_code", 0, 255)
    detail = _integer(detail, "detail", 0, 65535)
    if (isinstance(similarity, bool)
            or not isinstance(similarity, (int, float))
            or not 0.0 <= similarity <= 1.0):
        # The comparison also rejects NaN and infinity on MicroPython.
        raise ValueError("similarity must be finite and between 0 and 1")
    similarity = float(similarity)
    if reason is None:
        reason = REASON_UNMATCHED if event_type == UNKNOWN else 0
    reason = _integer(reason, "reason", 0, 255)

    if event_type == AUTH_SUCCESS:
        if user_id == 0 or reason != 0 or error_code != 0 or detail != 0:
            raise ValueError("invalid AUTH_SUCCESS fields")
    elif event_type == UNKNOWN:
        if (user_id != 0 or reason not in _UNKNOWN_REASONS
                or error_code != 0 or detail != 0):
            raise ValueError("invalid UNKNOWN fields")
    elif event_type == NO_FACE:
        if any((user_id, similarity, reason, error_code, detail)):
            raise ValueError("NO_FACE must not contain identity or error data")
    elif (user_id != 0 or similarity != 0.0 or reason != 0
          or error_code == 0):
        raise ValueError("invalid ERROR fields")

    return {
        "type": event_type,
        "user_id": user_id,
        "similarity": similarity,
        "reason": reason,
        "error_code": error_code,
        "detail": detail,
    }


def validate_event(event):
    """Validate and copy an event, preventing later caller mutation."""
    if not isinstance(event, dict):
        raise ValueError("event must be a dictionary")
    if len(event) != len(_EVENT_KEYS):
        raise ValueError("event must have exactly the documented fields")
    for key in _EVENT_KEYS:
        if key not in event:
            raise ValueError("event field missing: " + key)
    return make_event(
        event["type"], event["user_id"], event["similarity"],
        event["reason"], event["error_code"], event["detail"],
    )
