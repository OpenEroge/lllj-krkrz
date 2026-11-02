; Loaded only by tools/scenarios.py (emote scenario); never part of normal startup.
*ev
[begintrans]
[msgoff]
[allimage hide delete]
[ev file=&tf.llljEv show]
[endtrans notrans sync]
[s]
*eyecatch
[eval exp="tf.eyecatch='恵凪'"]
[call storage="eyecatch.ks" target=*eyecatch]
[s]
# Transitions (batch 5). The scene's own layers cover fore.base, so the stripe picture is
# a layer above them (tf.llljStripe, shown between begintrans and endtrans): the transition
# runs from the scene to the stripes. モザイク and ターン are keys of the game's table
# (envinit.tjs): mosaic 750 ms, turn 2000 ms. tf.llljMethod / tf.llljTime name any other
# extrans or extNagano method (extNagano is not loaded by the game, so it is linked here).
*trans_mosaic
[eval exp="(function(){ if (typeof tf.llljStripe == 'Object') tf.llljStripe.visible = false; return 1; })()"]
[begintrans]
[eval exp="(function(){ if (typeof tf.llljStripe != 'Object') { var l = new Layer(kag, kag.fore.base); l.setImageSize(1920, 1080); l.setSizeToImageSize(); l.setPos(0, 0); for (var i = 0; i < 16; i++) l.fillRect(i * 120, 0, 120, 1080, (((i & 1) == 1) ? 0xff3a6fa8 : 0xffe8a33d) + i); l.absolute = 100000; tf.llljStripe = l; } tf.llljStripe.visible = true; return 1; })()"]
[endtrans モザイク]
[s]
*trans_turn
[eval exp="(function(){ if (typeof tf.llljStripe == 'Object') tf.llljStripe.visible = false; return 1; })()"]
[begintrans]
[eval exp="(function(){ if (typeof tf.llljStripe != 'Object') { var l = new Layer(kag, kag.fore.base); l.setImageSize(1920, 1080); l.setSizeToImageSize(); l.setPos(0, 0); for (var i = 0; i < 16; i++) l.fillRect(i * 120, 0, 120, 1080, (((i & 1) == 1) ? 0xff3a6fa8 : 0xffe8a33d) + i); l.absolute = 100000; tf.llljStripe = l; } tf.llljStripe.visible = true; return 1; })()"]
[endtrans ターン]
[s]
*trans_method
[eval exp="Plugins.link('extNagano.dll')"]
[backlay]
[eval exp="(function(){ if (typeof tf.llljStripeBack == 'Object') tf.llljStripeBack.visible = false; var l = new Layer(kag, kag.back.base); l.setImageSize(1920, 1080); l.setSizeToImageSize(); l.setPos(0, 0); for (var i = 0; i < 16; i++) l.fillRect(i * 120, 0, 120, 1080, (((i & 1) == 1) ? 0xff3a6fa8 : 0xffe8a33d) + i); l.absolute = 100000; l.visible = true; tf.llljStripeBack = l; return 1; })()"]
[trans method=&tf.llljMethod time=&tf.llljTime]
[wt]
[s]
