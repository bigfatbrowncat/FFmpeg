import ffmpeg_api
import cv2
import typing


@ffmpeg_api.wrap_filter
class Foo:
    def __init__(self, arg: str):
        print(f"Got <{arg}>!")

    def get_formats(
        self,
    ) -> typing.List[typing.Union[str, ffmpeg_api.AVPixFmtDescriptor, int]]:
        return ["rgb24"]

    def config_output(self, outlink: ffmpeg_api.AVFilterLink):
        inlink = outlink.src.inputs[0]
        outlink.w = inlink.w * 4
        outlink.h = inlink.h * 4

    def __call__(self, av_in: ffmpeg_api.AVFrame, av_out: ffmpeg_api.AVFrame):
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

