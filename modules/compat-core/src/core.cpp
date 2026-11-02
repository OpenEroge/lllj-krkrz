// compat-core: collects what the modules registered (compat.h) and installs
// it when the engine installs the "CompatNative" class: init functions first,
// then the global CompatNative object, then the TJS preludes.
#include "tjsCommHead.h"
#include "tjsNative.h"
#include "tjsDictionary.h"
#include "ScriptMgnIntf.h"
#include "MsgIntf.h"
#include "DebugIntf.h"
#include "SysInitIntf.h"
#include "Extension.h"
#include "compat/compat.h"
#include <algorithm>
#include <string>
#include <tuple>
#include <vector>

namespace {

struct Native { const tjs_char *name; compat::NativeFunc fn; };
struct Init { int order; std::string name; void (*fn)(); };
struct Prelude { int order; std::string name; const char16_t *text; };

// function-local statics: registration happens during static initialisation
std::vector<Native> &Natives() { static std::vector<Native> v; return v; }
std::vector<Init> &Inits() { static std::vector<Init> v; return v; }
std::vector<Prelude> &Preludes() { static std::vector<Prelude> v; return v; }

template <typename T> void SortByOrder(std::vector<T> &v)
{
	std::stable_sort(v.begin(), v.end(), [](const T &a, const T &b) {
		return std::tie(a.order, a.name) < std::tie(b.order, b.name);
	});
}

// A TJS function object around a NativeFunc.
class tFuncObj : public tTJSDispatch
{
	compat::NativeFunc Fn;
public:
	explicit tFuncObj(compat::NativeFunc fn) : Fn(fn) {}
	tjs_error TJS_INTF_METHOD FuncCall(tjs_uint32 flag, const tjs_char *membername,
		tjs_uint32 *hint, tTJSVariant *result, tjs_int numparams,
		tTJSVariant **param, iTJSDispatch2 *objthis) override
	{
		if (membername) return TJS_E_MEMBERNOTFOUND;
		try {
			return Fn(result, numparams, param, objthis);
		} catch (const eTJS &) {
			throw;
		} catch (const std::exception &e) {
			TVPThrowExceptionMessage((ttstr(TJS_W("compat native: ")) + ttstr(e.what())).c_str());
		}
		return TJS_E_FAIL;
	}
	tjs_error TJS_INTF_METHOD IsInstanceOf(tjs_uint32 flag, const tjs_char *membername,
		tjs_uint32 *hint, const tjs_char *classname, iTJSDispatch2 *objthis) override
	{
		if (!membername && classname && !TJS_strcmp(classname, TJS_W("Function")))
			return TJS_S_TRUE;
		return tTJSDispatch::IsInstanceOf(flag, membername, hint, classname, objthis);
	}
};

void RunPrelude(const Prelude &p)
{
	const ttstr name(p.name.c_str());
	try {
		TVPExecuteScript(ttstr(reinterpret_cast<const tjs_char *>(p.text)), name, 0);
	} catch (eTJSScriptError &e) {
		compat::Log(TJS_W("prelude error in ") + name + TJS_W(": ") + e.GetMessage() +
			TJS_W(" at line ") + ttstr((tjs_int)e.GetSourceLine()));
	} catch (eTJS &e) {
		compat::Log(TJS_W("prelude error in ") + name + TJS_W(": ") + e.GetMessage());
	}
}

iTJSDispatch2 *CreateCompatNative(iTJSDispatch2 *global)
{
	SortByOrder(Inits());
	for (const auto &i : Inits()) i.fn();

	iTJSDispatch2 *obj = TJSCreateDictionaryObject();
	for (const auto &m : Natives()) {
		iTJSDispatch2 *f = new tFuncObj(m.fn);
		tTJSVariant v(f, f);
		f->Release();
		obj->PropSet(TJS_MEMBERENSURE, m.name, nullptr, &v, obj);
	}
	{
		// preludes use it before the engine registers the returned object
		tTJSVariant v(obj, obj);
		global->PropSet(TJS_MEMBERENSURE, TJS_W("CompatNative"), nullptr, &v, global);
	}

	SortByOrder(Preludes());
	for (const auto &p : Preludes()) RunPrelude(p);
	compat::Log(ttstr((tjs_int)Preludes().size()) + TJS_W(" preludes installed"));
	return obj;
}

} // namespace

static tTVPAtInstallClass TVPInstallCompatNative(TJS_W("CompatNative"), CreateCompatNative, TJS_W(""));

namespace compat {

RegisterNative::RegisterNative(const tjs_char *name, NativeFunc fn) { Natives().push_back({name, fn}); }
RegisterInit::RegisterInit(int order, const char *name, void (*fn)()) { Inits().push_back({order, name, fn}); }
RegisterPrelude::RegisterPrelude(int order, const char *name, const char16_t *text)
{
	Preludes().push_back({order, name, text});
}

void Log(const ttstr &message) { TVPAddImportantLog(TJS_W("(compat) ") + message); }

bool Option(const tjs_char *name, ttstr &value)
{
	tTJSVariant v;
	if (!TVPGetCommandLine(name, &v)) return false;
	value = ttstr(v);
	return true;
}

bool OptionEnabled(const tjs_char *name)
{
	ttstr v;
	return Option(name, v) && (v == TJS_W("yes") || v == TJS_W("1") || v == TJS_W("true"));
}

} // namespace compat

// CompatNative.log(msg): always-on log line, without the "(compat) " prefix
COMPAT_NATIVE(log)
{
	if (numparams >= 1) TVPAddImportantLog(ttstr(*param[0]));
	return TJS_S_OK;
}

// CompatNative.attachMembers(src, dst[, overwrite=false])
//   Copies every non-static member (methods AND properties) of src (usually a
//   TJS class object) into dst (usually a native class object such as Window or
//   Layer) with objthis cleared, so instances created later get them bound to
//   themselves -- the same mechanism ncbind's NCB_ATTACH_CLASS relies on.
//   By convention src is a helper class named "...Members"; its constructor
//   (the member with that name) and "finalize" are skipped.
//   Returns the number of members attached.
namespace {
bool IsMembersClassName(const ttstr &name)
{
	static const ttstr suffix(TJS_W("Members"));
	const tjs_int n = name.GetLen(), k = suffix.GetLen();
	return n >= k && !TJS_strcmp(name.c_str() + (n - k), suffix.c_str());
}
class tAttachCallback : public tTJSDispatch
{
public:
	iTJSDispatch2 *Dest = nullptr;
	bool Overwrite = false;
	tjs_int Count = 0;
	tjs_error TJS_INTF_METHOD FuncCall(tjs_uint32 flag, const tjs_char *membername,
		tjs_uint32 *hint, tTJSVariant *result, tjs_int numparams, tTJSVariant **param,
		iTJSDispatch2 *objthis) override
	{
		if (numparams < 3) return TJS_E_BADPARAMCOUNT;
		tjs_uint32 flags = (tjs_int)*param[1];
		if (!(flags & TJS_STATICMEMBER)) {
			ttstr name = *param[0];
			if (name == TJS_W("finalize") || IsMembersClassName(name)) {
				if (result) *result = (tjs_int)1;
				return TJS_S_OK;
			}
			if (!Overwrite) {
				tTJSVariant cur;
				tjs_error er = Dest->PropGet(TJS_MEMBERMUSTEXIST | TJS_IGNOREPROP, name.c_str(), nullptr, &cur, Dest);
				if (TJS_SUCCEEDED(er)) { if (result) *result = (tjs_int)1; return TJS_S_OK; }
			}
			tTJSVariant val = *param[2];
			if (val.Type() == tvtObject) val.ChangeClosureObjThis(nullptr);
			Dest->PropSet(TJS_MEMBERENSURE | TJS_IGNOREPROP, name.c_str(), nullptr, &val, Dest);
			Count++;
		}
		if (result) *result = (tjs_int)1; // continue
		return TJS_S_OK;
	}
};
} // namespace

COMPAT_NATIVE(attachMembers)
{
	if (numparams < 2) return TJS_E_BADPARAMCOUNT;
	iTJSDispatch2 *src = param[0]->AsObjectNoAddRef();
	iTJSDispatch2 *dst = param[1]->AsObjectNoAddRef();
	tAttachCallback *cb = new tAttachCallback();
	cb->Dest = dst;
	cb->Overwrite = numparams >= 3 && param[2]->operator bool();
	tTJSVariantClosure clo(cb, nullptr);
	src->EnumMembers(TJS_IGNOREPROP, &clo, src);
	tjs_int n = cb->Count;
	cb->Release();
	if (result) *result = n;
	return TJS_S_OK;
}
