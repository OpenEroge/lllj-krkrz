#pragma once
// LLLJ: krkrz_dev's ncbind compiled against the engine's internal headers.
//
// The E-mote runtime and layerExDraw need engine internals (tTJSNI_Layer,
// tTVPMemoryStream...) so they are built into the executable like the rest of
// lllj_compat, but their class tables stay in ncbind form. ncbind.hpp normally
// includes tp_stub.h (plugin ABI types in namespace krkrz_plugin); here the same
// names come from the engine headers instead, as krkr-ns' PluginStub.h does.
//
// Each module defines TVP_PLUGIN_NAME to its static plugin name before this
// header (its force-included prefix does so). TVP_STATIC_PLUGIN +
// TVP_PLUGIN_NAME make ncbind rename its auto-register lists per plugin, and
// every other ncbind name below gets the same prefix, so modules and the static
// krkrz_dev plugins linked into the same executable never share a symbol.
// Without a definition the prefix is lllj_emote (the first module).
#ifndef TVP_PLUGIN_NAME
#define TVP_PLUGIN_NAME lllj_emote
#endif
#define LLLJ_NCB_CAT_(a, b) a##b
#define LLLJ_NCB_CAT(a, b) LLLJ_NCB_CAT_(a, b)

#define TVP_STATIC_PLUGIN

#include "tjsCommHead.h"
#include "StorageImpl.h"
#include "tjsNative.h"
#include "ScriptMgnIntf.h"
#include "tjsArray.h"
#include "tjsDictionary.h"
#include "DebugIntf.h"
#include "TextStream.h"
#include "MsgIntf.h"
#include "CharacterSet.h"
#include "PluginImpl.h"

// tjsNative.h only defines tTJSNativeClassForPlugin for tp_stub users; in the
// engine it is private to tjsNative.cpp, so ncbind cannot up-cast it. Give
// ncbind an equivalent class of its own under a distinct name.
class tLLLJNativeClassForPlugin : public tTJSNativeClass
{
	tTJSCreateNativeInstance procCreateNativeInstance;
public:
	tLLLJNativeClassForPlugin(const ttstr &name, tTJSCreateNativeInstance proc)
		: tTJSNativeClass(name), procCreateNativeInstance(proc) {}
protected:
	iTJSNativeInstance *CreateNativeInstance() override { return procCreateNativeInstance(); }
};
inline tLLLJNativeClassForPlugin *LLLJCreateNativeClassForPlugin(const ttstr &name,
	tTJSCreateNativeInstance createinstance)
{
	return new tLLLJNativeClassForPlugin(name, createinstance);
}
#define tTJSNativeClassForPlugin tLLLJNativeClassForPlugin
#define TJSCreateNativeClassForPlugin LLLJCreateNativeClassForPlugin

// The static krkrz_dev plugins in the same executable compile ncbind against
// tp_stub's types; classes whose name does not mangle those types (vtables,
// typeinfo, member functions returning iTJSDispatch2*) would otherwise be
// merged with ours by the linker. ncbind already renames its auto-register
// lists for TVP_STATIC_PLUGIN; give every other ncbind class a private name.
#define ncbAncestorAttacher LLLJ_NCB_CAT(TVP_PLUGIN_NAME, _ncbAncestorAttacher)
#define ncbArrayAccessor LLLJ_NCB_CAT(TVP_PLUGIN_NAME, _ncbArrayAccessor)
#define ncbAttachTJS LLLJ_NCB_CAT(TVP_PLUGIN_NAME, _ncbAttachTJS)
#define ncbAttachTJS2Class LLLJ_NCB_CAT(TVP_PLUGIN_NAME, _ncbAttachTJS2Class)
#define ncbClassInfo LLLJ_NCB_CAT(TVP_PLUGIN_NAME, _ncbClassInfo)
#define ncbCtorSelect LLLJ_NCB_CAT(TVP_PLUGIN_NAME, _ncbCtorSelect)
#define ncbDictionaryAccessor LLLJ_NCB_CAT(TVP_PLUGIN_NAME, _ncbDictionaryAccessor)
#define ncbDispatchConvertor LLLJ_NCB_CAT(TVP_PLUGIN_NAME, _ncbDispatchConvertor)
#define ncbFactorySelect LLLJ_NCB_CAT(TVP_PLUGIN_NAME, _ncbFactorySelect)
#define ncbIMethodObject LLLJ_NCB_CAT(TVP_PLUGIN_NAME, _ncbIMethodObject)
#define ncbInstanceAdaptor LLLJ_NCB_CAT(TVP_PLUGIN_NAME, _ncbInstanceAdaptor)
#define ncbNCharToVariatTo LLLJ_NCB_CAT(TVP_PLUGIN_NAME, _ncbNCharToVariatTo)
#define ncbNarrowCharConvertor LLLJ_NCB_CAT(TVP_PLUGIN_NAME, _ncbNarrowCharConvertor)
#define ncbNativeClassConstructor LLLJ_NCB_CAT(TVP_PLUGIN_NAME, _ncbNativeClassConstructor)
#define ncbNativeClassFactory LLLJ_NCB_CAT(TVP_PLUGIN_NAME, _ncbNativeClassFactory)
#define ncbNativeClassMethod LLLJ_NCB_CAT(TVP_PLUGIN_NAME, _ncbNativeClassMethod)
#define ncbNativeClassMethodBase LLLJ_NCB_CAT(TVP_PLUGIN_NAME, _ncbNativeClassMethodBase)
#define ncbNativeClassProperty LLLJ_NCB_CAT(TVP_PLUGIN_NAME, _ncbNativeClassProperty)
#define ncbNativeFunctionAutoRegisterTempl LLLJ_NCB_CAT(TVP_PLUGIN_NAME, _ncbNativeFunctionAutoRegisterTempl)
#define ncbNativeObjectBoxing LLLJ_NCB_CAT(TVP_PLUGIN_NAME, _ncbNativeObjectBoxing)
#define ncbNativeSubClassConstructor LLLJ_NCB_CAT(TVP_PLUGIN_NAME, _ncbNativeSubClassConstructor)
#define ncbNativeSubClassFactory LLLJ_NCB_CAT(TVP_PLUGIN_NAME, _ncbNativeSubClassFactory)
#define ncbPropAccessor LLLJ_NCB_CAT(TVP_PLUGIN_NAME, _ncbPropAccessor)
#define ncbRawCallbackMethod LLLJ_NCB_CAT(TVP_PLUGIN_NAME, _ncbRawCallbackMethod)
#define ncbRawCallbackProperty LLLJ_NCB_CAT(TVP_PLUGIN_NAME, _ncbRawCallbackProperty)
#define ncbRawCallbackPropertySelector LLLJ_NCB_CAT(TVP_PLUGIN_NAME, _ncbRawCallbackPropertySelector)
#define ncbRegistClass LLLJ_NCB_CAT(TVP_PLUGIN_NAME, _ncbRegistClass)
#define ncbRegistNativeClass LLLJ_NCB_CAT(TVP_PLUGIN_NAME, _ncbRegistNativeClass)
#define ncbRegistNativeClassBase LLLJ_NCB_CAT(TVP_PLUGIN_NAME, _ncbRegistNativeClassBase)
#define ncbRegistSubClass LLLJ_NCB_CAT(TVP_PLUGIN_NAME, _ncbRegistSubClass)
#define ncbStrictResultConvertor LLLJ_NCB_CAT(TVP_PLUGIN_NAME, _ncbStrictResultConvertor)
#define ncbStringConvertor LLLJ_NCB_CAT(TVP_PLUGIN_NAME, _ncbStringConvertor)
#define ncbSubClassAttachAncestors LLLJ_NCB_CAT(TVP_PLUGIN_NAME, _ncbSubClassAttachAncestors)
#define ncbSubClassCheck LLLJ_NCB_CAT(TVP_PLUGIN_NAME, _ncbSubClassCheck)
#define ncbSubClassItem LLLJ_NCB_CAT(TVP_PLUGIN_NAME, _ncbSubClassItem)
#define ncbSubClassMemberCopy LLLJ_NCB_CAT(TVP_PLUGIN_NAME, _ncbSubClassMemberCopy)
#define ncbSubClassOf LLLJ_NCB_CAT(TVP_PLUGIN_NAME, _ncbSubClassOf)
#define ncbToValueConvertor LLLJ_NCB_CAT(TVP_PLUGIN_NAME, _ncbToValueConvertor)
#define ncbToVariantConvertor LLLJ_NCB_CAT(TVP_PLUGIN_NAME, _ncbToVariantConvertor)
#define ncbTypeConvertor LLLJ_NCB_CAT(TVP_PLUGIN_NAME, _ncbTypeConvertor)
#define ncbTypedefs LLLJ_NCB_CAT(TVP_PLUGIN_NAME, _ncbTypedefs)
#define ncbVariatToNChar LLLJ_NCB_CAT(TVP_PLUGIN_NAME, _ncbVariatToNChar)
#define ncbWideCharConvertor LLLJ_NCB_CAT(TVP_PLUGIN_NAME, _ncbWideCharConvertor)

// Skip the real tp_stub.h only for ncbind itself. Engine headers also test
// __TP_STUB_H__ (e.g. tjsNative.h), so it must not be visible to them.
#define __TP_STUB_H__
#include "ncbind/ncbind.hpp"
#undef __TP_STUB_H__
