// WaveSoundBuffer filters used by sysscn/voiceeffect.tjs (the wfTypicalDSP /
// wfBasicEffect plugins of the original): WaveDSPFilter, DelayEffect and
// StkFreeVerb. Every class is an iTVPBasicWaveFilter like the engine's
// PhaseVocoder: scripts put instances into buffer.filters before open().
//
// One host class (CompatWaveFilter) does the plumbing -- float conversion,
// segment pass-through, the reverb/echo tail after the source ends -- and
// owns an Effect that only processes interleaved float frames.
#include "tjsCommHead.h"
#include "tjsNative.h"
#include "tjsDictionary.h"
#include "WaveIntf.h"
#include "WaveLoopManager.h"
#include "WaveSegmentQueue.h"
#include "MsgIntf.h"
#include "Extension.h"
#include "DspFilters/Dsp.h"
#include "compat/compat.h"

#include <cmath>
#include <memory>
#include <vector>

namespace {

struct Effect {
	virtual ~Effect() {}
	virtual void prepare(int rate, int channels) = 0; // allocate for the stream format
	virtual void reset() = 0;                         // silence internal state
	virtual void process(float *frames, tjs_uint count, int channels) = 0; // in place, interleaved
	virtual double tailMs() const { return 0; }
	virtual void setParams(tjs_int, tTJSVariant **) { TVPThrowExceptionMessage(TJS_W("setParams is not supported")); }
};

double arg(tjs_int n, tTJSVariant **p, int i, double def)
{
	return (i < n && p[i]->Type() != tvtVoid) ? (double)*p[i] : def;
}

//---------------------------------------------------------------------------
// WaveDSPFilter(type, family) + setParams(sampleRate|void, p1..p7)
// Names and parameter order are DSPFilters' (Params[0] is the sample rate).
//---------------------------------------------------------------------------
struct DSPFilterEffect : Effect {
	typedef Dsp::Filter *(*Make)();
	Make make;
	Dsp::Params params;
	bool rateGiven = false;
	std::vector<std::unique_ptr<Dsp::Filter>> chans; // one mono filter per channel
	std::vector<float> plane;

	explicit DSPFilterEffect(Make m) : make(m) { params = std::unique_ptr<Dsp::Filter>(m())->getDefaultParams(); }

	void setParams(tjs_int n, tTJSVariant **p) override {
		rateGiven = n > 0 && p[0]->Type() != tvtVoid;
		for (tjs_int i = 0; i < n && i < Dsp::maxParameters; i++)
			if (p[i]->Type() != tvtVoid) params[i] = (double)*p[i];
		for (auto &f : chans) f->setParams(params);
	}
	void prepare(int rate, int channels) override {
		if (!rateGiven) params[0] = rate;
		chans.clear();
		for (int c = 0; c < channels; c++) {
			chans.emplace_back(make());
			chans.back()->setParams(params);
		}
	}
	void reset() override { for (auto &f : chans) f->reset(); }
	void process(float *x, tjs_uint n, int ch) override {
		plane.resize(n);
		for (int c = 0; c < ch; c++) {
			for (tjs_uint i = 0; i < n; i++) plane[i] = x[i * ch + c];
			float *pp = plane.data();
			chans[c]->process((int)n, &pp);
			for (tjs_uint i = 0; i < n; i++) x[i * ch + c] = plane[i];
		}
	}
};

template <class Design> Dsp::Filter *makeDesign() { return new Dsp::FilterDesign<Design, 1>; }

struct DSPType { const tjs_char *type, *family; DSPFilterEffect::Make make; };
#define POLE(T, F) { TJS_W(#T), TJS_W(#F), makeDesign<Dsp::F::Design::T<50>> },
#define BIQUAD(T, F) { TJS_W(#T), TJS_W(#F), makeDesign<Dsp::F::Design::T> },
#define POLE4(F) POLE(LowPass, F) POLE(HighPass, F) POLE(BandPass, F) POLE(BandStop, F)
const DSPType DSPTypes[] = {
	POLE4(Butterworth) POLE(LowShelf, Butterworth) POLE(HighShelf, Butterworth) POLE(BandShelf, Butterworth)
	POLE4(ChebyshevI) POLE(LowShelf, ChebyshevI) POLE(HighShelf, ChebyshevI) POLE(BandShelf, ChebyshevI)
	POLE4(ChebyshevII) POLE(LowShelf, ChebyshevII) POLE(HighShelf, ChebyshevII) POLE(BandShelf, ChebyshevII)
	POLE4(Elliptic) POLE4(Bessel) POLE(LowShelf, Bessel) POLE4(Legendre)
	BIQUAD(LowPass, RBJ) BIQUAD(HighPass, RBJ) BIQUAD(BandPass1, RBJ) BIQUAD(BandPass2, RBJ) BIQUAD(BandStop, RBJ)
	BIQUAD(LowShelf, RBJ) BIQUAD(HighShelf, RBJ) BIQUAD(BandShelf, RBJ) BIQUAD(AllPass, RBJ)
	BIQUAD(OnePole, Custom) BIQUAD(TwoPole, Custom)
};
#undef POLE4
#undef BIQUAD
#undef POLE

Effect *makeDSPFilter(tjs_int n, tTJSVariant **p)
{
	if (n < 2) TVPThrowExceptionMessage(TJS_W("WaveDSPFilter(type, family)"));
	ttstr type = *p[0], family = *p[1];
	for (auto &t : DSPTypes)
		if (type == t.type && family == t.family) return new DSPFilterEffect(t.make);
	TVPThrowExceptionMessage((TJS_W("WaveDSPFilter: unknown filter ") + type + TJS_W("_") + family).c_str());
	return nullptr;
}

//---------------------------------------------------------------------------
// DelayEffect(time_ms, feedback, mix, extend_ms=1000): feedback echo,
// dry/wet crossfaded like STK's effects.
//---------------------------------------------------------------------------
struct DelayEffect : Effect {
	double timeMs, feedback, mix, extendMs;
	std::vector<float> line;
	size_t pos = 0;

	DelayEffect(tjs_int n, tTJSVariant **p)
		: timeMs(arg(n, p, 0, 300)), feedback(arg(n, p, 1, 0.5)), mix(arg(n, p, 2, 0.5)), extendMs(arg(n, p, 3, 1000)) {}

	double tailMs() const override { return extendMs; }
	void prepare(int rate, int channels) override {
		size_t frames = std::max<size_t>(1, (size_t)std::lround(timeMs * rate / 1000));
		line.assign(frames * channels, 0.f);
		pos = 0;
	}
	void reset() override { std::fill(line.begin(), line.end(), 0.f); pos = 0; }
	void process(float *x, tjs_uint n, int ch) override {
		const float fb = (float)feedback, wet = (float)mix, dry = 1.f - wet;
		for (tjs_uint i = 0; i < n * ch; i++) {
			float d = line[pos];
			line[pos] = x[i] + fb * d;
			x[i] = dry * x[i] + wet * d;
			if (++pos == line.size()) pos = 0;
		}
	}
};

//---------------------------------------------------------------------------
// StkFreeVerb(mix=.75, roomsize=.75, damping=.25, width=1, mode=0, extend_ms=1000)
// Jezar's Freeverb with the STK FreeVerb parameter mapping (same defaults).
//---------------------------------------------------------------------------
struct FreeVerbEffect : Effect {
	struct Comb {
		std::vector<float> buf; size_t i = 0; float store = 0;
		float tick(float in, float feedback, float damp) {
			float out = buf[i];
			store = out * (1 - damp) + store * damp;
			buf[i] = in + store * feedback;
			if (++i == buf.size()) i = 0;
			return out;
		}
	};
	struct AllPass {
		std::vector<float> buf; size_t i = 0;
		float tick(float in) {
			float b = buf[i];
			buf[i] = in + b * 0.5f;
			if (++i == buf.size()) i = 0;
			return b - in;
		}
	};
	static constexpr int CombTuning[8] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
	static constexpr int AllPassTuning[4] = {556, 441, 341, 225};
	static constexpr int StereoSpread = 23;

	double extendMs;
	float wet1, wet2, dry, room, damp, gain;
	Comb comb[2][8];
	AllPass allpass[2][4];

	FreeVerbEffect(tjs_int n, tTJSVariant **p) {
		double mix = arg(n, p, 0, 0.75), roomsize = arg(n, p, 1, 0.75), damping = arg(n, p, 2, 0.25);
		double width = arg(n, p, 3, 1.0);
		bool frozen = arg(n, p, 4, 0) != 0;
		extendMs = arg(n, p, 5, 1000);
		// STK FreeVerb::update(): scaleWet 3, scaleDry 2, L1-normalised
		double w = 3 * mix, d = 2 * (1 - mix);
		w /= (w + d);
		d /= (w + d);
		wet1 = (float)(w * (width / 2 + 0.5));
		wet2 = (float)(w * (1 - width) / 2);
		dry = (float)d;
		room = frozen ? 1.f : (float)(roomsize * 0.28 + 0.7);
		damp = frozen ? 0.f : (float)(damping * 0.4);
		gain = frozen ? 0.f : 0.015f;
	}
	double tailMs() const override { return extendMs; }
	void prepare(int rate, int) override {
		double scale = rate / 44100.0;
		for (int s = 0; s < 2; s++) {
			for (int k = 0; k < 8; k++)
				comb[s][k].buf.assign(std::max(1L, std::lround((CombTuning[k] + s * StereoSpread) * scale)), 0.f);
			for (int k = 0; k < 4; k++)
				allpass[s][k].buf.assign(std::max(1L, std::lround((AllPassTuning[k] + s * StereoSpread) * scale)), 0.f);
		}
		reset();
	}
	void reset() override {
		for (auto &side : comb) for (auto &c : side) { std::fill(c.buf.begin(), c.buf.end(), 0.f); c.i = 0; c.store = 0; }
		for (auto &side : allpass) for (auto &a : side) { std::fill(a.buf.begin(), a.buf.end(), 0.f); a.i = 0; }
	}
	void process(float *x, tjs_uint n, int ch) override {
		for (tjs_uint f = 0; f < n; f++, x += ch) {
			float inL = x[0], inR = ch > 1 ? x[1] : x[0];
			float in = (inL + inR) * gain, out[2] = {0, 0};
			for (int s = 0; s < 2; s++) {
				for (auto &c : comb[s]) out[s] += c.tick(in, room, damp);
				for (auto &a : allpass[s]) out[s] = a.tick(out[s]);
			}
			float l = out[0] * wet1 + out[1] * wet2 + inL * dry;
			float r = out[1] * wet1 + out[0] * wet2 + inR * dry;
			if (ch == 1) x[0] = (l + r) * 0.5f;
			else x[0] = l, x[1] = r; // channels beyond 2 pass through dry
		}
	}
};
constexpr int FreeVerbEffect::CombTuning[8];
constexpr int FreeVerbEffect::AllPassTuning[4];

//---------------------------------------------------------------------------
// CompatWaveFilter: the native instance shared by every class above
//---------------------------------------------------------------------------
typedef Effect *(*EffectFactory)(tjs_int numparams, tTJSVariant **param);
tjs_int32 FilterClassID = -1;

class CompatWaveFilter : public tTJSNativeInstance, public iTVPBasicWaveFilter, public tTVPSampleAndLabelSource
{
	EffectFactory Factory;
	std::unique_ptr<Effect> Fx;
	tTVPSampleAndLabelSource *Source = nullptr;
	tTVPWaveFormat InputFormat, OutputFormat;
	std::vector<char> Convert;
	bool Ended = false;
	tjs_int64 TailLeft = 0, LastPos = 0;

public:
	explicit CompatWaveFilter(EffectFactory f) : Factory(f) {}
	tjs_error TJS_INTF_METHOD Construct(tjs_int n, tTJSVariant **p, iTJSDispatch2 *) override {
		Fx.reset(Factory(n, p));
		return TJS_S_OK;
	}
	Effect &fx() { return *Fx; }

	tTVPSampleAndLabelSource *Recreate(tTVPSampleAndLabelSource *source) override {
		if (Source) TVPThrowExceptionMessage(TVPCannotConnectMultipleWaveSoundBufferAtOnce);
		Source = source;
		InputFormat = OutputFormat = source->GetFormat();
		OutputFormat.IsFloat = true;
		OutputFormat.BitsPerSample = 32;
		OutputFormat.BytesPerSample = 4;
		Fx->prepare(InputFormat.SamplesPerSec, InputFormat.Channels);
		Reset();
		return this;
	}
	void Clear() override { Source = nullptr; Convert.clear(); }
	void Update() override {}
	void Reset() override { Fx->reset(); Ended = false; TailLeft = LastPos = 0; }

	void Decode(void *dest, tjs_uint samples, tjs_uint &written, tTVPWaveSegmentQueue &segments) override {
		const int ch = InputFormat.Channels;
		float *out = (float *)dest;
		written = 0;
		if (!Ended) {
			if (InputFormat.IsFloat && InputFormat.BytesPerSample == 4) {
				Source->Decode(out, samples, written, segments);
			} else {
				Convert.resize((size_t)samples * InputFormat.BytesPerSample * ch);
				Source->Decode(Convert.data(), samples, written, segments);
				TVPConvertPCMToFloat(out, Convert.data(), InputFormat, written);
			}
			if (!segments.GetSegments().empty()) {
				const tTVPWaveSegment &s = segments.GetSegments().back();
				LastPos = s.Start + s.Length;
			}
			if (written < samples) {
				Ended = true;
				TailLeft = (tjs_int64)(Fx->tailMs() * InputFormat.SamplesPerSec / 1000);
			}
		}
		if (Ended && TailLeft > 0 && written < samples) {
			// keep the echo/reverb ringing on silence; the extra frames map to
			// no source samples, so positions and labels stay where they ended
			tjs_uint extra = (tjs_uint)std::min<tjs_int64>(samples - written, TailLeft);
			std::fill(out + (size_t)written * ch, out + (size_t)(written + extra) * ch, 0.f);
			segments.Enqueue(tTVPWaveSegment(LastPos, 0, extra));
			TailLeft -= extra;
			written += extra;
		}
		Fx->process(out, written, ch);
	}
	const tTVPWaveFormat &GetFormat() const override { return OutputFormat; }
};

CompatWaveFilter *instance(iTJSDispatch2 *objthis)
{
	CompatWaveFilter *self = nullptr;
	if (!objthis || TJS_FAILED(objthis->NativeInstanceSupport(TJS_NIS_GETINSTANCE, FilterClassID, (iTJSNativeInstance **)&self)))
		TVPThrowExceptionMessage(TJS_W("not a wave filter instance"));
	return self;
}
tjs_error TJS_INTF_METHOD FilterConstructor(tTJSVariant *, tjs_int n, tTJSVariant **p, iTJSDispatch2 *objthis)
{
	return instance(objthis)->Construct(n, p, objthis);
}
tjs_error TJS_INTF_METHOD FilterFinalize(tTJSVariant *, tjs_int, tTJSVariant **, iTJSDispatch2 *) { return TJS_S_OK; }
tjs_error TJS_INTF_METHOD FilterSetParams(tTJSVariant *, tjs_int n, tTJSVariant **p, iTJSDispatch2 *objthis)
{
	instance(objthis)->fx().setParams(n, p);
	return TJS_S_OK;
}
tjs_error TJS_INTF_METHOD FilterInterface(tTJSVariant *result, iTJSDispatch2 *objthis)
{
	if (result) *result = reinterpret_cast<tjs_int64>((iTVPBasicWaveFilter *)instance(objthis));
	return TJS_S_OK;
}
tjs_error TJS_INTF_METHOD DenySet(const tTJSVariant *, iTJSDispatch2 *) { return TJS_E_ACCESSDENYED; }

class FilterClass : public tTJSNativeClass
{
	EffectFactory Factory;
public:
	FilterClass(const tjs_char *name, EffectFactory f) : tTJSNativeClass(name), Factory(f) {
		if (FilterClassID < 0) FilterClassID = TJSRegisterNativeClass(TJS_W("CompatWaveFilter"));
		TJSNativeClassSetClassID(this, FilterClassID);
		TJSNativeClassRegisterNCM(this, name, TJSCreateNativeClassConstructor(FilterConstructor), name, nitMethod);
		TJSNativeClassRegisterNCM(this, TJS_W("finalize"), TJSCreateNativeClassMethod(FilterFinalize), name, nitMethod);
		TJSNativeClassRegisterNCM(this, TJS_W("setParams"), TJSCreateNativeClassMethod(FilterSetParams), name, nitMethod);
		TJSNativeClassRegisterNCM(this, TJS_W("interface"), TJSCreateNativeClassProperty(FilterInterface, DenySet), name, nitProperty);
	}
	iTJSNativeInstance *CreateNativeInstance() override { return new CompatWaveFilter(Factory); }
};

template <class T> Effect *make(tjs_int n, tTJSVariant **p) { return new T(n, p); }

// Installed after the core classes: adds the filters as static members of
// WaveSoundBuffer (like WaveSoundBuffer.PhaseVocoder).
iTJSDispatch2 *InstallWaveFilters(iTJSDispatch2 *global)
{
	tTJSVariant wsbv;
	global->PropGet(0, TJS_W("WaveSoundBuffer"), nullptr, &wsbv, global);
	iTJSDispatch2 *wsb = wsbv.AsObjectNoAddRef();
	const struct { const tjs_char *name; EffectFactory f; } classes[] = {
		{TJS_W("WaveDSPFilter"), makeDSPFilter},
		{TJS_W("DelayEffect"), make<DelayEffect>},
		{TJS_W("StkFreeVerb"), make<FreeVerbEffect>},
	};
	for (auto &c : classes) {
		iTJSDispatch2 *cls = new FilterClass(c.name, c.f);
		tTJSVariant v(cls, nullptr);
		cls->Release();
		wsb->PropSet(TJS_MEMBERENSURE | TJS_IGNOREPROP | TJS_STATICMEMBER, c.name, nullptr, &v, wsb);
	}
	return TJSCreateDictionaryObject(); // global.CompatWaveFilters: placeholder required by tTVPAtInstallClass
}

} // namespace

// CompatNative.waveFilterGain(filter, hz, rate=44100): amplitude gain of an
// unattached filter for a sine (test hook; resets the filter's state).
COMPAT_NATIVE(waveFilterGain)
{
	if (numparams < 2 || param[0]->Type() != tvtObject) return TJS_E_BADPARAMCOUNT;
	Effect &fx = instance(param[0]->AsObjectNoAddRef())->fx();
	const double hz = *param[1], rate = numparams > 2 ? (double)*param[2] : 44100.0;
	const tjs_uint n = (tjs_uint)rate; // one second; measure the second half
	std::vector<float> x(n);
	for (tjs_uint i = 0; i < n; i++) x[i] = (float)std::sin(2 * M_PI * hz * i / rate);
	fx.prepare((int)rate, 1);
	fx.process(x.data(), n, 1);
	double sum = 0;
	for (tjs_uint i = n / 2; i < n; i++) sum += (double)x[i] * x[i];
	if (result) *result = std::sqrt(sum / (n - n / 2) * 2);
	return TJS_S_OK;
}

static tTVPAtInstallClass CompatInstallWaveFilters(TJS_W("CompatWaveFilters"), InstallWaveFilters, TJS_W("WaveSoundBuffer"));
