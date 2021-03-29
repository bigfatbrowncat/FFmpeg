import libavfilter

def foo_av(in_, out_):
    av_in = libavfilter.AVFrame.from_buffer(in_)
    av_out = libavfilter.AVFrame.from_buffer(out_)
    src = av_in.get_data(0, (av_in.height, av_in.linesize[0] // 3, 3))
    dst = av_out.get_data(0, (av_out.height, av_out.linesize[0] // 3, 3))
    
    dw, dh = av_out.width, av_out.height
    img = cv2.resize(src, (dw, dh), interpolation=cv2.INTER_CUBIC)
    dst[:dh,:dw,:] = img[:dh,:dw,:]

