// OP/ED/MV subtitles of the Chinese patch, rendered with libass (movie_subtitles.cpp).
#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

struct ass_library;
struct ass_renderer;
struct ass_track;
struct ass_image;

class LLLJMovieSubtitles {
public:
	// Main thread (evaluates the current language). Returns null when the patch
	// has no subtitle for this movie in the current language.
	static std::unique_ptr<LLLJMovieSubtitles> Open(const std::string &movieUtf8Path, int width, int height);
	~LLLJMovieSubtitles();

	// Decode thread. Blend the subtitle at timeMs into an I420 frame, copying the
	// planes into internal buffers first; on true, *y/*u/*v and the pitches point
	// at the blended copy (valid until the next call). False: nothing to draw.
	bool BlendI420(int64_t timeMs, const uint8_t *const src[3], const int srcPitch[3],
		uint8_t *dst[3], int dstPitch[3]);
	// Decode thread. Blend into a B,G,R,A image.
	void BlendBGRA(int64_t timeMs, uint8_t *dest, int pitch);

	const std::string &Name() const { return mName; }
	int Events() const;
	// statistics (read by LLLJNative.movieStats on the main thread)
	std::atomic<int64_t> images{0}, renderUs{0}, maxImages{0}, maxRenderUs{0};

private:
	LLLJMovieSubtitles() = default;
	ass_image *Render(int64_t timeMs);
	std::string mName;
	int mWidth = 0, mHeight = 0;
	ass_renderer *mRenderer = nullptr;
	ass_track *mTrack = nullptr;
	std::vector<uint8_t> mPlanes[3];
	std::mutex mLock; // the BGRA updater may run on another thread than the decoder
};
