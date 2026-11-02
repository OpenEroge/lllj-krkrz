// Multi-track BGM (.mchx, the wumultitrack plugin of the original).
//
// A .mchx is a tab-separated manifest naming a multi-channel source and the
// stereo pairs that make up each instrument track:
//     channel 2
//     source  BGM71.opus
//     track   pf  0  1
//     mute    pf            (optional: track starts silent)
// The decoder mixes the tracks down to `channel` outputs with per-track gains.
// sysscn/voiceeffect.tjs (MultiTrackMusicSupport) learns the decoder id through
// WaveSoundBuffer.multiTrackOpenCallbacks and changes the gains with
// WaveSoundBuffer.setMultiTrackMask(id, name|void, volume, time_ms).
#include "tjsCommHead.h"
#include "tjsNative.h"
#include "tjsArray.h"
#include "WaveIntf.h"
#include "StorageIntf.h"
#include "ScriptMgnIntf.h"
#include "MsgIntf.h"
#include "Extension.h"

#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::mutex Lock; // guards Decoders and every decoder's track gains
class MultiTrackDecoder;
std::map<ttstr, MultiTrackDecoder *> Decoders;
int NextId = 0;

class MultiTrackDecoder : public tTVPWaveDecoder
{
public:
	struct Track {
		ttstr name;
		int left, right;
		float gain = 1, target = 1, step = 0;
		tjs_int64 rampLeft = 0;
	};
	ttstr Id;
	std::vector<Track> Tracks;

	MultiTrackDecoder(tTVPWaveDecoder *source, int outChannels, std::vector<Track> tracks)
		: Source(source), Tracks(std::move(tracks))
	{
		Source->GetFormat(SourceFormat);
		Format = SourceFormat;
		Format.Channels = outChannels;
		Format.IsFloat = true;
		Format.BitsPerSample = 32;
		Format.BytesPerSample = 4;
		Format.SpeakerConfig = 0;
		std::lock_guard<std::mutex> g(Lock);
		Id = ttstr(TJS_W("mtrack")) + ttstr((tjs_int)++NextId);
		Decoders[Id] = this;
	}
	~MultiTrackDecoder() override
	{
		std::lock_guard<std::mutex> g(Lock);
		Decoders.erase(Id);
	}

	void GetFormat(tTVPWaveFormat &format) override { format = Format; }
	bool SetPosition(tjs_uint64 pos) override { return Source->SetPosition(pos); }

	bool Render(void *buf, tjs_uint frames, tjs_uint &rendered) override
	{
		const int inCh = SourceFormat.Channels, outCh = Format.Channels;
		Raw.resize((size_t)frames * SourceFormat.BytesPerSample * inCh);
		In.resize((size_t)frames * inCh);
		bool more = Source->Render(Raw.data(), frames, rendered);
		TVPConvertPCMToFloat(In.data(), Raw.data(), SourceFormat, rendered);

		float *out = (float *)buf;
		std::fill(out, out + (size_t)rendered * outCh, 0.f);
		std::lock_guard<std::mutex> g(Lock);
		for (Track &t : Tracks) {
			for (tjs_uint i = 0; i < rendered; i++) {
				if (t.rampLeft > 0 && --t.rampLeft == 0) t.gain = t.target;
				else if (t.rampLeft > 0) t.gain += t.step;
				if (t.gain == 0) continue;
				const float *s = &In[(size_t)i * inCh];
				float *d = &out[(size_t)i * outCh];
				if (t.left < inCh) d[0] += t.gain * s[t.left];
				if (outCh > 1 && t.right < inCh) d[1] += t.gain * s[t.right];
			}
		}
		return more;
	}

	// caller holds Lock
	void SetGain(Track &t, float volume, tjs_int64 ms)
	{
		tjs_int64 frames = ms * SourceFormat.SamplesPerSec / 1000;
		t.target = volume;
		if (frames <= 0) {
			t.gain = volume;
			t.rampLeft = 0;
		} else {
			t.step = (volume - t.gain) / frames;
			t.rampLeft = frames;
		}
	}

private:
	std::unique_ptr<tTVPWaveDecoder> Source;
	tTVPWaveFormat SourceFormat, Format;
	std::vector<char> Raw;
	std::vector<float> In;
};

std::string ReadText(const ttstr &storage)
{
	std::unique_ptr<iTJSBinaryStream> stream(TVPCreateStream(storage));
	std::string text((size_t)stream->GetSize(), '\0');
	TVPReadBuffer(stream.get(), &text[0], (tjs_uint)text.size());
	return text;
}

ttstr OpenSource(const ttstr &manifest, const std::string &name)
{
	// relative to the manifest; the files are lower-case on disk
	ttstr dir = TVPExtractStoragePath(manifest);
	std::string lower = name;
	for (char &c : lower) c = (char)tolower((unsigned char)c);
	for (const std::string &n : {name, lower}) {
		ttstr path = dir + ttstr(n.c_str());
		if (TVPIsExistentStorage(path)) return path;
		if (TVPIsExistentStorage(ttstr(n.c_str()))) return ttstr(n.c_str());
	}
	TVPThrowExceptionMessage(TVPCannotOpenStorage, ttstr(name.c_str()));
	return ttstr();
}

void NotifyOpen(const MultiTrackDecoder &d, const ttstr &storage)
{
	// script: MultiTrackMusicSupport.onOpen(id, storage, ?, trackNames, aliases)
	iTJSDispatch2 *global = TVPGetScriptDispatch();
	tTJSVariant wsb, callbacks;
	global->PropGet(0, TJS_W("WaveSoundBuffer"), nullptr, &wsb, global);
	global->Release();
	if (wsb.Type() != tvtObject) return;
	wsb.AsObjectNoAddRef()->PropGet(0, TJS_W("multiTrackOpenCallbacks"), nullptr, &callbacks, nullptr);
	if (callbacks.Type() != tvtObject) return;
	iTJSDispatch2 *array = callbacks.AsObjectNoAddRef();

	iTJSDispatch2 *names = TJSCreateArrayObject();
	for (tjs_int i = 0; i < (tjs_int)d.Tracks.size(); i++) {
		tTJSVariant n(d.Tracks[i].name);
		names->PropSetByNum(TJS_MEMBERENSURE, i, &n, names);
	}
	tTJSVariant id(d.Id), name(storage), none, nameArray(names, names);
	names->Release();
	tTJSVariant *params[] = {&id, &name, &none, &nameArray, &none};

	tTJSVariant count;
	array->PropGet(0, TJS_W("count"), nullptr, &count, array);
	for (tjs_int i = 0; i < (tjs_int)count; i++) {
		tTJSVariant fn;
		array->PropGetByNum(0, i, &fn, array);
		if (fn.Type() == tvtObject)
			fn.AsObjectClosureNoAddRef().FuncCall(0, nullptr, nullptr, nullptr, 5, params, nullptr);
	}
}

class MultiTrackCreator : public tTVPWaveDecoderCreator
{
public:
	tTVPWaveDecoder *Create(const ttstr &storage, const ttstr &ext) override
	{
		if (ext != TJS_W(".mchx")) return nullptr;
		int channels = 2;
		std::string source;
		std::vector<MultiTrackDecoder::Track> tracks;
		std::istringstream lines(ReadText(storage));
		for (std::string line; std::getline(lines, line);) {
			std::istringstream f(line);
			std::string key, name;
			f >> key;
			if (key == "channel") f >> channels;
			else if (key == "source") { std::getline(f >> std::ws, source); while (!source.empty() && isspace((unsigned char)source.back())) source.pop_back(); }
			else if (key == "track") {
				MultiTrackDecoder::Track t;
				f >> name >> t.left >> t.right;
				t.name = ttstr(name.c_str());
				tracks.push_back(t);
			} else if (key == "mute") {
				f >> name;
				for (auto &t : tracks) if (t.name == ttstr(name.c_str())) t.gain = t.target = 0;
			}
		}
		if (source.empty() || tracks.empty() || channels < 1 || channels > 2)
			TVPThrowExceptionMessage((ttstr(TJS_W("invalid multitrack file: ")) + storage).c_str());
		tTVPWaveDecoder *inner = TVPCreateWaveDecoder(OpenSource(storage, source));
		auto *decoder = new MultiTrackDecoder(inner, channels, std::move(tracks));
		NotifyOpen(*decoder, storage);
		return decoder;
	}
} Creator;

// WaveSoundBuffer.setMultiTrackMask(id, name|void, volume, time_ms=0)
tjs_error TJS_INTF_METHOD SetMultiTrackMask(tTJSVariant *, tjs_int n, tTJSVariant **p, iTJSDispatch2 *)
{
	if (n < 3) return TJS_E_BADPARAMCOUNT;
	ttstr id = *p[0];
	bool all = p[1]->Type() == tvtVoid;
	ttstr name = all ? ttstr() : ttstr(*p[1]);
	float volume = (float)(double)*p[2];
	tjs_int64 ms = (n > 3 && p[3]->Type() != tvtVoid) ? (tjs_int64)*p[3] : 0;
	std::lock_guard<std::mutex> g(Lock);
	auto it = Decoders.find(id);
	if (it == Decoders.end()) return TJS_S_OK; // buffer already closed
	for (auto &t : it->second->Tracks)
		if (all || t.name == name) it->second->SetGain(t, volume, ms);
	return TJS_S_OK;
}

iTJSDispatch2 *InstallMultiTrack(iTJSDispatch2 *global)
{
	TVPRegisterWaveDecoderCreator(&Creator);
	tTJSVariant wsbv;
	global->PropGet(0, TJS_W("WaveSoundBuffer"), nullptr, &wsbv, global);
	iTJSDispatch2 *wsb = wsbv.AsObjectNoAddRef();
	const tjs_uint32 flags = TJS_MEMBERENSURE | TJS_IGNOREPROP | TJS_STATICMEMBER;
	{
		iTJSDispatch2 *callbacks = TJSCreateArrayObject();
		tTJSVariant v(callbacks, callbacks);
		callbacks->Release();
		wsb->PropSet(flags, TJS_W("multiTrackOpenCallbacks"), nullptr, &v, wsb);
	}
	{
		iTJSDispatch2 *fn = TJSCreateNativeClassMethod(SetMultiTrackMask);
		tTJSVariant v(fn, nullptr);
		fn->Release();
		wsb->PropSet(flags, TJS_W("setMultiTrackMask"), nullptr, &v, wsb);
	}
	return TJSCreateArrayObject(); // global.LLLJMultiTrack: placeholder required by tTVPAtInstallClass
}

} // namespace

static tTVPAtInstallClass LLLJInstallMultiTrack(TJS_W("LLLJMultiTrack"), InstallMultiTrack, TJS_W("WaveSoundBuffer"));
