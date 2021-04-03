try:
    from sys import LIBAVUTIL_VERSION_MAJOR
except ImportError:
    raise ImportError("Please use ffmpeg_api from ffmpeg python filter")

import numpy as np
import numpy.ctypeslib as np_ctypes

import ctypes
from ctypes import (
    c_int,
    c_int8,
    c_uint8,
    c_int64,
    c_uint64,
    c_size_t,
    c_void_p,
    POINTER,
)

AV_NUM_DATA_POINTERS = 8

# see macro usage in frame.h
# taken from version.h
FF_API_PKT_PTS = LIBAVUTIL_VERSION_MAJOR < 57
FF_API_ERROR_FRAME = LIBAVUTIL_VERSION_MAJOR < 57
FF_API_FRAME_QP = LIBAVUTIL_VERSION_MAJOR < 57


class AVRational(ctypes.Structure):
    _fields_ = [("num", c_int), ("den", c_int)]


class AVFrame(ctypes.Structure):
    _fields_ = (
        [
            ("_data", POINTER(c_uint8) * AV_NUM_DATA_POINTERS),
            ("linesize", c_int * AV_NUM_DATA_POINTERS),
            ("extended_data", POINTER(POINTER(c_uint8))),
            ("width", c_int),
            ("height", c_int),
            ("nb_samples", c_int),
            ("format", c_int),
            ("key_frame", c_int),
            ("_pict_type", c_int),
            ("sample_aspect_ratio", AVRational),
            ("pts", c_int64),
        ]
        + ([("pkt_pts", c_int64)] if FF_API_PKT_PTS else [])
        + [
            ("pkt_dts", c_int64),
            ("coded_picture_number", c_int),
            ("display_picture_number", c_int),
            ("quality", c_int),
            ("opaque", c_void_p),
        ]
        + ([("error", c_uint64 * AV_NUM_DATA_POINTERS)] if FF_API_ERROR_FRAME else [])
        + [
            ("repeat_pic", c_int),
            ("interlaced_frame", c_int),
            ("top_field_first", c_int),
            ("palette_has_changed", c_int),
            ("reordered_opaque", c_int64),
            ("sample_rate", c_int),
            ("channel_layout", c_uint64),
            # array of AVBufferRef, declare later if needed
            ("buf", c_void_p * AV_NUM_DATA_POINTERS),
            ("extended_buf", c_void_p),
            ("nb_extended_buf", c_int),
            ("side_data", c_void_p),
            ("nb_side_data", c_int),
            ("flags", c_int),
            ("_color_range", c_int),
            ("_color_primaries", c_int),
            ("_color_trc", c_int),
            ("_colorspace", c_int),
            ("_chroma_location", c_int),
            ("best_effort_timestamp", c_int64),
            ("pkt_pos", c_int64),
            ("pkt_duration", c_int64),
            ("metadata", c_void_p),
            ("decode_error_flags", c_int),
            ("channels", c_int),
            ("pkt_size", c_int),
        ]
        + (
            [
                ("qscale_table", ctypes.POINTER(c_int8)),
                ("qstride", c_int),
                ("qscale_type", c_int),
                ("qp_table_buf", c_void_p),
            ]
            if FF_API_FRAME_QP
            else []
        )
        + [
            ("hw_frames_ctx", c_void_p),
            ("opaque_ref", c_void_p),
            ("ctop_top", c_size_t),
            ("crop_bottom", c_size_t),
            ("crop_left", c_size_t),
            ("crop_right", c_size_t),
            ("private_ref", c_void_p),
        ]
    )

    @property
    def pict_type(self):
        # TODO: convert to enum?..
        return self._pict_type

    def get_data(self, idx, shape=None):
        # TODO: add caching?
        return np_ctypes.as_array(self._data[idx], shape=shape)


def unpack_frames(func):
    def transform(x):
        return AVFrame.from_buffer(x) if isinstance(x, memoryview) else x

    def wrapped(*args, **kw):
        nargs = tuple(transform(arg) for arg in args)
        nkw = {k: transform(v) for k, v in kw.items()}

        return func(*nargs, **nkw)

    return wrapped
