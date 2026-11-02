// ASS subtitles drawn onto decoded movie frames with libass. What to draw
// comes from the registered providers (compat/subtitles.h); a patch module or
// game profile supplies scripts and fonts. Blending happens on the decode
// thread, so subtitles also show in overlay mode where the movie is on top.
#include "tjsCommHead.h"
#include "compat/compat.h"
#include "compat/subtitles.h"
#include "movie_subtitles.h"
#include "DebugIntf.h"
#include "CharacterSet.h"
#include <ass/ass.h>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <mutex>

namespace {

std::vector<compat::SubtitleProvider> &Providers() { static std::vector<compat::SubtitleProvider> v; return v; }
std::vector<compat::SubtitleFontProvider> &FontProviders() { static std::vector<compat::SubtitleFontProvider> v; return v; }

void Log(const std::string &s)
{
	tjs_string w;
	TVPUtf8ToUtf16(w, "(compat) subtitles: " + s);
	TVPAddImportantLog(ttstr(w.c_str()));
}

void AssLog(int level, const char *fmt, va_list va, void *)
{
	if (level > 1) return; // errors only
	char buf[512];
	vsnprintf(buf, sizeof buf, fmt, va);
	Log(std::string("libass: ") + buf);
}

// One library for the process; fonts are read from the pack once.
ASS_Library *Library()
{
	static ASS_Library *lib = nullptr;
	static std::once_flag once;
	std::call_once(once, [] {
		lib = ass_library_init();
		if (!lib) return;
		ass_set_message_cb(lib, AssLog, nullptr);
		std::vector<compat::SubtitleFont> fonts;
		for (auto provider : FontProviders()) provider(fonts);
		for (auto &f : fonts)
			ass_add_font(lib, f.family.c_str(), reinterpret_cast<const char *>(f.data.data()), (int)f.data.size());
	});
	return lib;
}

std::string Leaf(const std::string &path)
{
	size_t p = path.find_last_of("/\\>");
	std::string leaf = p == std::string::npos ? path : path.substr(p + 1);
	for (auto &c : leaf) c = (char)std::tolower((unsigned char)c);
	return leaf;
}

inline uint8_t Mix(uint8_t dst, int src, int a) { return (uint8_t)((dst * (255 - a) + src * a + 127) / 255); }

} // namespace

std::unique_ptr<CompatMovieSubtitles> CompatMovieSubtitles::Open(const std::string &movie, int width, int height)
{
	compat::SubtitleScript script;
	bool found = false;
	for (auto provider : Providers()) if ((found = provider(movie, script))) break;
	if (!found) return nullptr;
	ASS_Library *lib = Library();
	if (!lib) return nullptr;

	std::unique_ptr<CompatMovieSubtitles> s(new CompatMovieSubtitles());
	s->mName = script.name;
	s->mWidth = width;
	s->mHeight = height;
	s->mTrack = ass_read_memory(lib, reinterpret_cast<char *>(script.data.data()), script.data.size(), const_cast<char *>("UTF-8"));
	s->mRenderer = ass_renderer_init(lib);
	if (!s->mTrack || !s->mRenderer) { Log("cannot load " + s->mName); return nullptr; }
	ass_set_frame_size(s->mRenderer, width, height);
	ass_set_storage_size(s->mRenderer, width, height);
	// fonts not registered by a provider come from fontconfig
	ass_set_fonts(s->mRenderer, nullptr, "sans-serif", ASS_FONTPROVIDER_AUTODETECT, nullptr, 1);
	Log(s->mName + " for " + Leaf(movie) + " (" + std::to_string(s->mTrack->n_events) + " events)");
	return s;
}

CompatMovieSubtitles::~CompatMovieSubtitles()
{
	if (mTrack) ass_free_track(mTrack);
	if (mRenderer) ass_renderer_done(mRenderer);
}

int CompatMovieSubtitles::Events() const { return mTrack ? mTrack->n_events : 0; }

ASS_Image *CompatMovieSubtitles::Render(int64_t timeMs)
{
	auto t0 = std::chrono::steady_clock::now();
	int changed = 0;
	ASS_Image *img = ass_render_frame(mRenderer, mTrack, (long long)timeMs, &changed);
	int64_t n = 0;
	for (ASS_Image *i = img; i; i = i->next) ++n;
	const int64_t us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count();
	images = n;
	renderUs = us;
	if (n > maxImages) maxImages = n;  // single writer under mLock
	if (us > maxRenderUs) maxRenderUs = us;
	return img;
}

bool CompatMovieSubtitles::BlendI420(int64_t timeMs, const uint8_t *const src[3], const int srcPitch[3],
	uint8_t *dst[3], int dstPitch[3])
{
	std::lock_guard<std::mutex> lk(mLock);
	ASS_Image *img = Render(timeMs);
	if (!img) return false;
	const int cw = (mWidth + 1) / 2, ch = (mHeight + 1) / 2;
	const int pw[3] = {mWidth, cw, cw}, ph[3] = {mHeight, ch, ch};
	for (int p = 0; p < 3; ++p) {
		mPlanes[p].resize((size_t)pw[p] * ph[p]);
		for (int y = 0; y < ph[p]; ++y)
			memcpy(mPlanes[p].data() + (size_t)y * pw[p], src[p] + (size_t)y * srcPitch[p], pw[p]);
		dst[p] = mPlanes[p].data();
		dstPitch[p] = pw[p];
	}
	uint8_t *Y = dst[0], *U = dst[1], *V = dst[2];
	for (; img; img = img->next) {
		if (img->w <= 0 || img->h <= 0) continue;
		const int r = img->color >> 24, g = (img->color >> 16) & 255, b = (img->color >> 8) & 255;
		const int opacity = 255 - (int)(img->color & 255);
		// BT.601 limited range: the GL presenter and libyuv convert I420 with it
		const int yv = (int)(16.5 + 0.2568 * r + 0.5041 * g + 0.0979 * b);
		const int uv = (int)(128.5 - 0.1482 * r - 0.2910 * g + 0.4392 * b);
		const int vv = (int)(128.5 + 0.4392 * r - 0.3678 * g - 0.0714 * b);
		const int x0 = std::max(0, img->dst_x), y0 = std::max(0, img->dst_y);
		const int x1 = std::min(mWidth, img->dst_x + img->w), y1 = std::min(mHeight, img->dst_y + img->h);
		for (int y = y0; y < y1; ++y) {
			const uint8_t *m = img->bitmap + (size_t)(y - img->dst_y) * img->stride - img->dst_x;
			uint8_t *row = Y + (size_t)y * mWidth;
			for (int x = x0; x < x1; ++x)
				if (m[x]) row[x] = Mix(row[x], yv, m[x] * opacity / 255);
		}
		// chroma: average coverage of each 2x2 block
		for (int cy = y0 / 2; cy < (y1 + 1) / 2; ++cy) {
			for (int cx = x0 / 2; cx < (x1 + 1) / 2; ++cx) {
				int sum = 0;
				for (int dy = 0; dy < 2; ++dy) {
					const int y = cy * 2 + dy;
					if (y < y0 || y >= y1) continue;
					const uint8_t *m = img->bitmap + (size_t)(y - img->dst_y) * img->stride - img->dst_x;
					for (int dx = 0; dx < 2; ++dx) {
						const int x = cx * 2 + dx;
						if (x >= x0 && x < x1) sum += m[x];
					}
				}
				if (!sum) continue;
				const int a = (sum / 4) * opacity / 255;
				U[(size_t)cy * cw + cx] = Mix(U[(size_t)cy * cw + cx], uv, a);
				V[(size_t)cy * cw + cx] = Mix(V[(size_t)cy * cw + cx], vv, a);
			}
		}
	}
	return true;
}

void CompatMovieSubtitles::BlendBGRA(int64_t timeMs, uint8_t *dest, int pitch)
{
	std::lock_guard<std::mutex> lk(mLock);
	for (ASS_Image *img = Render(timeMs); img; img = img->next) {
		const int r = img->color >> 24, g = (img->color >> 16) & 255, b = (img->color >> 8) & 255;
		const int opacity = 255 - (int)(img->color & 255);
		const int x0 = std::max(0, img->dst_x), y0 = std::max(0, img->dst_y);
		const int x1 = std::min(mWidth, img->dst_x + img->w), y1 = std::min(mHeight, img->dst_y + img->h);
		for (int y = y0; y < y1; ++y) {
			const uint8_t *m = img->bitmap + (size_t)(y - img->dst_y) * img->stride - img->dst_x;
			uint8_t *row = dest + (size_t)y * pitch;
			for (int x = x0; x < x1; ++x) {
				if (!m[x]) continue;
				const int a = m[x] * opacity / 255;
				uint8_t *p = row + x * 4;
				p[0] = Mix(p[0], b, a);
				p[1] = Mix(p[1], g, a);
				p[2] = Mix(p[2], r, a);
			}
		}
	}
}

void compat::AddSubtitleProvider(SubtitleProvider provider) { Providers().push_back(provider); }
void compat::AddSubtitleFontProvider(SubtitleFontProvider provider) { FontProviders().push_back(provider); }
