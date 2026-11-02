# webp

WebP images stored under another extension (patch packs ship WebP files named
`*.png`). Registered as an engine graphic sniffer (`EngineHooks.h`): the PNG
loader hands the stream over when it starts with `RIFF....WEBP`.
