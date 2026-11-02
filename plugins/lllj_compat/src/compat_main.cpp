// LLLJ compat layer entry: registers the LLLJNative class and runs the
// embedded TJS prelude (prelude.tjs) once the VM's core classes exist.
#include "tjsCommHead.h"
#include "tjsNative.h"
#include "tjsDictionary.h"
#include "ScriptMgnIntf.h"
#include "MsgIntf.h"
#include "DebugIntf.h"
#include "Extension.h"
#include "SysInitIntf.h"
#include "tjsDebug.h"
#include "lllj_compat.h"

#include "lllj_prelude.inc"

static std::vector<LLLJNativeMethod> &LLLJMethods()
{
	static std::vector<LLLJNativeMethod> v;
	return v;
}

LLLJRegisterMethod::LLLJRegisterMethod(const tjs_char *name, LLLJNativeFunc fn)
{
	LLLJMethods().push_back({name, fn});
}

namespace {

class tFuncObj : public tTJSDispatch
{
	LLLJNativeFunc Fn;
public:
	explicit tFuncObj(LLLJNativeFunc fn) : Fn(fn) {}
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
			TVPThrowExceptionMessage((ttstr(TJS_W("lllj native: ")) + ttstr(e.what())).c_str());
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

iTJSDispatch2 *CreateLLLJNative(iTJSDispatch2 *global)
{
	// Release builds do not create the optional TJS stack tracer. Enable it
	// before entering any prelude script so trace push/pop stays balanced.
	// This opt-in reference is independent of the VM's full debug mode.
	tTJSVariant traceOption;
	if (TVPGetCommandLine(TJS_W("-lllj_paths"), &traceOption) && ttstr(traceOption) == TJS_W("yes")) {
		struct TraceLifetime {
			TraceLifetime() { TJSAddRefStackTracer(); }
			~TraceLifetime() { TJSReleaseStackTracer(); }
		};
		static TraceLifetime traceLifetime;
	}
	iTJSDispatch2 *obj = TJSCreateDictionaryObject();
	for (auto &m : LLLJMethods()) {
		iTJSDispatch2 *f = new tFuncObj(m.fn);
		tTJSVariant v(f, f);
		f->Release();
		obj->PropSet(TJS_MEMBERENSURE, m.name, nullptr, &v, obj);
	}
	{
		tTJSVariant v(obj, obj);
		global->PropSet(TJS_MEMBERENSURE, TJS_W("LLLJNative"), nullptr, &v, global);
	}
	try {
		TVPExecuteScript(ttstr((const tjs_char *)LLLJ_PRELUDE), TJS_W("lllj_prelude.tjs"), 0);
		TVPAddImportantLog(TJS_W("(lllj) compat prelude installed"));
	} catch (eTJSScriptError &e) {
		TVPAddImportantLog(ttstr(TJS_W("(lllj) prelude error: ")) + e.GetMessage() +
			TJS_W(" at line ") + ttstr((tjs_int)e.GetSourceLine()));
	} catch (eTJS &e) {
		TVPAddImportantLog(ttstr(TJS_W("(lllj) prelude error: ")) + e.GetMessage());
	}
	return obj; // registered again as global.LLLJNative by the caller
}

} // namespace

static tTVPAtInstallClass TVPInstallLLLJNative(TJS_W("LLLJNative"), CreateLLLJNative, TJS_W(""));

// ported native classes (krkr-ns): PSBFile, TextRenderBase
#include "PsbFilePlugin.h"
#include "TextRenderBase.h"
static iTJSDispatch2 *LLLJCreatePSBFile(iTJSDispatch2 *) { return TVPCreateNativeClass_PSBFile(); }
static iTJSDispatch2 *LLLJCreateTextRenderBase(iTJSDispatch2 *) { return TVPCreateNativeClass_TextRenderBase(); }
static tTVPAtInstallClass TVPInstallLLLJPSBFile(TJS_W("PSBFile"), LLLJCreatePSBFile, TJS_W(""));
static tTVPAtInstallClass TVPInstallLLLJTextRenderBase(TJS_W("TextRenderBase"), LLLJCreateTextRenderBase, TJS_W(""));

// LLLJNative.log(msg): always-on important log line (game scripts nullify Debug.message)
LLLJ_NATIVE(log)
{
	if (numparams >= 1) TVPAddImportantLog(ttstr(*param[0]));
	return TJS_S_OK;
}

// LLLJNative.attachMembers(src, dst[, overwrite=false])
//   Copies every non-static member (methods AND properties) of src (usually a
//   TJS class object) into dst (usually a native class object such as Window or
//   Layer) with objthis cleared, so instances created later get them bound to
//   themselves -- the same mechanism ncbind's NCB_ATTACH_CLASS relies on.
//   Returns the number of members attached.
namespace {
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
			if (name == TJS_W("finalize") || name.StartsWith(TJS_W("LLLJ"))) {
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

LLLJ_NATIVE(attachMembers)
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
