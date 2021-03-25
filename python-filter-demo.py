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

