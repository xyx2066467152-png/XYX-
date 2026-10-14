"""Convert scored recognition into events without parsing display strings."""
from events import make_event, NO_FACE, UNKNOWN, AUTH_SUCCESS, ERROR


def result_event(face_count, matches, user_ids, threshold):
    if type(face_count) is not int or face_count < 0:
        raise ValueError('face_count must be a nonnegative integer')
    if face_count == 0:
        return make_event(NO_FACE)
    if face_count != 1:
        return make_event(UNKNOWN, reason=2)
    if len(matches) != 1:
        return make_event(ERROR, error_code=1, detail=1)
    name, score = matches[0]
    if not isinstance(score, (int,float)) or isinstance(score,bool) or not 0 <= score <= 1:
        return make_event(ERROR, error_code=1, detail=2)
    if name is None or score < threshold:
        return make_event(UNKNOWN, reason=1)
    if name not in user_ids:
        return make_event(UNKNOWN, reason=3)
    return make_event(AUTH_SUCCESS, user_id=user_ids[name], similarity=float(score))
