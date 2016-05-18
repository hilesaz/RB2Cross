extern "C" {
#include <libavcodec\avcodec.h>
#include <libavformat\avformat.h>

#include <SDL.h>
}

#include <iostream>
#include <fstream>
#include <functional>

#include "dash.h"

enum ErrorSources
{
	ESDL,
	EFFMPEG,
	EGENERIC
};

const std::function<void(int)> EPrinters[] = {
	[](int val) {std::cout << "SDL error code: " << val << std::endl; },
	[](int val) {int val2 = -val; (std::cout << "FFMPEG error code: ").write((char*)&val2, 4) << std::endl; },
	[](int val) {std::cout << "GENERIC error code: " << val << std::endl; },
};

template <ErrorSources e, typename T>
struct ErrorPrint
{
	static void p(T arg)
	{
		std::cout << "generic print: " << arg << std::endl;
	}
};

template <ErrorSources e>
struct ErrorPrint<e, int>
{
	static void p(int arg)
	{
		std::function<void(int)> printer = EPrinters[e];
		printer(arg);
	}
};


template <typename T, ErrorSources e = EGENERIC>
__declspec(noreturn)
void unwrap(T arg, const char* msg = "Generic Failure."
	, std::function<bool(T)> pred = [](T val)->bool {return !val; })
{
	if (pred(arg))
	{
		ErrorPrint<e, T>::p(arg);
		std::cout << msg << std::endl;
		std::cin.get();
		exit(1457);
	}
}

struct SDL_Ctx
{
	SDL_Surface *left, *right;
	SDL_Renderer* r;
	SDL_Texture* t;
};

void decode_YUV420p(AVFrame* image, int i, int j, Uint8& r, Uint8& g, Uint8& b, Uint8& a)
{
	uint8_t *yin = image->data[0], *uin = image->data[1], *vin = image->data[2];
	int ystride = image->linesize[0], ustride = image->linesize[1], vstride = image->linesize[2];

	uint8_t y = yin[j*ystride + i];
	uint8_t u = uin[j / 2 * ustride / 2 + i / 2];
	uint8_t v = vin[j / 2 * vstride / 2 + i / 2];

	r = (Uint8)(y + 1.402 * (int8_t)(v - 128));
	g = (Uint8)(y - 0.344 * (int8_t)(u - 128) - 0.714 * (int8_t)(v - 128));
	b = (Uint8)(y + 1.772 * (int8_t)(u - 128));
	a = (Uint8)256;
}



bool filter_frame(SDL_Surface* left, SDL_Surface* right, AVFrame* image)
{
	Uint32* pl = (Uint32*)left->pixels, *pr = (Uint32*)right->pixels;
	
	if (image->format != AV_PIX_FMT_YUV420P)
	{
		std::cout << "Couldn't load frame data, expected YUV420P." << std::endl;
		return false;
	}

	for (int j = 0; j < left->h; ++j)
	{
		for (int i = 0; i < left->w; ++i)
		{
			Uint8 r, g, b, a;
			decode_YUV420p(image, i, j, r, g, b, a);

			pl[j*left->pitch + i] = SDL_MapRGBA(left->format, r, g, b, a);
			pr[j*right->pitch + i] = SDL_MapRGBA(right->format, r, g, b, 256-r);
		}
	}
	return true;
}

struct Bino
{
	SDL_Surface *l, *r;
};

bool display_frame(SDL_Ctx& ctx, AVFrame* image)
{
	if (!filter_frame(ctx.left, ctx.right, image))
		return false;

	int w = ctx.left->w, h = ctx.left->h;
	SDL_Rect leftBox = { 0,0,w,h }, rightBox = { w,0,w,h };
	SDL_UpdateTexture(ctx.t, &leftBox, ctx.left->pixels, ctx.left->pitch);
	SDL_UpdateTexture(ctx.t, &rightBox, ctx.right->pixels, ctx.right->pitch);

	SDL_RenderClear(ctx.r);
	SDL_RenderCopy(ctx.r, ctx.t, NULL, NULL);
	SDL_RenderPresent(ctx.r);
	return true;
}

int packet_generator_from_file(const std::string& filename, std::function < int (AVPacket&)>& generator, AVCodecParameters*& codec)
{
	AVFormatContext* ctx = nullptr;
	int res = avformat_open_input(&ctx, filename.c_str(), nullptr, nullptr);
	if (res < 0)
		return res;

	res = avformat_find_stream_info(ctx, nullptr);
	if (res < 0)
		return res;

	int videoStream = av_find_best_stream(ctx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
	/*
	int videoStream = -1;
	for (int i = 0; i < (int)ctx->nb_streams; ++i)
	{
		if (ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
		{
			videoStream = i;
			break;
		}
	}
	if (videoStream == -1)
		return -1;
	*/

	if (videoStream < 0)
		return videoStream;

	codec = ctx->streams[videoStream]->codecpar;
	
	generator = [=](AVPacket& pkt) mutable ->int
	{
		if (!ctx)
			return -(int)MKTAG('!','c','t','x');
		int res;
		do 
		{
			res = av_read_frame(ctx, &pkt);
			if (res < 0)
			{
				avformat_close_input(&ctx);
				ctx = 0;
			}
		} while (pkt.stream_index != videoStream);

		return res;
	};
	return 0;
}

int frame_generator_from_packetgen(std::function<int(AVPacket&)>&& packetgen,AVCodecParameters* codecPar, std::function<int(AVFrame&)>& generator)
{
	AVCodecContext* ctx = avcodec_alloc_context3(nullptr);
	unwrap<AVCodecContext*, EFFMPEG>(ctx, "Couldn't alloc context.");

	int res = avcodec_parameters_to_context(ctx, codecPar);
	unwrap<int, EFFMPEG>(res, "Couldn't load context from codec parameters", [](int val)->bool {return val < 0; });

	AVCodec* codec = avcodec_find_decoder(ctx->codec_id);
	unwrap<AVCodec*, EFFMPEG>(codec, "Codex not found?");

	res = avcodec_open2(ctx, codec, NULL);
	unwrap<int, EFFMPEG>(res, "Couldn't open codec.", [](int val)->bool {return val < 0; });

	AVPacket pkt;
	av_init_packet(&pkt);

	generator = [=](AVFrame& frame) mutable ->int
	{
		int ret = 0;
		bool done = true;
		do
		{
			int err = avcodec_receive_frame(ctx, &frame);
			if (err == AVERROR(EAGAIN))
			{
				int res = packetgen(pkt);
				unwrap<int, EFFMPEG>(res, "Ran out of packets before video was fully decoded.", [](int val)->bool {return val < 0; });

				std::cout << "Adding new packet of size " << pkt.size << std::endl;

				res = avcodec_send_packet(ctx, &pkt);
				if (res < 0)
					return res;
				done = false;
			}
			else //Handles success, EOF, and generic errors
			{
				if(err < 0)
					std::cout << "Generic error, whoops." << std::endl;
			}
		} while (!done);
		return ret;
	};
}
int transform_display_file(const std::string filename)
{
	av_register_all();
	avcodec_register_all();

	std::function< int(AVPacket&)> packet_gen;
	AVCodecParameters* codec;
	int res = packet_generator_from_file(filename, packet_gen, codec);
	unwrap<int>(res, "Couldn't load packet generator from file.", [](int val)->bool {return val < 0; });

	std::cout << "Detected codec id: " << codec->codec_id << std::endl;

	std::function<int(AVFrame&)> frame_gen;
	res = frame_generator_from_packetgen(std::move(packet_gen), codec, frame_gen);
	unwrap<int>(res, "Couldn't load frame generator from packet generator.", [](int val)->bool {return val < 0; });

	AVFrame* frame = av_frame_alloc();
	unwrap<AVFrame*, EFFMPEG>(frame, "Couldn't alloc video frame.");

	/*================================================
	SDL INITIALIZATION
	==================================================*/

	int width = codec->width, height = codec->height;

	SDL_Init(SDL_INIT_VIDEO);
	SDL_Ctx sdlctx;
	SDL_Window* window;
	SDL_CreateWindowAndRenderer(width * 2, height
		, SDL_WINDOW_SHOWN, &window, &sdlctx.r);

	sdlctx.t = SDL_CreateTexture(sdlctx.r
		, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING
		, width, height);

	sdlctx.left = SDL_CreateRGBSurface(0, width, height, 32
		, 0x00ff0000
		, 0x0000ff00
		, 0x000000ff
		, 0xff000000);

	sdlctx.right = SDL_CreateRGBSurface(0, width, height, 32
		, 0x00ff0000
		, 0x0000ff00
		, 0x000000ff
		, 0xff000000);

	int ret = 0;

	do
	{
		ret = frame_gen(*frame);
		unwrap<int, EFFMPEG>(ret, "Failed getting frame from stream.", [](int val)->bool {return val < 0; });

		bool displayed = display_frame(sdlctx, frame);
		if (!displayed)
			ret = -1234;
	} while (ret > 0);
	return ret;
}

//TODO: smarter pointers
int main(int argc, char** argv)
{
	//read_dash("Ba$$.mp4");
	transform_display_file("Ba$$.mp4");
	std::cin.get();
	return 0;
}
