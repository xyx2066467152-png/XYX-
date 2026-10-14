"""All deployment and temporal-confirmation settings. No permission information."""
CONFIG = {
    'api_major': 1, 'api_minor': 0,
    'face_threshold': 0.75,
    'detection_confidence': 0.5, 'nms_threshold': 0.2,
    'detection_model': '/sdcard/examples/kmodel/face_detection_320.kmodel',
    'recognition_model': '/sdcard/examples/kmodel/face_recognition.kmodel',
    'anchors_path': '/sdcard/examples/utils/prior_data_320.bin',
    'database_dir': '/sdcard/examples/utils/db/',
    'feature_dimension': 512, 'max_database_entries': 100,
    'anchor_count': 4200, 'anchor_dimension': 4,
    'sensor_size': (1280, 960), 'rgb_size': (1024, 768),
    'display_size': (640, 480), 'display_mode': 'lcd',
    'det_input_size': (320, 320), 'rec_input_size': (112, 112),
    'preview_enabled': True, 'preview_font_size': 32,
    'preview_line_width': 4, 'preview_box_color': (255, 0, 0, 255),
    'preview_text_color': (255, 255, 0, 0),
    # Explicit identity serialization, not an access-control list.
    'user_ids': {'USER01': 1, 'id_1': 61441, 'id_2': 61442},
    'success_frames': 3, 'success_min_ms': 150,
    'unknown_frames': 3, 'unknown_min_ms': 150,
    # Require sustained absence: live testing showed short detector dropouts
    # could re-arm authentication while USER01 remained in front of the lens.
    'leave_frames': 15, 'leave_min_ms': 3000,
    'cooldown_ms': 5000, 'max_frame_gap_ms': 1000,
    'error_repeat_ms': 5000,
    'uart_id': 2, 'uart_tx_pin': 44, 'uart_rx_pin': 45,
    'uart_baudrate': 115200, 'uart_timeout_ms': 0,
    # UART4 is shared with the board's debug connector. Lab harness only.
    'allow_debug_uart4': False,
    'partial_frame_timeout_ms': 200,
    'uart_read_chunk': 128, 'uart_max_reads_per_poll': 4,
    'uart_max_write_calls': 8,
    'max_requests_per_poll': 8, 'bad_request_interval_ms': 500,
    'loop_sleep_ms': 1, 'gc_every_frames': 1,
}


def validate_config(config):
    threshold = config['face_threshold']
    if isinstance(threshold, bool) or not 0 < threshold <= 1:
        raise ValueError('face_threshold must be in (0,1]')
    mapping = config['user_ids']
    if not mapping or len(mapping) > config['max_database_entries']:
        raise ValueError('identity map must be nonempty and bounded')
    seen = set()
    for name, number in mapping.items():
        if not isinstance(name, str) or not name or '/' in name or '.' in name:
            raise ValueError('identity name must be a database basename')
        if type(number) is not int or not 1 <= number <= 65535 or number in seen:
            raise ValueError('USER_ID must be unique in 1..65535')
        seen.add(number)
    routes = {1: (40, 41), 2: (44, 45)}
    uart_id = config['uart_id']
    if type(uart_id) is not int:
        raise ValueError('UART profile must be an integer')
    if uart_id in routes:
        if (config['uart_tx_pin'], config['uart_rx_pin']) != routes[uart_id]:
            raise ValueError('use documented DNK230D expansion UART pin pair')
    elif uart_id == 4 and config['allow_debug_uart4']:
        if config['uart_tx_pin'] is not None or config['uart_rx_pin'] is not None:
            raise ValueError('debug UART4 must retain existing pin routing')
    else:
        raise ValueError('unsupported UART profile')
    for key in ('feature_dimension','max_database_entries','anchor_count',
                'anchor_dimension','uart_baudrate','partial_frame_timeout_ms',
                'max_requests_per_poll','gc_every_frames','uart_read_chunk',
                'uart_max_reads_per_poll','uart_max_write_calls'):
        if type(config[key]) is not int or config[key] <= 0:
            raise ValueError(key+' must be a positive integer')
    for key in ('loop_sleep_ms','bad_request_interval_ms','uart_timeout_ms'):
        if type(config[key]) is not int or config[key] < 0:
            raise ValueError(key+' must be a nonnegative integer')
    if config['uart_read_chunk'] > 1024 or config['uart_max_reads_per_poll'] > 32 or config['uart_max_write_calls'] > 64:
        raise ValueError('UART I/O budget exceeds protocol adapter bounds')
    if config['partial_frame_timeout_ms'] > 60000:
        raise ValueError('partial frame timeout exceeds parser bounds')
    for key in ('api_major','api_minor'):
        if type(config[key]) is not int or not 0 <= config[key] <= 255:
            raise ValueError(key+' must fit a uint8')
    if config['feature_dimension'] > 65535:
        raise ValueError('feature dimension must fit a uint16')
    from confirmation import ConfirmationFilter
    keys = ('success_frames','success_min_ms','unknown_frames','unknown_min_ms',
            'leave_frames','leave_min_ms','cooldown_ms','max_frame_gap_ms','error_repeat_ms')
    ConfirmationFilter({key:config[key] for key in keys})
    return config
