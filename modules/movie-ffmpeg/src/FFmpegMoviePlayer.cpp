// FFmpeg movie backend for VideoOverlay (mp4/wmv/asf/mov/avi).
//
// The engine's generic movie player handles WebM (libvpx) and MPEG-1 only. This
// player follows the structure of engine/generic/app/Mpeg1MoviePlayer.cpp
// (own decode thread, audio sink sample counter as the master clock, prepare
// frame, seek/loop/pause) with libavformat/libavcodec as the source. It is
// registered through TVPExternalMoviePlayerFactory (engine: MoviePlayer.h).
//
// FFmpeg comes from tools/fetch_ffmpeg.sh (prebuilt LGPL shared libraries that
// are copied next to the executable by run.sh).
#include "tjsCommHead.h"
#include "MoviePlayer.h"
#include "IMoviePlayer.h"            // IMovieReadStream
#include "MovieAudioSinkAdapter.h"   // tTVPMovieAudioSinkAdapter
#include "IAudioSink.h"
#include "DebugIntf.h"
#include "CharacterSet.h"
#include "tjsDictionary.h"
#include "compat/compat.h"
#include "movie_subtitles.h"

#include "libyuv/convert_argb.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
}

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

//---------------------------------------------------------------------------
// statistics for CompatNative.movieStats() (most recent player)
struct MovieStats {
	std::atomic<int64_t> frames{0};      // delivered video frames
	std::atomic<int64_t> dropped{0};     // frames skipped because the clock passed them
	std::atomic<int64_t> maxLateUs{0};   // max (clock - pts) at delivery
	std::atomic<int64_t> decodeUs{0};    // total video decode time
	std::atomic<int64_t> underrunUs{0};  // audio clock time spent without queued audio
	std::atomic<int64_t> opened{0};
	std::mutex nameLock;
	std::string name;
	// subtitles from compat/subtitles.h providers (movie_subtitles.cpp): the current player's object
	std::shared_ptr<CompatMovieSubtitles> subs;
	void Reset(const char *n, std::shared_ptr<CompatMovieSubtitles> s) {
		frames = 0; dropped = 0; maxLateUs = 0; decodeUs = 0; underrunUs = 0;
		++opened;
		std::lock_guard<std::mutex> lk(nameLock); name = n ? n : ""; subs = std::move(s);
	}
};
MovieStats gStats;

void AtomicMax(std::atomic<int64_t> &a, int64_t v) {
	int64_t cur = a.load();
	while (v > cur && !a.compare_exchange_weak(cur, v)) {}
}

int64_t NowUs() {
	return std::chrono::duration_cast<std::chrono::microseconds>(
		std::chrono::steady_clock::now().time_since_epoch()).count();
}

bool EndsWithCI(const char *s, const char *ext) {
	size_t len = std::strlen(s), el = std::strlen(ext);
	if (len < el) return false;
	for (size_t i = 0; i < el; ++i)
		if (std::tolower((unsigned char)s[len - el + i]) != ext[i]) return false;
	return true;
}

bool IsFFmpegPath(const char *name) {
	if (!name) return false;
	for (const char *ext : {".mp4", ".m4v", ".mov", ".wmv", ".asf", ".avi"})
		if (EndsWithCI(name, ext)) return true;
	return false;
}

ttstr FromUtf8(const std::string &s) {
	tjs_string w;
	TVPUtf8ToUtf16(w, s);
	return ttstr(w.c_str());
}

void Log(const std::string &s) {
	TVPAddImportantLog(FromUtf8("(compat) movie: " + s));
}

std::string AvErr(int e) {
	char buf[AV_ERROR_MAX_STRING_SIZE] = {};
	av_strerror(e, buf, sizeof(buf));
	return buf;
}

//---------------------------------------------------------------------------
class tCompatFFmpegMoviePlayer : public iTVPMoviePlayer
{
	enum State { stStopped, stPlaying, stPaused, stEnded };

	struct VideoFrame { AVFrame *frame; int64_t ptsUs; };
	struct AudioBuf { std::vector<float> data; };

	// source
	IMovieReadStream *mStream = nullptr;
	AVIOContext *mAvio = nullptr;
	AVFormatContext *mFmt = nullptr;
	AVCodecContext *mVDec = nullptr;
	AVCodecContext *mADec = nullptr;
	int mVIndex = -1, mAIndex = -1;
	AVPacket *mPkt = nullptr;
	AVFrame *mTmp = nullptr;
	int64_t mStartUs = 0;          // container start_time; all pts are relative to it
	bool mDemuxEOF = false;
	bool mVideoDrained = false, mAudioDrained = false;
	int64_t mSkipUntilUs = 0;      // after a seek: drop decoded data before this time

	int mWidth = 0, mHeight = 0;
	double mFps = 0.0;
	int64_t mDurationUs = 0;

	// audio
	tTVPMovieAudioSinkAdapter mSink;
	bool mHasAudio = false;
	int mAudioRate = 0, mAudioChannels = 0;
	std::vector<AudioBuf*> mAudioPool, mAudioFree;
	// clock: audio position = mAudioStartUs + samples played since flush (pinned
	// to the queued content so sink underrun padding does not move it ahead)
	bool mAudioStartValid = false;
	int64_t mAudioStartUs = 0;
	int64_t mPlayedBase = 0;       // sink played counter at flush (adjusted on underrun)
	int64_t mQueuedSamples = 0;    // samples enqueued since flush
	// some decoders (wmapro after a seek) emit frames without timestamps: those
	// continue from the previous frame, or start at the last audio packet's pts
	int64_t mAudioNextUs = AV_NOPTS_VALUE;
	// after a seek the sink stays stopped until the first video frame is ready,
	// so decoding up to the target (GOPs of several seconds) does not leave the
	// picture behind the already playing audio
	std::atomic<bool> mHoldAudio{false};
	int64_t mAudioPktUs = AV_NOPTS_VALUE;
	// clock without audio: wall time
	std::atomic<bool> mWallValid{false};   // reset from Play/Resume (main thread)
	int64_t mWallEpochUs = 0, mWallPtsUs = 0;

	std::deque<VideoFrame> mVideoQ;

	// state / thread
	std::atomic<State> mState{stStopped};
	std::atomic<bool> mPrepareOnly{false};
	std::atomic<bool> mTerminate{false};
	std::atomic<bool> mDoSeek{false};
	std::atomic<int64_t> mSeekUs{0};
	std::atomic<int64_t> mCurPosUs{0};
	std::atomic<bool> mLoop{false};
	std::thread mThread;
	std::mutex mMtx;
	std::condition_variable mCond;

	OnVideoDecoded mOnDecoded;
	OnVideoDecodedPlanes mOnPlanes;
	bool mUseYUV = false;
	float mVolume = 1.0f;
	std::shared_ptr<CompatMovieSubtitles> mSubs;

public:
	~tCompatFFmpegMoviePlayer() override
	{
		StopThread();
		ClearVideoQ();
		mSink.Stop();
		avcodec_free_context(&mVDec);
		avcodec_free_context(&mADec);
		av_packet_free(&mPkt);
		av_frame_free(&mTmp);
		avformat_close_input(&mFmt);
		if (mAvio) { av_freep(&mAvio->buffer); avio_context_free(&mAvio); }
		if (mStream) mStream->Release();
		for (AudioBuf *b : mAudioPool) delete b;
	}

	// stream != nullptr: read through it (ownership taken only on success).
	bool Init(IMovieReadStream *stream, const char *name, bool preferYUV)
	{
		mFmt = avformat_alloc_context();
		if (!mFmt) return false;
		int err;
		if (stream) {
			const int kBuf = 1 << 16;
			unsigned char *buf = (unsigned char *)av_malloc(kBuf);
			mAvio = avio_alloc_context(buf, kBuf, 0, stream, &ReadPacket, nullptr, &SeekStream);
			if (!mAvio) { av_free(buf); return false; }
			mFmt->pb = mAvio;
			mFmt->flags |= AVFMT_FLAG_CUSTOM_IO;
			stream->Seek(0, SEEK_SET);
			err = avformat_open_input(&mFmt, name, nullptr, nullptr);
		} else {
			err = avformat_open_input(&mFmt, name, nullptr, nullptr);
		}
		if (err < 0) { Log(std::string("open failed: ") + name + ": " + AvErr(err)); mFmt = nullptr; return false; }
		if ((err = avformat_find_stream_info(mFmt, nullptr)) < 0) {
			Log(std::string("no stream info: ") + AvErr(err)); return false;
		}

		mVIndex = av_find_best_stream(mFmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
		if (mVIndex < 0) { Log(std::string("no video stream: ") + name); return false; }
		mAIndex = av_find_best_stream(mFmt, AVMEDIA_TYPE_AUDIO, -1, mVIndex, nullptr, 0);
		if (!OpenDecoder(mVIndex, &mVDec)) return false;
		if (mVDec->pix_fmt != AV_PIX_FMT_YUV420P && mVDec->pix_fmt != AV_PIX_FMT_YUVJ420P) {
			Log(std::string("unsupported pixel format ") + av_get_pix_fmt_name(mVDec->pix_fmt));
			return false;
		}
		if (mAIndex >= 0 && !OpenDecoder(mAIndex, &mADec)) mAIndex = -1;
		for (unsigned i = 0; i < mFmt->nb_streams; ++i)
			if ((int)i != mVIndex && (int)i != mAIndex) mFmt->streams[i]->discard = AVDISCARD_ALL;

		AVStream *vs = mFmt->streams[mVIndex];
		mWidth = mVDec->width;
		mHeight = mVDec->height;
		AVRational fr = av_guess_frame_rate(mFmt, vs, nullptr);
		mFps = (fr.num > 0 && fr.den > 0) ? av_q2d(fr) : 0.0;
		mStartUs = mFmt->start_time != AV_NOPTS_VALUE ? mFmt->start_time : 0;
		mDurationUs = mFmt->duration != AV_NOPTS_VALUE ? mFmt->duration : 0;
		if (mWidth <= 0 || mHeight <= 0) return false;

		if (mADec) {
			mAudioRate = mADec->sample_rate;
			mAudioChannels = mADec->ch_layout.nb_channels;
			if (mAudioRate > 0 && mAudioChannels > 0 &&
			    mSink.Setup(mAudioChannels, mAudioRate, 32, IAudioSink::PCM_F32)) {
				mHasAudio = true;
			} else {
				avcodec_free_context(&mADec);
				mAIndex = -1;
			}
		}

		mPkt = av_packet_alloc();
		mTmp = av_frame_alloc();
		mUseYUV = preferYUV;

		char info[256];
		std::snprintf(info, sizeof(info), "%s: %s %dx%d %.3ffps %.1fs, audio %s %dHz %dch%s",
			name, avcodec_get_name(mVDec->codec_id), mWidth, mHeight, mFps, mDurationUs / 1e6,
			mADec ? avcodec_get_name(mADec->codec_id) : "none", mAudioRate, mAudioChannels,
			stream ? " (stream)" : "");
		Log(info);
		mSubs = CompatMovieSubtitles::Open(name, mWidth, mHeight);
		gStats.Reset(name, mSubs);

		mStream = stream;   // owned from here on (success)
		mThread = std::thread(&tCompatFFmpegMoviePlayer::ThreadMain, this);
		return true;
	}

	//--- iTVPMoviePlayer ---------------------------------------------------
	void Play(bool loop = false) override
	{
		std::lock_guard<std::mutex> lk(mMtx);
		mLoop = loop;
		if (mState == stEnded) { mDoSeek = true; mSeekUs = 0; }
		mPrepareOnly = false;
		mState = stPlaying;
		mWallValid = false;
		if (!mHoldAudio) mSink.Start();   // otherwise started with the first frame after the seek
		mCond.notify_all();
	}
	bool PrepareFrame() override
	{
		std::lock_guard<std::mutex> lk(mMtx);
		mSeekUs = 0; mDoSeek = true;
		mPrepareOnly = true;
		mState = stPlaying;
		mCond.notify_all();
		return true;
	}
	void Stop() override
	{
		std::lock_guard<std::mutex> lk(mMtx);
		mState = stStopped;
		mPrepareOnly = false;
		mSink.Stop();
		mCond.notify_all();
	}
	void Pause() override
	{
		std::lock_guard<std::mutex> lk(mMtx);
		mPrepareOnly = false;
		if (mState == stPlaying) { mState = stPaused; mSink.Stop(); }
		mCond.notify_all();
	}
	void Resume() override
	{
		std::lock_guard<std::mutex> lk(mMtx);
		if (mState == stPaused) { mState = stPlaying; mWallValid = false; if (!mHoldAudio) mSink.Start(); }
		mCond.notify_all();
	}
	void Seek(int64_t posUs) override
	{
		std::lock_guard<std::mutex> lk(mMtx);
		mSeekUs = posUs < 0 ? 0 : posUs;
		mDoSeek = true;
		mCond.notify_all();
	}
	void SetLoop(bool loop) override { mLoop = loop; }

	int32_t Width() const override { return mWidth; }
	int32_t Height() const override { return mHeight; }
	int64_t Duration() const override { return mDurationUs; }
	int64_t Position() const override { return mCurPosUs.load(); }
	double FrameRate() const override { return mFps; }
	bool IsPlaying() const override { return mState.load() == stPlaying; }
	bool Loop() const override { return mLoop.load(); }

	void SetOnVideoDecoded(OnVideoDecoded cb) override { mOnDecoded = cb; }
	void SetOnVideoDecodedPlanes(OnVideoDecodedPlanes cb) override { mOnPlanes = cb; }
	bool SupportsPlanes() const override { return mUseYUV; }
	bool IsAudioAvailable() const override { return mHasAudio; }
	void SetVolume(float v) override { mVolume = v; mSink.SetVolume(v); }
	float Volume() const override { return mVolume; }

private:
	//--- IO ------------------------------------------------------------------
	static int ReadPacket(void *opaque, uint8_t *buf, int size)
	{
		size_t n = static_cast<IMovieReadStream *>(opaque)->Read(buf, (size_t)size);
		return n == 0 ? AVERROR_EOF : (int)n;
	}
	static int64_t SeekStream(void *opaque, int64_t offset, int whence)
	{
		IMovieReadStream *s = static_cast<IMovieReadStream *>(opaque);
		if (whence & AVSEEK_SIZE) return (int64_t)s->Size();
		s->Seek(offset, whence & ~AVSEEK_FORCE);
		return s->Tell();
	}

	bool OpenDecoder(int index, AVCodecContext **out)
	{
		AVStream *st = mFmt->streams[index];
		const AVCodec *codec = avcodec_find_decoder(st->codecpar->codec_id);
		if (!codec) { Log(std::string("no decoder for ") + avcodec_get_name(st->codecpar->codec_id)); return false; }
		AVCodecContext *c = avcodec_alloc_context3(codec);
		if (!c) return false;
		avcodec_parameters_to_context(c, st->codecpar);
		c->pkt_timebase = st->time_base;
		c->thread_count = 0;   // auto
		c->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;
		int err = avcodec_open2(c, codec, nullptr);
		if (err < 0) {
			Log(std::string("decoder open failed: ") + codec->name + ": " + AvErr(err));
			avcodec_free_context(&c);
			return false;
		}
		*out = c;
		return true;
	}

	//--- thread helpers ------------------------------------------------------
	void StopThread()
	{
		mTerminate = true;
		{ std::lock_guard<std::mutex> lk(mMtx); mCond.notify_all(); }
		if (mThread.joinable()) mThread.join();
	}
	bool Interrupted()
	{
		return mTerminate.load() || mDoSeek.load() || mState.load() != stPlaying;
	}
	void WaitUs(int64_t us)
	{
		if (us <= 0) return;
		std::unique_lock<std::mutex> lk(mMtx);
		mCond.wait_for(lk, std::chrono::microseconds(us), [&] { return Interrupted(); });
	}
	void ClearVideoQ()
	{
		for (VideoFrame &f : mVideoQ) av_frame_free(&f.frame);
		mVideoQ.clear();
	}

	int64_t PtsUs(const AVFrame *f, int index)
	{
		int64_t ts = f->best_effort_timestamp;
		if (ts == AV_NOPTS_VALUE) ts = f->pts;
		if (ts == AV_NOPTS_VALUE) return AV_NOPTS_VALUE;
		return av_rescale_q(ts, mFmt->streams[index]->time_base, AV_TIME_BASE_Q) - mStartUs;
	}

	//--- audio ---------------------------------------------------------------
	void RecycleAudio()
	{
		void *p = nullptr;
		while (mSink.TryPopConsumed(&p)) if (p) mAudioFree.push_back(static_cast<AudioBuf *>(p));
	}
	AudioBuf *AcquireAudioBuf()
	{
		RecycleAudio();
		if (!mAudioFree.empty()) { AudioBuf *b = mAudioFree.back(); mAudioFree.pop_back(); return b; }
		AudioBuf *b = new AudioBuf();
		mAudioPool.push_back(b);
		return b;
	}
	void FlushAudio()
	{
		if (!mHasAudio) return;
		mSink.Flush();
		RecycleAudio();
		mPlayedBase = mSink.GetSamplesPlayed();
		mQueuedSamples = 0;
		mAudioStartValid = false;
	}
	// samples played since flush, clamped to what was queued (underrun padding
	// advances the sink counter without consuming content)
	int64_t PlayedSamples()
	{
		int64_t played = mSink.GetSamplesPlayed() - mPlayedBase;
		if (played < 0) { mPlayedBase += played; played = 0; }
		if (played > mQueuedSamples) {
			int64_t over = played - mQueuedSamples;
			if (!mDemuxEOF && mAudioStartValid)
				gStats.underrunUs += over * 1000000 / mAudioRate;
			mPlayedBase += over;
			played = mQueuedSamples;
		}
		return played;
	}
	int64_t AudioBufferedUs()
	{
		return (mQueuedSamples - PlayedSamples()) * 1000000 / mAudioRate;
	}
	void QueueAudio(AVFrame *f, int64_t ptsUs)
	{
		const int n = f->nb_samples, ch = mAudioChannels;
		if (n <= 0 || f->ch_layout.nb_channels != ch) return;
		if (!mAudioStartValid) {
			mAudioStartUs = ptsUs;
			mAudioStartValid = true;
		}
		AudioBuf *b = AcquireAudioBuf();
		b->data.resize((size_t)n * ch);
		float *dst = b->data.data();
		const AVSampleFormat fmt = (AVSampleFormat)f->format;
		const bool planar = av_sample_fmt_is_planar(fmt);
		for (int c = 0; c < ch; ++c) {
			const uint8_t *src = planar ? f->extended_data[c] : f->extended_data[0];
			const int step = planar ? 1 : ch, off = planar ? 0 : c;
			for (int i = 0; i < n; ++i) {
				const int k = i * step + off;
				float v;
				switch (av_get_packed_sample_fmt(fmt)) {
				case AV_SAMPLE_FMT_FLT: v = ((const float *)src)[k]; break;
				case AV_SAMPLE_FMT_DBL: v = (float)((const double *)src)[k]; break;
				case AV_SAMPLE_FMT_S16: v = ((const int16_t *)src)[k] * (1.0f / 32768.0f); break;
				case AV_SAMPLE_FMT_S32: v = ((const int32_t *)src)[k] * (1.0f / 2147483648.0f); break;
				case AV_SAMPLE_FMT_U8:  v = (((const uint8_t *)src)[k] - 128) * (1.0f / 128.0f); break;
				default: v = 0.0f; break;
				}
				dst[(size_t)i * ch + c] = v;
			}
		}
		mSink.Enqueue(dst, b->data.size() * sizeof(float), false, b);
		mQueuedSamples += n;
	}

	//--- clock ---------------------------------------------------------------
	// current presentation time in microseconds, or AV_NOPTS_VALUE before the
	// clock source has started
	int64_t ClockUs()
	{
		if (mHasAudio && !mWallValid) {
			if (!mAudioStartValid) return AV_NOPTS_VALUE;
			const int64_t played = PlayedSamples();
			const int64_t clk = mAudioStartUs + played * 1000000 / mAudioRate;
			if (!(mAudioDrained && played >= mQueuedSamples)) return clk;
			// the audio track ended before the video: continue on wall time
			mWallPtsUs = std::max<int64_t>(clk, mCurPosUs.load()); mWallEpochUs = NowUs(); mWallValid = true;
			return mWallPtsUs;
		}
		if (!mWallValid) return AV_NOPTS_VALUE;
		return mWallPtsUs + (NowUs() - mWallEpochUs);
	}

	//--- demux / decode ------------------------------------------------------
	void ReceiveVideo()
	{
		for (;;) {
			int64_t t0 = NowUs();
			int r = avcodec_receive_frame(mVDec, mTmp);
			gStats.decodeUs += NowUs() - t0;
			if (r == AVERROR_EOF) { mVideoDrained = true; return; }
			if (r < 0) return;
			int64_t pts = PtsUs(mTmp, mVIndex);
			if (pts == AV_NOPTS_VALUE)
				pts = mVideoQ.empty() ? mCurPosUs.load() : mVideoQ.back().ptsUs + (mFps > 0 ? (int64_t)(1e6 / mFps) : 33333);
			if (pts < mSkipUntilUs - (mFps > 0 ? (int64_t)(1e6 / mFps) : 0)) { av_frame_unref(mTmp); continue; }
			AVFrame *f = av_frame_alloc();
			av_frame_move_ref(f, mTmp);
			mVideoQ.push_back({f, pts});
		}
	}
	void ReceiveAudio()
	{
		for (;;) {
			int r = avcodec_receive_frame(mADec, mTmp);
			if (r == AVERROR_EOF) { mAudioDrained = true; return; }
			if (r < 0) return;
			int64_t pts = PtsUs(mTmp, mAIndex);
			if (pts == AV_NOPTS_VALUE) pts = mAudioNextUs != AV_NOPTS_VALUE ? mAudioNextUs : mAudioPktUs;
			if (pts == AV_NOPTS_VALUE) pts = 0;
			const int64_t end = pts + (int64_t)mTmp->nb_samples * 1000000 / mAudioRate;
			mAudioNextUs = end;
			if (end > mSkipUntilUs) QueueAudio(mTmp, pts);
			av_frame_unref(mTmp);
		}
	}
	// read until enough video frames and audio are queued
	void Fill()
	{
		const size_t kMinVideo = 3, kMaxVideo = 12, kHardMaxVideo = 48;
		const int64_t kAudioAheadUs = 400000;
		while (!mDemuxEOF && !Interrupted()) {
			const bool needVideo = mVideoQ.size() < kMinVideo;
			const bool needAudio = mHasAudio && !mPrepareOnly.load() && AudioBufferedUs() < kAudioAheadUs;
			if (!needVideo && !needAudio) break;
			// beyond kMaxVideo only while audio is about to starve (sparse interleave)
			if (mVideoQ.size() >= kMaxVideo && !(needAudio && AudioBufferedUs() < 100000 && mVideoQ.size() < kHardMaxVideo)) break;
			int r = av_read_frame(mFmt, mPkt);
			if (r < 0) {
				// end of file (or read error): drain decoders
				mDemuxEOF = true;
				avcodec_send_packet(mVDec, nullptr);
				ReceiveVideo();
				if (mADec) { avcodec_send_packet(mADec, nullptr); ReceiveAudio(); }
				if (r != AVERROR_EOF) Log("read error: " + AvErr(r));
				break;
			}
			if (mPkt->stream_index == mVIndex) {
				int64_t t0 = NowUs();
				avcodec_send_packet(mVDec, mPkt);
				gStats.decodeUs += NowUs() - t0;
				ReceiveVideo();
			} else if (mPkt->stream_index == mAIndex) {
				int64_t ts = mPkt->pts != AV_NOPTS_VALUE ? mPkt->pts : mPkt->dts;
				if (ts != AV_NOPTS_VALUE)
					mAudioPktUs = av_rescale_q(ts, mFmt->streams[mAIndex]->time_base, AV_TIME_BASE_Q) - mStartUs;
				avcodec_send_packet(mADec, mPkt);
				ReceiveAudio();
			}
			av_packet_unref(mPkt);
		}
	}

	void DoSeek(int64_t posUs)
	{
		ClearVideoQ();
		FlushAudio();
		int64_t ts = posUs + mStartUs;
		int r = av_seek_frame(mFmt, -1, ts, AVSEEK_FLAG_BACKWARD);
		if (r < 0 && posUs <= 0) {
			// some demuxers refuse timestamp seeks; fall back to the data start
			r = avformat_seek_file(mFmt, -1, INT64_MIN, mStartUs, mStartUs, AVSEEK_FLAG_BYTE);
		}
		if (r < 0) Log("seek failed: " + AvErr(r));
		avcodec_flush_buffers(mVDec);
		if (mADec) avcodec_flush_buffers(mADec);
		mDemuxEOF = mVideoDrained = mAudioDrained = false;
		mCurPosUs = posUs;
		mSkipUntilUs = posUs;
		mAudioNextUs = mAudioPktUs = AV_NOPTS_VALUE;
		if (mHasAudio) { mSink.Stop(); mHoldAudio = true; }
		mWallValid = false;
	}

	//--- main loop -----------------------------------------------------------
	void ThreadMain()
	{
		bool lastShown = false;   // the final frame got its display time already
		for (;;) {
			{
				std::unique_lock<std::mutex> lk(mMtx);
				mCond.wait(lk, [&] { return mTerminate.load() || mDoSeek.load() || mState.load() == stPlaying; });
			}
			if (mTerminate) break;
			if (mDoSeek.exchange(false)) { DoSeek(mSeekUs.load()); lastShown = false; continue; }
			if (mState.load() != stPlaying) continue;
			const bool prepareOnly = mPrepareOnly.load();

			Fill();
			if (Interrupted()) continue;

			if (!mVideoQ.empty()) {
				VideoFrame vf = mVideoQ.front();
				if (prepareOnly) {
					mVideoQ.pop_front();
					mCurPosUs = vf.ptsUs;
					Deliver(vf.frame, vf.ptsUs);
					av_frame_free(&vf.frame);
					std::lock_guard<std::mutex> lk(mMtx);
					if (mPrepareOnly.load()) {
						mState = stPaused;
						mPrepareOnly = false;
						mSeekUs = 0; mDoSeek = true;
					}
					continue;
				}
				int64_t clk = ClockUs();
				if (clk == AV_NOPTS_VALUE && (!mHasAudio || mAudioDrained)) {
					mWallPtsUs = vf.ptsUs; mWallEpochUs = NowUs(); mWallValid = true;
					clk = vf.ptsUs;
				}
				// audio not started yet, or held after a seek: show this frame now
				if (clk == AV_NOPTS_VALUE || mHoldAudio.load()) clk = std::max<int64_t>(clk == AV_NOPTS_VALUE ? 0 : clk, vf.ptsUs);
				if (clk < vf.ptsUs) { WaitUs(std::min<int64_t>(vf.ptsUs - clk, 10000)); continue; }
				mVideoQ.pop_front();
				// skip frames the clock has already passed (keep the newest due one)
				if (!mVideoQ.empty() && mVideoQ.front().ptsUs <= clk) {
					++gStats.dropped;
					av_frame_free(&vf.frame);
					continue;
				}
				ReleaseAudio();
				AtomicMax(gStats.maxLateUs, clk - vf.ptsUs);
				mCurPosUs = vf.ptsUs;
				Deliver(vf.frame, vf.ptsUs);
				++gStats.frames;
				av_frame_free(&vf.frame);
				lastShown = false;
				continue;
			}

			if (!mDemuxEOF) continue;   // Fill was interrupted or produced nothing yet
			// end of stream: let the last frame and the queued audio play out
			ReleaseAudio();   // seek target past the last video frame
			if (mHasAudio && mAudioStartValid && AudioBufferedUs() > 0) { WaitUs(10000); continue; }
			if (!lastShown && !mHasAudio && mFps > 0) { lastShown = true; WaitUs((int64_t)(1e6 / mFps)); continue; }
			if (Interrupted()) continue;
			if (mLoop.load()) { DoSeek(0); continue; }
			mSink.Stop();
			mCurPosUs = mDurationUs;
			mPrepareOnly = false;
			mState = stEnded;
		}
	}

	void ReleaseAudio()
	{
		std::lock_guard<std::mutex> lk(mMtx);
		if (mHoldAudio && mState.load() == stPlaying) { mHoldAudio = false; mSink.Start(); }
	}

	void Deliver(AVFrame *f, int64_t ptsUs)
	{
		const int w = f->width, h = f->height;
		const int64_t ms = ptsUs / 1000;
		if (mUseYUV && mOnPlanes) {
			const uint8_t *const src[3] = {f->data[0], f->data[1], f->data[2]};
			const int srcPitch[3] = {f->linesize[0], f->linesize[1], f->linesize[2]};
			uint8_t *planes[3] = {f->data[0], f->data[1], f->data[2]};
			int pitch[3] = {f->linesize[0], f->linesize[1], f->linesize[2]};
			// subtitles go into a copy: the decoder may still reference this frame
			if (mSubs && w == mWidth && h == mHeight) mSubs->BlendI420(ms, src, srcPitch, planes, pitch);
			iTVPMoviePlayer::VideoPlaneFrame p;
			p.width = w; p.height = h;
			p.format = iTVPMoviePlayer::VPF_I420;
			p.planeCount = 3;
			p.planes[0] = { planes[0], w, h, pitch[0] };
			p.planes[1] = { planes[1], (w + 1) / 2, (h + 1) / 2, pitch[1] };
			p.planes[2] = { planes[2], (w + 1) / 2, (h + 1) / 2, pitch[2] };
			mOnPlanes(p);
		} else if (mOnDecoded) {
			CompatMovieSubtitles *subs = (w == mWidth && h == mHeight) ? mSubs.get() : nullptr;
			mOnDecoded(w, h, [f, w, h, subs, ms](char *dest, int pitch) {
				// libyuv "ARGB" is B,G,R,A in memory, the layer pixel layout; alpha = 255
				libyuv::I420ToARGB(f->data[0], f->linesize[0], f->data[1], f->linesize[1],
					f->data[2], f->linesize[2], (uint8_t *)dest, pitch, w, h);
				if (subs) subs->BlendBGRA(ms, (uint8_t *)dest, pitch);
			});
		}
	}
};

iTVPMoviePlayer *CreateFFmpegPlayer(IMovieReadStream *stream, const char *utf8name, bool preferYUV)
{
	if (!IsFFmpegPath(utf8name)) return nullptr;
	tCompatFFmpegMoviePlayer *p = new tCompatFFmpegMoviePlayer();
	if (p->Init(stream, utf8name, preferYUV)) return p;
	delete p;   // stream stays with the caller
	return nullptr;
}

struct RegisterFactory {
	RegisterFactory() { TVPExternalMoviePlayerFactory = &CreateFFmpegPlayer; }
} gRegisterFactory;

void SetStat(iTJSDispatch2 *d, const tjs_char *name, double v)
{
	tTJSVariant value(v);
	d->PropSet(TJS_MEMBERENSURE, name, nullptr, &value, d);
}

} // namespace

// CompatNative.movieStats(): counters of the most recently opened FFmpeg movie
COMPAT_NATIVE(movieStats) {
	iTJSDispatch2 *d = TJSCreateDictionaryObject();
	SetStat(d, TJS_W("opened"), (double)gStats.opened.load());
	SetStat(d, TJS_W("frames"), (double)gStats.frames.load());
	SetStat(d, TJS_W("dropped"), (double)gStats.dropped.load());
	SetStat(d, TJS_W("max_late_ms"), gStats.maxLateUs.load() / 1000.0);
	SetStat(d, TJS_W("decode_ms"), gStats.decodeUs.load() / 1000.0);
	SetStat(d, TJS_W("underrun_ms"), gStats.underrunUs.load() / 1000.0);
	{
		std::lock_guard<std::mutex> lk(gStats.nameLock);
		tTJSVariant name(FromUtf8(gStats.name));
		d->PropSet(TJS_MEMBERENSURE, TJS_W("name"), nullptr, &name, d);
		// subtitles: file, events, most images in one frame, slowest render
		if (gStats.subs) {
			iTJSDispatch2 *s = TJSCreateDictionaryObject();
			tTJSVariant file(FromUtf8(gStats.subs->Name()));
			s->PropSet(TJS_MEMBERENSURE, TJS_W("file"), nullptr, &file, s);
			SetStat(s, TJS_W("events"), gStats.subs->Events());
			SetStat(s, TJS_W("images"), (double)gStats.subs->images);
			SetStat(s, TJS_W("max_images"), (double)gStats.subs->maxImages);
			SetStat(s, TJS_W("max_render_ms"), gStats.subs->maxRenderUs / 1000.0);
			tTJSVariant sv(s, s);
			s->Release();
			d->PropSet(TJS_MEMBERENSURE, TJS_W("subtitle"), nullptr, &sv, d);
		}
	}
	if (result) *result = tTJSVariant(d, d);
	d->Release();
	return TJS_S_OK;
}
