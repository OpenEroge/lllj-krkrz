#include "tp_stub.h"
#include "simplebinder.hpp"

//#define V2LINK_NEED_HMODULE // V2LinkModuleが必要
//#define V2LINK_USE_V2DETACH // onV2Detach()が必要(globalが取れなかったケースでのunlink処理)
//#define V2LINK_NEED_ZCHECK  // IsKirikiriZ() が必要
//#define V2LINK_USE_KRKRTYPE // IsKirikiri{2,Z}() が必要（NEED_ZCHECKと排他／実装が異なる）

#ifdef TVP_STATIC_PLUGIN

#define EXPORT(hr) static hr STDCALL

#else

#if defined(_MSC_VER)
    #define DLL_EXPORT  __declspec(dllexport)
#else
	#define DLL_EXPORT  __attribute__((visibility("default")))
#endif

#define EXPORT(hr) extern "C" DLL_EXPORT hr STDCALL

#ifdef _WIN32

#ifdef _MSC_VER
# if defined(_M_AMD64) || defined(_M_X64)
#  pragma comment(linker, "/EXPORT:V2Link")
#  pragma comment(linker, "/EXPORT:V2Unlink")
# else
#  pragma comment(linker, "/EXPORT:V2Link=_V2Link@4")
#  pragma comment(linker, "/EXPORT:V2Unlink=_V2Unlink@0")
# endif
#endif
#ifdef __GNUC__
asm (".section .drectve");
# if defined(__x86_64__) || defined(__x86_64)
asm (".ascii \" -export:V2Link=V2Link -export:V2Unlink=V2Unlink\"");
# else
asm (".ascii \" -export:V2Link=V2Link@4 -export:V2Unlink=V2Unlink@0\"");
# endif
#endif

HINSTANCE V2LinkModule = 0;

extern "C"
BOOL WINAPI
DllMain(HINSTANCE hinst, unsigned long reason, void* /*lpReserved*/)
{
	if (reason == DLL_PROCESS_ATTACH) {
		V2LinkModule = hinst;
	}
	return 1;
}

#endif

#endif // TVP_STATIC_PLUGIN


static tjs_int GlobalRefCountAtInit = 0;

#if defined(V2LINK_NEED_ZCHECK) && defined(V2LINK_USE_KRKRTYPE)
#error V2LINK_NEED_ZCHECK and V2LINK_USE_KRKRTYPE defines are exclusive.
#endif

#ifdef V2LINK_NEED_ZCHECK
static bool kirikiriZ = false;
bool IsKirikiriZ() { return kirikiriZ; }
static void CheckKirikiriZ(iTVPFunctionExporter *exporter) {
	static const char *judgeZ = "void ::TVPMIDIOutData(const tjs_uint8 *,int)";
	void *ptr;
	kirikiriZ = !exporter->QueryFunctionsByNarrowString(&judgeZ, &ptr, 1);
}
#endif

#ifdef V2LINK_USE_KRKRTYPE
static struct KirikiriTypeChecker {
	KirikiriTypeChecker() : exporter(0), type(0) {}
	void setup(iTVPFunctionExporter *exporter) { this->exporter = exporter; }
	int getType() {
		if (type == 0) type = GetType(exporter);
		return type;
	}
#if ((V2LINK_USE_KRKRTYPE&2) != 0)
	bool queryFunctions(const char **names, void **functions, tjs_uint count) const {
		return exporter && exporter->QueryFunctionsByNarrowString(names, functions, count);
	}
#endif
private:
	iTVPFunctionExporter *exporter;
	int type; // 0:unchecked, -1:unknown, 1:Z, 2:2
	static int GetType(iTVPFunctionExporter *exporter) {
		if (exporter) {
			const ttstr prefix (TJS_W("bool ::TVPGetFileVersionOf(const "));
			const ttstr postfix(TJS_W(" *,tjs_int &,tjs_int &,tjs_int &,tjs_int &)"));
			if (HasExportedFunction(exporter, ttstr(prefix + TJS_W("wchar_t") + postfix).c_str())) return 1; // KZ
			if (HasExportedFunction(exporter, ttstr(prefix + TJS_W("char")    + postfix).c_str())) return 2; // K2
		}
		return -1;
	}
	static bool HasExportedFunction(iTVPFunctionExporter *exporter, const tjs_char *name, void *ptr = NULL) {
		return exporter->QueryFunctions(&name, &ptr, 1) && ptr;
	}
} KirikiriType;

#if ((V2LINK_USE_KRKRTYPE&2) != 0)
bool TVPQueryFunctions(const char **names, void **functions, tjs_uint count) {
	return KirikiriType.queryFunctions(names, functions, count);
}
#endif

// extern reference from other soruces
bool IsKirikiriZ() { return KirikiriType.getType() == 1; }
bool IsKirikiri2() { return KirikiriType.getType() == 2; }
#endif

extern bool ONV2LINK();
extern bool ONV2UNLINK();
#ifdef V2LINK_USE_V2DETACH
extern void ONV2DETACH();
#endif

EXPORT(HRESULT) V2Link(iTVPFunctionExporter *exporter)
{
#ifdef V2LINK_NEED_ZCHECK
	CheckKirikiriZ(exporter);
#endif
	TVPInitImportStub(exporter);
#ifdef V2LINK_USE_KRKRTYPE
	KirikiriType.setup(exporter);
#endif

	if (!ONV2LINK()) return E_FAIL;

	GlobalRefCountAtInit = TVPPluginGlobalRefCount;
	return S_OK;
}
EXPORT(HRESULT) V2Unlink()
{
	iTJSDispatch2 *global = TVPGetScriptDispatch();
	if (global) {
		global->Release();
		if (TVPPluginGlobalRefCount > GlobalRefCountAtInit ||
			!ONV2UNLINK()) return E_FAIL;
#ifdef V2LINK_USE_V2DETACH
	} else {
		ONV2DETACH();
#endif
	}

	TVPUninitImportStub();
	return S_OK;
}

#ifdef TVP_STATIC_PLUGIN

#if defined(_MSC_VER)
    #define EXPORT_USED __declspec(dllexport)
#else
	#define EXPORT_USED __attribute__((visibility("default"), used))
#endif


// リンク用エントリ関数
// _krkrz_plugin_プロジェクト名 で関数が作られる
extern "C" EXPORT_USED void STDCALL MAKE_FUNC(TVP_PLUGIN_NAME)() {
	static iTVPStaticPlugin plugin;
    plugin.name = XTOSTR(TVP_PLUGIN_NAME);
	plugin.link = (int32_t (STDCALL *)(iTVPFunctionExporter *))V2Link;
	plugin.unlink = (int32_t (STDCALL *)(void))V2Unlink;
	TVPRegisterPlugin(&plugin);
}

#endif
