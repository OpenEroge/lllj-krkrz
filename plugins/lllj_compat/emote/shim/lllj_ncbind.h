#pragma once
// LLLJ: krkrz_dev's ncbind compiled against the engine's internal headers.
//
// The E-mote runtime needs engine internals (tTJSNI_Layer, tTVPMemoryStream...)
// so it is built into the executable like the rest of lllj_compat, but its
// class table stays in ncbind form. ncbind.hpp normally includes tp_stub.h
// (plugin ABI types in namespace krkrz_plugin); here the same names come from
// the engine headers instead, as krkr-ns' PluginStub.h does.
//
// TVP_STATIC_PLUGIN + TVP_PLUGIN_NAME make ncbind rename its auto-register
// lists per plugin, so these registrations stay separate from the static
// krkrz_dev plugins linked into the same executable.
#define TVP_STATIC_PLUGIN
#define TVP_PLUGIN_NAME lllj_emote

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
#define ncbAncestorAttacher lllj_emote_ncbAncestorAttacher
#define ncbArrayAccessor lllj_emote_ncbArrayAccessor
#define ncbAttachTJS lllj_emote_ncbAttachTJS
#define ncbClassInfo lllj_emote_ncbClassInfo
#define ncbCtorSelect lllj_emote_ncbCtorSelect
#define ncbDictionaryAccessor lllj_emote_ncbDictionaryAccessor
#define ncbDispatchConvertor lllj_emote_ncbDispatchConvertor
#define ncbFactorySelect lllj_emote_ncbFactorySelect
#define ncbIMethodObject lllj_emote_ncbIMethodObject
#define ncbInstanceAdaptor lllj_emote_ncbInstanceAdaptor
#define ncbNarrowCharConvertor lllj_emote_ncbNarrowCharConvertor
#define ncbNativeClassConstructor lllj_emote_ncbNativeClassConstructor
#define ncbNativeClassFactory lllj_emote_ncbNativeClassFactory
#define ncbNativeClassMethod lllj_emote_ncbNativeClassMethod
#define ncbNativeClassMethodBase lllj_emote_ncbNativeClassMethodBase
#define ncbNativeClassProperty lllj_emote_ncbNativeClassProperty
#define ncbNativeFunctionAutoRegisterTempl lllj_emote_ncbNativeFunctionAutoRegisterTempl
#define ncbNativeObjectBoxing lllj_emote_ncbNativeObjectBoxing
#define ncbNativeSubClassConstructor lllj_emote_ncbNativeSubClassConstructor
#define ncbNativeSubClassFactory lllj_emote_ncbNativeSubClassFactory
#define ncbPropAccessor lllj_emote_ncbPropAccessor
#define ncbRawCallbackMethod lllj_emote_ncbRawCallbackMethod
#define ncbRawCallbackProperty lllj_emote_ncbRawCallbackProperty
#define ncbRawCallbackPropertySelector lllj_emote_ncbRawCallbackPropertySelector
#define ncbRegistClass lllj_emote_ncbRegistClass
#define ncbRegistNativeClass lllj_emote_ncbRegistNativeClass
#define ncbRegistNativeClassBase lllj_emote_ncbRegistNativeClassBase
#define ncbRegistSubClass lllj_emote_ncbRegistSubClass
#define ncbStrictResultConvertor lllj_emote_ncbStrictResultConvertor
#define ncbStringConvertor lllj_emote_ncbStringConvertor
#define ncbSubClassCheck lllj_emote_ncbSubClassCheck
#define ncbSubClassItem lllj_emote_ncbSubClassItem
#define ncbSubClassMemberCopy lllj_emote_ncbSubClassMemberCopy
#define ncbSubClassOf lllj_emote_ncbSubClassOf
#define ncbTypeConvertor lllj_emote_ncbTypeConvertor
#define ncbTypedefs lllj_emote_ncbTypedefs
#define ncbWideCharConvertor lllj_emote_ncbWideCharConvertor

// Skip the real tp_stub.h only for ncbind itself. Engine headers also test
// __TP_STUB_H__ (e.g. tjsNative.h), so it must not be visible to them.
#define __TP_STUB_H__
#include "ncbind/ncbind.hpp"
#undef __TP_STUB_H__
