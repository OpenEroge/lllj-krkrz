#include "ncbind.hpp"

typedef unsigned char BYTE;
typedef unsigned short WORD;


#include "LayerExImage.h"

// ----------------------------------- クラスの登録

NCB_GET_INSTANCE_HOOK(layerExImage)
{
	// インスタンスゲッタ
	NCB_INSTANCE_GETTER(objthis) { // objthis を iTJSDispatch2* 型の引数とする
		ClassT* obj = GetNativeInstance(objthis);	// ネイティブインスタンスポインタ取得
		if (!obj) {
			obj = new ClassT(objthis);				// ない場合は生成する
			SetNativeInstance(objthis, obj);		// objthis に obj をネイティブインスタンスとして登録する
		}
		obj->reset();
		return obj;
	}
	// デストラクタ（実際のメソッドが呼ばれた後に呼ばれる）
	~NCB_GET_INSTANCE_HOOK_CLASS () {
	}
};


// フックつきアタッチ
NCB_ATTACH_CLASS_WITH_HOOK(layerExImage, Layer) {
	NCB_METHOD(light);
	NCB_METHOD(colorize);
	NCB_METHOD(modulate);
	NCB_METHOD(noise);
	NCB_METHOD(generateWhiteNoise);
	NCB_METHOD(gaussianBlur);
}

// 同梱コンポーネント (CxImage) のライセンスを本体収集機構へ登録
// (LicensesGen.cpp = licenses/manifest.json から生成)
extern void RegisterLayerExImageLicenses();
NCB_PRE_REGIST_CALLBACK(RegisterLayerExImageLicenses);
