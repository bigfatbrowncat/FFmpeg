try:
    from _ffmpeg import LIBAVUTIL_VERSION_MAJOR
    import _ffmpeg
except ImportError:
    raise ImportError("Please use ffmpeg_api from within ffmpeg python filter")

import numpy as np
import numpy.ctypeslib as np_ctypes

import ctypes
from ctypes import (
    c_int,
    c_uint,
    c_int8,
    c_uint8,
    c_int64,
    c_uint64,
    c_size_t,
    c_void_p,
    c_char_p,
    c_double,
    POINTER,
)

AV_NUM_DATA_POINTERS = 8

# see macro usage in frame.h
# taken from version.h
FF_API_PKT_PTS = LIBAVUTIL_VERSION_MAJOR < 57
FF_API_ERROR_FRAME = LIBAVUTIL_VERSION_MAJOR < 57
FF_API_FRAME_QP = LIBAVUTIL_VERSION_MAJOR < 57
FF_API_PLUS1_MINUS1 = LIBAVUTIL_VERSION_MAJOR < 57


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
            ("_format", c_int),
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
    def format(self):
        return FORMAT_BY_ID[self._format]

    @property
    def pict_type(self):
        # TODO: convert to enum?..
        return self._pict_type

    def get_data(self, idx, shape=None):
        # TODO: add caching?
        return np_ctypes.as_array(self._data[idx], shape=shape)


def staticmethod_helper(decorator):
    def wrapped_decorator(func):
        if isinstance(func, staticmethod):
            func = func.__func__
            is_static = True
        else:
            is_static = False
        res = decorator(func)
        return staticmethod(res) if is_static else res

    return wrapped_decorator


@staticmethod_helper
def unpack_frames(func):
    def transform(x):
        return AVFrame.from_buffer(x) if isinstance(x, memoryview) else x

    def wrapped(*args, **kw):
        nargs = tuple(transform(arg) for arg in args)
        nkw = {k: transform(v) for k, v in kw.items()}

        return func(*nargs, **nkw)

    return wrapped


class AVComponentDescriptor(ctypes.Structure):
    _fields_ = [
        ("plane", c_int),
        ("step", c_int),
        ("offset", c_int),
        ("shift", c_int),
        ("depth", c_int),
    ] + (
        [("step_minus1", c_int), ("depth_minus1", c_int), ("offset_plus1", c_int)]
        if FF_API_PLUS1_MINUS1
        else []
    )


class AVPixFmtDescriptor(ctypes.Structure):
    _fields_ = [
        ("_name", c_char_p),
        ("nb_components", c_uint8),
        ("log2_chroma_w", c_uint8),
        ("log2_chroma_h", c_uint8),
        ("flags", c_uint64),
        ("comp", AVComponentDescriptor * 4),
        ("_alias", c_char_p),
    ]
    _id = -1

    @property
    def name(self):
        return self._name.decode("utf8")

    @property
    def alias(self):
        return self._alias.decode("utf8")

    def __index__(self):
        return self._id

    def __int__(self):
        return self._id

    def __str__(self):
        return self.name

    def __repr__(self):
        return f"AVPixFmtDesc<name={self.name}>"


def get_pixel_formats():
    av_pix_fmt_desc_next = ctypes.CFUNCTYPE(
        ctypes.POINTER(AVPixFmtDescriptor), ctypes.POINTER(AVPixFmtDescriptor)
    )(_ffmpeg.av_pix_fmt_desc_next)
    av_pix_fmt_desc_get_id = ctypes.CFUNCTYPE(
        c_int, ctypes.POINTER(AVPixFmtDescriptor)
    )(_ffmpeg.av_pix_fmt_desc_get_id)

    def next_fmt(x):
        pfmt = av_pix_fmt_desc_next(x)
        if pfmt:
            fmt = pfmt.contents
            fmt._id = av_pix_fmt_desc_get_id(pfmt)
        else:
            fmt = None
        return fmt, pfmt

    fmt, pfmt = next_fmt(None)
    if fmt:
        yield fmt
    while pfmt:
        fmt, pfmt = next_fmt(pfmt)
        if fmt:
            yield fmt


PIXEL_FORMATS = list(get_pixel_formats())
MIN_FORMAT = int(min(PIXEL_FORMATS, key=int))
MAX_FORMAT = int(max(PIXEL_FORMATS, key=int))
FORMAT_BY_NAME = {fmt.name: fmt for fmt in PIXEL_FORMATS}
FORMAT_BY_ID = {int(fmt): fmt for fmt in PIXEL_FORMATS}


@staticmethod_helper
def convert_pixformat(func):
    def wrapper(*args, **kw):
        res = func(*args, **kw)
        if not isinstance(res, (list, tuple)):
            res = [res]
        return [FORMAT_BY_NAME.get(r, r) for r in res]

    return wrapper


class AVFilterContext(ctypes.Structure):
    # _fields_ would be added later
    @property
    def inputs(self):
        return self._inputs.contents

    @property
    def outputs(self):
        return self._outputs.contents


class AVFilterPad(ctypes.Structure):
    _fields_ = [
        ("name", c_char_p),
        ("type", c_int),
        ("get_video_buffer", c_void_p),
        ("get_audio_buffer", c_void_p),
        ("filter_frame", c_void_p),
        ("request_frame", c_void_p),
        ("config_props", c_void_p),
        ("needs_writable", c_int),
    ]


class AVFilterFormats(ctypes.Structure):
    _fields_ = [
        ("nb_formats", c_uint),
        ("formats", ctypes.POINTER(c_int)),
        ("refcount", c_uint),
        ("refs", c_void_p),
    ]


class AVFilterChannelLayouts(ctypes.Structure):
    _fields_ = [
        ("channel_layouts", ctypes.POINTER(c_uint64)),
        ("nb_channel_layouts", c_int),
        ("all_layouts", c_int),
        ("all_counts", c_int),
        ("refcount", c_uint),
        ("refs", c_void_p),
    ]


class AVFilterFormatsConfig(ctypes.Structure):
    _fields_ = [
        ("formats", ctypes.POINTER(AVFilterFormats)),
        ("samplerates", ctypes.POINTER(AVFilterFormats)),
        ("channel_layouts", ctypes.POINTER(AVFilterChannelLayouts)),
    ]


class AVFilterLink(ctypes.Structure):
    _fields_ = [
        ("_src", ctypes.POINTER(AVFilterContext)),
        ("srcpad", ctypes.POINTER(AVFilterPad)),
        ("_dst", ctypes.POINTER(AVFilterContext)),
        ("dstpad", ctypes.POINTER(AVFilterPad)),
        ("type", c_int),
        ("w", c_int),
        ("h", c_int),
        ("sample_aspect_ratio", AVRational),
        ("channel_layout", c_uint64),
        ("sample_rate", c_int),
        ("format", c_int),
        ("time_base", AVRational),
        ("incfg", AVFilterFormatsConfig),
        ("outcfg", AVFilterFormatsConfig),
        ("init_state", c_int),
        ("graph", c_void_p),
        ("current_pts", c_int64),
        ("current_pts_us", c_int64),
        ("age_index", c_int),
        ("frame_rate", AVRational),
        ("partial_buf", c_void_p),
        ("partial_buf_size", c_int),
        ("min_samples", c_int),
        ("max_samples", c_int),
        ("channels", c_int),
        ("frame_count_in", c_int64),
        ("frame_count_out", c_int64),
        ("frame_pool", c_void_p),
        ("frame_wanted_out", c_int),
        ("hw_frames_ctx", c_void_p),
        ("reserved", c_uint8 * 0xF000),
    ]

    @property
    def src(self):
        return self._src.contents

    @property
    def dst(self):
        return self._dst.contents


AVFilterContext._fields_ = [
    ("av_class", c_void_p),
    ("filter", c_void_p),
    ("name", c_char_p),
    ("input_pads", ctypes.POINTER(AVFilterPad)),
    ("_inputs", ctypes.POINTER(ctypes.POINTER(AVFilterLink))),
    ("nb_inputs", c_uint),
    ("output_pads", ctypes.POINTER(AVFilterPad)),
    ("_outputs", ctypes.POINTER(ctypes.POINTER(AVFilterLink))),
    ("nb_outputs", c_uint),
    ("priv", c_void_p),
    ("graph", c_void_p),
    ("thread_type", c_int),
    ("internal", c_void_p),
    ("command_queue", c_void_p),
    ("enable_str", c_char_p),
    ("enable", c_void_p),
    ("var_values", ctypes.POINTER(c_double)),
    ("is_disabled", c_int),
    ("hw_device_ctx", c_void_p),
    ("nb_threads", c_int),
    ("ready", c_uint),
    ("extra_hw_frames", c_int),
]


@staticmethod_helper
def unpack_filterlink(func):
    def transform(x):
        return AVFilterLink.from_buffer(x) if isinstance(x, memoryview) else x

    def wrapped(*args, **kw):
        nargs = tuple(transform(arg) for arg in args)
        nkw = {k: transform(v) for k, v in kw.items()}

        return func(*nargs, **nkw)

    return wrapped


def wrap_filter(cls):
    def _wrap(name, conv):
        try:
            method, method_desc = getattr(cls, name), cls.__dict__[name]
        except (AttributeError, KeyError):
            return
        method = conv(method)
        if isinstance(method_desc, staticmethod):
            method = staticmethod(method)
        setattr(cls, name, method)

    _wrap("get_formats", convert_pixformat)
    _wrap("config_input", unpack_filterlink)
    _wrap("config_output", unpack_filterlink)

    cls.__call__ = unpack_frames(cls.__call__)
    return cls
