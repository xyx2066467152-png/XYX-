"""Manual CanMV candidate entry point. Does not install or replace /sdcard/main.py."""
import sys
PROJECT_DIR = '/sdcard/project_version/identity_auth_v1'
if '/sdcard' not in sys.path:
    sys.path.append('/sdcard')
if PROJECT_DIR in sys.path:
    sys.path.remove(PROJECT_DIR)
sys.path.insert(0,PROJECT_DIR)


def run(config=None, max_frames=None, uart_override=None, observer=None):
    import os, time, gc
    from config import CONFIG, validate_config
    from clock_adapter import MonotonicClock
    from hardware_uart import open_uart
    from module_runtime import IdentityModule, STATE_ERROR
    from events import make_event, ERROR
    from uart_protocol import (ERROR_CONFIG, ERROR_UART, ERROR_DATABASE,
        ERROR_MODEL, ERROR_CAMERA, ERROR_AI, ERROR_CLEANUP)
    settings = dict(CONFIG if config is None else config)
    try:
        validate_config(settings)
    except Exception as exc:
        return {'frames':0,'failure':str(exc),'cleanup_errors':[],
                'state':STATE_ERROR,'last_error':ERROR_CONFIG}
    clock = MonotonicClock(time)
    try:
        uart = uart_override if uart_override is not None else open_uart(settings)
    except Exception as exc:
        return {'frames':0,'failure':str(exc),'cleanup_errors':[],
                'state':STATE_ERROR,'last_error':ERROR_UART}
    module = IdentityModule(uart,settings)
    recognizer = None
    pipeline = None
    failure = None
    frames = 0
    cleanup_errors = []
    stage = ERROR_MODEL
    try:
        from ai_adapter import FaceRecognizer, DatabaseError
        from libs.PipeLine import PipeLine
        from media.sensor import Sensor
        from presentation import draw_preview
        stage = ERROR_CAMERA
        pipeline = PipeLine(rgb888p_size=list(settings['rgb_size']),
            display_size=list(settings['display_size']),display_mode=settings['display_mode'])
        sensor = Sensor(width=settings['sensor_size'][0],height=settings['sensor_size'][1])
        pipeline.create(sensor=sensor)
        # Preserve official initialization order: camera/media before KPU models.
        # PipeLine.create() calls nn.shrink_memory_pool().
        stage = ERROR_MODEL
        try:
            recognizer = FaceRecognizer(settings)
        except DatabaseError:
            stage = ERROR_DATABASE
            raise
        stage = ERROR_UART
        module.ready()
        while max_frames is None or frames < max_frames:
            os.exitpoint()
            stage = ERROR_UART
            module.poll(clock.now_ms())
            stage = ERROR_CAMERA
            frame = pipeline.get_frame()
            stage = ERROR_AI
            observation = recognizer.observe(frame)
            stage = ERROR_UART
            confirmed = module.observe(observation,clock.now_ms())
            if observer is not None:
                observer(observation,confirmed)
            stage = ERROR_CAMERA
            if settings['preview_enabled']:
                draw_preview(pipeline,recognizer,observation,settings)
            frames += 1
            stage = ERROR_UART
            module.poll(clock.now_ms())
            if frames % settings['gc_every_frames'] == 0:
                gc.collect()
            if settings['loop_sleep_ms']:
                time.sleep_ms(settings['loop_sleep_ms'])
    except Exception as exc:
        # One runtime error stops inference; no stale success after a hardware failure.
        failure = str(exc)
        if stage == ERROR_UART:
            module.record_link_failure(ERROR_UART)
        else:
            try:
                module.observe(make_event(ERROR,error_code=stage),clock.now_ms())
            except Exception:
                module.record_link_failure(ERROR_UART)
    finally:
        if recognizer is not None:
            cleanup_errors.extend(recognizer.close())
        if pipeline is not None:
            try:
                pipeline.destroy()
            except Exception as exc:
                cleanup_errors.append(str(exc))
        if cleanup_errors:
            try:
                module.observe(make_event(ERROR,error_code=ERROR_CLEANUP),clock.now_ms())
            except Exception:
                module.record_link_failure(ERROR_UART)
        if uart_override is None and hasattr(uart,'deinit'):
            try:
                uart.deinit()
            except Exception as exc:
                cleanup_errors.append(str(exc))
                module.record_link_failure(ERROR_CLEANUP)
    return {'frames':frames,'failure':failure,'cleanup_errors':cleanup_errors,
            'state':module.state,'last_error':module.last_error}


if __name__ == '__main__':
    run()
