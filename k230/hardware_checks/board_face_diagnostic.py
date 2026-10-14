"""Bounded face-only diagnostic for CanMV IDE; runs from RAM, writes no TF files.

It uses the deployed V1 recognition candidate and displays its camera preview,
but replaces the physical UART with a sink. No authentication frame reaches
STM32, so this diagnostic cannot request a door opening. Read the IDE console
for raw scores, confirmed events, frame progress and the final result.
"""

import sys
import time

PROJECT_DIR = '/sdcard/project_version/identity_auth_v1'
if PROJECT_DIR not in sys.path:
    sys.path.insert(0, PROJECT_DIR)

from config import CONFIG
import run_auth
from ai_adapter import FaceRecognizer

MAX_FRAMES = 360
PRINT_EVERY_FRAMES = 20

# The currently deployed board may still have the older identity_result.py,
# which erased scores for UNKNOWN. Recover the already-computed best score in
# this RAM session without changing the matching algorithm or TF files.
_recognizer_observe = FaceRecognizer.observe


def observe_with_score(self, frame):
    result = _recognizer_observe(self, frame)
    if result['type'] == 'UNKNOWN' and len(self.last_matches) == 1:
        result['similarity'] = float(self.last_matches[0][1])
    return result


FaceRecognizer.observe = observe_with_score


class SinkUART:
    def read(self, count):
        return None

    def write(self, data):
        return len(data)


counts = {'NO_FACE': 0, 'UNKNOWN': 0, 'AUTH_SUCCESS': 0, 'ERROR': 0}
confirmed_counts = {'NO_FACE': 0, 'UNKNOWN': 0, 'AUTH_SUCCESS': 0, 'ERROR': 0}
max_unknown_score_q = 0
frames_seen = 0
started_ms = time.ticks_ms()


def observe(observation, confirmed):
    global frames_seen, max_unknown_score_q
    frames_seen += 1
    kind = observation['type']
    counts[kind] += 1
    score_q = int(observation['similarity'] * 10000 + 0.5)
    if kind == 'UNKNOWN' and score_q > max_unknown_score_q:
        max_unknown_score_q = score_q
    for event in confirmed:
        confirmed_counts[event['type']] += 1
    if confirmed or frames_seen % PRINT_EVERY_FRAMES == 0:
        elapsed = time.ticks_diff(time.ticks_ms(), started_ms)
        print('FACE_DIAG frame={} ms={} raw={} reason={} score_q={} confirmed={}'.format(
            frames_seen, elapsed, kind, observation['reason'], score_q,
            ','.join(event['type'] for event in confirmed) or '-'))


print('FACE_DIAG_START no_sd_write=1 physical_uart=disabled max_frames={}'.format(
    MAX_FRAMES))
try:
    result = run_auth.run(config=dict(CONFIG), max_frames=MAX_FRAMES,
                          uart_override=SinkUART(), observer=observe)
except Exception as exc:
    result = {'uncaught': repr(exc)}
print('FACE_DIAG_END result={} raw_counts={} confirmed_counts={} '
      'max_unknown_score_q={}'.format(
          result, counts, confirmed_counts, max_unknown_score_q))
