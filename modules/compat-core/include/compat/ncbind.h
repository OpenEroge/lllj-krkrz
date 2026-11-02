#pragma once
// PORT: the vendored ncbind (vendor/krkr-plugins/ncbind) compiled against the engine's internal headers.
//
// The E-mote runtime and layerExDraw need engine internals (tTJSNI_Layer,
// tTVPMemoryStream...) so they are built into the executable like the rest of
// the compatibility modules, but their class tables stay in ncbind form. ncbind.hpp normally
// includes tp_stub.h (plugin ABI types in namespace krkrz_plugin); here the same
// names come from the engine headers instead, as krkr-ns' PluginStub.h does.
//
// Each module defines TVP_PLUGIN_NAME to its static plugin name before this
// header (its force-included prefix does so). TVP_STATIC_PLUGIN +
// TVP_PLUGIN_NAME make ncbind rename its auto-register lists per plugin, and
// every other ncbind name below gets the same prefix, so modules and the static
// vendored krkrz plugins linked into the same executable never share a symbol.
// Without a definition the prefix is compat_emote (the first module).
#ifndef TVP_PLUGIN_NAME
#define TVP_PLUGIN_NAME compat_emote
#endif
#define COMPAT_NCB_CAT_(a, b) a##b
#define COMPAT_NCB_CAT(a, b) COMPAT_NCB_CAT_(a, b)

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
class tCompatNativeClassForPlugin : public tTJSNativeClass
{
	tTJSCreateNativeInstance procCreateNativeInstance;
public:
	tCompatNativeClassForPlugin(const ttstr &name, tTJSCreateNativeInstance proc)
		: tTJSNativeClass(name), procCreateNativeInstance(proc) {}
protected:
	iTJSNativeInstance *CreateNativeInstance() override { return procCreateNativeInstance(); }
};
inline tCompatNativeClassForPlugin *CompatCreateNativeClassForPlugin(const ttstr &name,
	tTJSCreateNativeInstance createinstance)
{
	return new tCompatNativeClassForPlugin(name, createinstance);
}
#define tTJSNativeClassForPlugin tCompatNativeClassForPlugin
#define TJSCreateNativeClassForPlugin CompatCreateNativeClassForPlugin

// The static vendored krkrz plugins in the same executable compile ncbind against
// tp_stub's types; classes whose name does not mangle those types (vtables,
// typeinfo, member functions returning iTJSDispatch2*) would otherwise be
// merged with ours by the linker. ncbind already renames its auto-register
// lists for TVP_STATIC_PLUGIN; give every other ncbind class a private name.
#define ncbAncestorAttacher COMPAT_NCB_CAT(TVP_PLUGIN_NAME, _ncbAncestorAttacher)
#define ncbArrayAccessor COMPAT_NCB_CAT(TVP_PLUGIN_NAME, _ncbArrayAccessor)
#define ncbAttachTJS COMPAT_NCB_CAT(TVP_PLUGIN_NAME, _ncbAttachTJS)
#define ncbAttachTJS2Class COMPAT_NCB_CAT(TVP_PLUGIN_NAME, _ncbAttachTJS2Class)
#define ncbClassInfo COMPAT_NCB_CAT(TVP_PLUGIN_NAME, _ncbClassInfo)
#define ncbCtorSelect COMPAT_NCB_CAT(TVP_PLUGIN_NAME, _ncbCtorSelect)
#define ncbDictionaryAccessor COMPAT_NCB_CAT(TVP_PLUGIN_NAME, _ncbDictionaryAccessor)
#define ncbDispatchConvertor COMPAT_NCB_CAT(TVP_PLUGIN_NAME, _ncbDispatchConvertor)
#define ncbFactorySelect COMPAT_NCB_CAT(TVP_PLUGIN_NAME, _ncbFactorySelect)
#define ncbIMethodObject COMPAT_NCB_CAT(TVP_PLUGIN_NAME, _ncbIMethodObject)
#define ncbInstanceAdaptor COMPAT_NCB_CAT(TVP_PLUGIN_NAME, _ncbInstanceAdaptor)
#define ncbNCharToVariatTo COMPAT_NCB_CAT(TVP_PLUGIN_NAME, _ncbNCharToVariatTo)
#define ncbNarrowCharConvertor COMPAT_NCB_CAT(TVP_PLUGIN_NAME, _ncbNarrowCharConvertor)
#define ncbNativeClassConstructor COMPAT_NCB_CAT(TVP_PLUGIN_NAME, _ncbNativeClassConstructor)
#define ncbNativeClassFactory COMPAT_NCB_CAT(TVP_PLUGIN_NAME, _ncbNativeClassFactory)
#define ncbNativeClassMethod COMPAT_NCB_CAT(TVP_PLUGIN_NAME, _ncbNativeClassMethod)
#define ncbNativeClassMethodBase COMPAT_NCB_CAT(TVP_PLUGIN_NAME, _ncbNativeClassMethodBase)
#define ncbNativeClassProperty COMPAT_NCB_CAT(TVP_PLUGIN_NAME, _ncbNativeClassProperty)
#define ncbNativeFunctionAutoRegisterTempl COMPAT_NCB_CAT(TVP_PLUGIN_NAME, _ncbNativeFunctionAutoRegisterTempl)
#define ncbNativeObjectBoxing COMPAT_NCB_CAT(TVP_PLUGIN_NAME, _ncbNativeObjectBoxing)
#define ncbNativeSubClassConstructor COMPAT_NCB_CAT(TVP_PLUGIN_NAME, _ncbNativeSubClassConstructor)
#define ncbNativeSubClassFactory COMPAT_NCB_CAT(TVP_PLUGIN_NAME, _ncbNativeSubClassFactory)
#define ncbPropAccessor COMPAT_NCB_CAT(TVP_PLUGIN_NAME, _ncbPropAccessor)
#define ncbRawCallbackMethod COMPAT_NCB_CAT(TVP_PLUGIN_NAME, _ncbRawCallbackMethod)
#define ncbRawCallbackProperty COMPAT_NCB_CAT(TVP_PLUGIN_NAME, _ncbRawCallbackProperty)
#define ncbRawCallbackPropertySelector COMPAT_NCB_CAT(TVP_PLUGIN_NAME, _ncbRawCallbackPropertySelector)
#define ncbRegistClass COMPAT_NCB_CAT(TVP_PLUGIN_NAME, _ncbRegistClass)
#define ncbRegistNativeClass COMPAT_NCB_CAT(TVP_PLUGIN_NAME, _ncbRegistNativeClass)
#define ncbRegistNativeClassBase COMPAT_NCB_CAT(TVP_PLUGIN_NAME, _ncbRegistNativeClassBase)
#define ncbRegistSubClass COMPAT_NCB_CAT(TVP_PLUGIN_NAME, _ncbRegistSubClass)
#define ncbStrictResultConvertor COMPAT_NCB_CAT(TVP_PLUGIN_NAME, _ncbStrictResultConvertor)
#define ncbStringConvertor COMPAT_NCB_CAT(TVP_PLUGIN_NAME, _ncbStringConvertor)
#define ncbSubClassAttachAncestors COMPAT_NCB_CAT(TVP_PLUGIN_NAME, _ncbSubClassAttachAncestors)
#define ncbSubClassCheck COMPAT_NCB_CAT(TVP_PLUGIN_NAME, _ncbSubClassCheck)
#define ncbSubClassItem COMPAT_NCB_CAT(TVP_PLUGIN_NAME, _ncbSubClassItem)
#define ncbSubClassMemberCopy COMPAT_NCB_CAT(TVP_PLUGIN_NAME, _ncbSubClassMemberCopy)
#define ncbSubClassOf COMPAT_NCB_CAT(TVP_PLUGIN_NAME, _ncbSubClassOf)
#define ncbToValueConvertor COMPAT_NCB_CAT(TVP_PLUGIN_NAME, _ncbToValueConvertor)
#define ncbToVariantConvertor COMPAT_NCB_CAT(TVP_PLUGIN_NAME, _ncbToVariantConvertor)
#define ncbTypeConvertor COMPAT_NCB_CAT(TVP_PLUGIN_NAME, _ncbTypeConvertor)
#define ncbTypedefs COMPAT_NCB_CAT(TVP_PLUGIN_NAME, _ncbTypedefs)
#define ncbVariatToNChar COMPAT_NCB_CAT(TVP_PLUGIN_NAME, _ncbVariatToNChar)
#define ncbWideCharConvertor COMPAT_NCB_CAT(TVP_PLUGIN_NAME, _ncbWideCharConvertor)

// Skip the real tp_stub.h only for ncbind itself. Engine headers also test
// __TP_STUB_H__ (e.g. tjsNative.h), so it must not be visible to them.
#define __TP_STUB_H__
#include "ncbind/ncbind.hpp"
#undef __TP_STUB_H__
