"""Read-only model/database adapter. No UART, print, enrollment, or door action."""
import os
import math
import ulab.numpy as np
from vendor_face_models import FaceDetApp, FaceRegistrationApp
from identity_result import result_event


class DatabaseError(Exception):
    pass


class ModelError(Exception):
    pass


class FaceRecognizer:
    def __init__(self, config):
        self.config = config
        self.face_det = None
        self.face_reg = None
        self.database = []
        self.last_boxes = []
        self.last_matches = []
        try:
            self._load_database()
            anchors = np.fromfile(config['anchors_path'], dtype=np.float)
            anchors = anchors.reshape((config['anchor_count'], config['anchor_dimension']))
            self.face_det = FaceDetApp(
                config['detection_model'], list(config['det_input_size']), anchors,
                confidence_threshold=config['detection_confidence'],
                nms_threshold=config['nms_threshold'],
                rgb888p_size=list(config['rgb_size']),
                display_size=list(config['display_size']), debug_mode=0)
            self.face_reg = FaceRegistrationApp(
                config['recognition_model'], list(config['rec_input_size']),
                rgb888p_size=list(config['rgb_size']),
                display_size=list(config['display_size']), debug_mode=0)
            self.face_det.config_preprocess()
        except DatabaseError:
            self.close()
            raise
        except Exception as exc:
            self.close()
            raise ModelError(str(exc))

    def _load_database(self):
        c = self.config
        try:
            names = sorted(x for x in os.listdir(c['database_dir']) if x.endswith('.bin'))
            if not names:
                raise DatabaseError('no enrolled feature files')
            if len(names) > c['max_database_entries']:
                raise DatabaseError('database exceeds configured bound')
            for filename in names:
                with open(c['database_dir'].rstrip('/')+'/'+filename,'rb') as f:
                    raw = f.read(c['feature_dimension'] * 4 + 1)
                if len(raw) != c['feature_dimension'] * 4:
                    raise DatabaseError('invalid feature length: '+filename)
                feature = np.frombuffer(raw, dtype=np.float)
                norm = float(np.linalg.norm(feature))
                if not math.isfinite(norm) or norm <= 0:
                    raise DatabaseError('invalid feature norm: '+filename)
                self.database.append((filename[:-4], feature))
        except DatabaseError:
            raise
        except Exception as exc:
            raise DatabaseError(str(exc))

    def _match(self, feature):
        if len(feature) != self.config['feature_dimension']:
            raise ValueError('model feature dimension mismatch')
        norm = float(np.linalg.norm(feature))
        if not math.isfinite(norm) or norm <= 0:
            raise ValueError('nonfinite or zero model feature')
        # Keep official normalization and shifted cosine score unchanged.
        feature /= norm
        best_name, best_score = None, 0.0
        for name, db_feature in self.database:
            db_feature /= np.linalg.norm(db_feature)
            score = float(np.dot(feature, db_feature) / 2 + 0.5)
            if not math.isfinite(score):
                raise ValueError('nonfinite similarity')
            # Only contain floating-point roundoff at the mathematical endpoints.
            score = max(0.0, min(1.0, score))
            if score > best_score:
                best_name, best_score = name, score
        return best_name, best_score

    def observe(self, frame):
        boxes, landmarks = self.face_det.run(frame)
        self.last_boxes = boxes
        self.last_matches = []
        count = len(boxes)
        if count == 1:
            if len(landmarks) != 1:
                raise ValueError('face detector output mismatch')
            self.face_reg.config_preprocess(landmarks[0])
            self.last_matches.append(self._match(self.face_reg.run(frame)))
        # Multiple faces produce an explicit observation, never choose the first face.
        return result_event(count, self.last_matches, self.config['user_ids'],
                            self.config['face_threshold'])

    def close(self):
        errors = []
        for name in ('face_reg','face_det'):
            instance = getattr(self,name)
            if instance is not None:
                try:
                    instance.deinit()
                except Exception as exc:
                    errors.append(str(exc))
                setattr(self,name,None)
        self.database = []
        return errors
