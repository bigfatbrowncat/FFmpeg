import numpy as np
import cv2

def foo(format, w, h, src_linesize, dst_linesize, src_data, dst_data):
    src = np.frombuffer(src_data[0], dtype=np.uint8)
    dst = np.frombuffer(dst_data[0], dtype=np.uint8)

    src.shape = (h, src_linesize[0] // 3, 3)
    dst.shape = (h * 4, dst_linesize[0] // 3, 3)
    
    dw, dh = w*4, h*4
    img = cv2.resize(src, (dw, dh), interpolation=cv2.INTER_CUBIC)
    dst[:dh,:dw,:] = img[:dh,:dw,:]

import libavfilter

def foo_av(in_, out_):
    av_in = libavfilter.AVFrame.from_buffer(in_)
    av_out = libavfilter.AVFrame.from_buffer(out_)
    src = av_in.get_data(0, (av_in.height, av_in.linesize[0] // 3, 3))
    dst = av_out.get_data(0, (av_out.height, av_out.linesize[0] // 3, 3))
    
    dw, dh = av_out.width, av_out.height
    img = cv2.resize(src, (dw, dh), interpolation=cv2.INTER_CUBIC)
    dst[:dh,:dw,:] = img[:dh,:dw,:]

