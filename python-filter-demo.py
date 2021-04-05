import ffmpeg_api
import cv2


class Foo:
    def __init__(self, arg):
        print(f"Got <{arg}>!")

    @ffmpeg_api.convert_pixformat
    def get_formats(self):
        return ["rgb24"]

    @ffmpeg_api.unpack_frames
    def __call__(self, av_in, av_out):
        components = av_in.format.nb_components
        src = av_in.get_data(
            0, (av_in.height, av_in.linesize[0] // components, components)
        )
        dst = av_out.get_data(
            0, (av_out.height, av_out.linesize[0] // components, components)
        )

        dw, dh = av_out.width, av_out.height
        img = cv2.resize(src, (dw, dh), interpolation=cv2.INTER_CUBIC)
        dst[:dh, :dw, :] = img[:dh, :dw, :]

