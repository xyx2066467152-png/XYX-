"""Identity reporting interface, shared by real hardware and software harnesses."""
from events import NO_FACE, UNKNOWN, AUTH_SUCCESS, ERROR, make_event
from confirmation import ConfirmationFilter
from uart_protocol import (FrameParser, ProtocolUART, decode_message,
    CMD_MODULE_READY, CMD_AUTH_SUCCESS, CMD_UNKNOWN, CMD_NO_FACE, CMD_ERROR,
    CMD_PING, CMD_GET_STATUS, CMD_PONG, CMD_STATUS, ERROR_UNKNOWN_CMD,
    ERROR_BAD_PAYLOAD, ERROR_PROTOCOL)

STATE_STARTING = 0
STATE_READY = 1
STATE_RUNNING = 2
STATE_ERROR = 3
EVENT_NUMBER = {NO_FACE:1, UNKNOWN:2, AUTH_SUCCESS:3, ERROR:4}
CONFIRMATION_KEYS = ('success_frames','success_min_ms','unknown_frames',
    'unknown_min_ms','leave_frames','leave_min_ms','cooldown_ms',
    'max_frame_gap_ms','error_repeat_ms')


class IdentityModule:
    def __init__(self, uart, config):
        self.config = config
        self.filter = ConfirmationFilter({key:config[key] for key in CONFIRMATION_KEYS})
        self.link = ProtocolUART(uart, FrameParser(config['partial_frame_timeout_ms']),
            read_chunk=config['uart_read_chunk'],
            max_reads_per_poll=config['uart_max_reads_per_poll'],
            max_write_calls=config['uart_max_write_calls'])
        self.state = STATE_STARTING
        self.last_event = 0
        self.last_user_id = 0
        self.last_similarity = 0.0
        self.last_error = 0
        self._bad_request_ms = None
        self.dropped_requests = 0

    def ready(self):
        """Call only after the models, database and camera initialized successfully."""
        self.link.send_message(CMD_MODULE_READY, api_major=self.config['api_major'],
            api_minor=self.config['api_minor'],
            feature_dimension=self.config['feature_dimension'],
            threshold=self.config['face_threshold'])
        self.state = STATE_READY

    def observe(self, observation, now_ms):
        confirmed = self.filter.update(observation,now_ms)
        self.state = STATE_ERROR if observation['type']==ERROR else STATE_RUNNING
        if observation['type'] == ERROR:
            self.last_error = observation['error_code']
        else:
            self.last_error = 0
        for event in confirmed:
            self.last_event = EVENT_NUMBER[event['type']]
            self.last_user_id = event['user_id']
            # UNKNOWN scores are local diagnostics only. Keep the published V1
            # STATUS semantics unchanged: similarity describes a sent identity.
            self.last_similarity = (event['similarity']
                                    if event['type'] == AUTH_SUCCESS else 0.0)
            kind = event['type']
            if kind == AUTH_SUCCESS:
                self.link.send_message(CMD_AUTH_SUCCESS,user_id=event['user_id'],
                                       similarity=event['similarity'])
            elif kind == UNKNOWN:
                self.link.send_message(CMD_UNKNOWN,reason=event['reason'])
            elif kind == NO_FACE:
                self.link.send_message(CMD_NO_FACE)
            else:
                self.last_error = event['error_code']
                self.link.send_message(CMD_ERROR,error_code=event['error_code'],
                                       detail=event['detail'])
        return confirmed

    def record_link_failure(self, error_code):
        """Keep local diagnostics; never recursively attempt to report a broken UART."""
        self.state = STATE_ERROR
        self.last_event = EVENT_NUMBER[ERROR]
        self.last_user_id = 0
        self.last_similarity = 0.0
        self.last_error = error_code

    def _bad_request(self, frame, code, now_ms):
        if self._bad_request_ms is not None and now_ms-self._bad_request_ms < self.config['bad_request_interval_ms']:
            self.dropped_requests += 1
            return
        self.link.send_message(CMD_ERROR,seq=frame['seq'],error_code=code,detail=frame['cmd'])
        self._bad_request_ms = now_ms

    def poll(self, now_ms):
        frames = self.link.poll(now_ms)
        limit = self.config['max_requests_per_poll']
        self.dropped_requests += max(0,len(frames)-limit)
        for frame in frames[:limit]:
            # Never reply to module-originated messages, even malformed ones.
            # This prevents two reflected/cross-wired peers echoing ERROR forever.
            if frame['known'] and frame['cmd'] not in (CMD_PING,CMD_GET_STATUS):
                self.dropped_requests += 1
                continue
            if not frame['known']:
                self._bad_request(frame,ERROR_UNKNOWN_CMD,now_ms)
                continue
            try:
                decode_message(frame)
            except ValueError:
                self._bad_request(frame,ERROR_BAD_PAYLOAD,now_ms)
                continue
            if frame['cmd'] == CMD_PING:
                self.link.send_message(CMD_PONG,seq=frame['seq'])
            elif frame['cmd'] == CMD_GET_STATUS:
                self.link.send_message(CMD_STATUS,seq=frame['seq'],state=self.state,
                    last_event=self.last_event,last_user_id=self.last_user_id,
                    last_similarity=self.last_similarity,last_error=self.last_error)
            else:
                self._bad_request(frame,ERROR_PROTOCOL,now_ms)
        return len(frames)
