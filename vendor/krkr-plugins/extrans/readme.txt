Title: extrans plugin
Author: W.Dee <dee@kikyou.info>（吉里吉里本体作者）

●これはなに？

吉里吉里Z に追加のトランジション（画面切り替え効果）を提供するプラグインです。
以下の 7 種類のトランジションを登録します。

  wave         : ラスタスクロールによる波状のトランジション
  mosaic       : モザイク状に切り替わるトランジション
  turn         : ページをめくるようなトランジション
  rotatezoom   : 回転・ズームしながら切り替わるトランジション
  rotatevanish : 回転しながら消えていくトランジション
  rotateswap   : 回転しながら入れ替わるトランジション
  ripple       : 波紋状に広がるトランジション

いずれも吉里吉里2 時代の追加トランジションプラグインを、吉里吉里Z の全ビルド
バリアント（WIN / SDL / LIB）でビルドできるように整備したものです。トランジション
ハンドラプロバイダ機構（TVPAddTransHandlerProvider / common/visual/TransIntf）は
全バリアント共通のため、generic（SDL）版でも動作します。

●使い方

manual.tjs 参照。
Layer.beginTransition（または KAG の trans タグ）でトランジション名とオプションを
指定して使用します。共通オプション time（所要時間, ミリ秒）は必須です。

●コンパイル時の注意

特別な外部ライブラリは不要です。
ripple の MMX/EMMX インラインアセンブラ版は 32bit x86（_M_IX86）専用で、x64 など
それ以外では C 参照版が使用されます。

●ライセンス

作者・ライセンスともに吉里吉里本体に準拠します。
Copyright (C) 2000-2009 W.Dee <dee@kikyou.info> and contributors.
詳細は吉里吉里本体のライセンス文書を参照してください。
