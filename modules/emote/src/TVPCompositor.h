#pragma once

#include <cstdint>

// Minimal 2D render-backend contract used by the open-source E-mote
// implementation. KRKR-ns supplies a private GL context plus a CPU fallback;
// the main KiriKiri Layer compositor retains ownership of the SDL renderer.
namespace krkrsdl3
{
class iTVPRenderBackend
{
public:
    virtual ~iTVPRenderBackend() = default;
    virtual void* CreateTarget(int width, int height) = 0;
    // Physical target size can differ from the logical motion canvas.
    virtual bool GetTargetSize(void* target, int& width, int& height) const { return false; }
    virtual void DestroyTarget(void* target) = 0;
    virtual void SetTarget(void* target) = 0;
    virtual void ClearTarget(bool clearColor) = 0;
    virtual uint8_t* LockTarget(void* target, int& pitch) = 0;
    virtual void UnlockTarget(void* target) = 0;
    // Byte order of the pixels LockTarget returns: false RGBA, true BGRA (the
    // Kirikiri layer order, so the layer copy needs no channel swap).
    virtual bool TargetIsBGRA(void* target) const { return false; }
    // The part of the locked pixels that changed since the previous call, as
    // [x0,x1) x [y0,y1); x0 >= x1 means nothing changed. Resets the record.
    // false: not tracked, every pixel may have changed.
    virtual bool TakeChangedRect(void* target, int& x0, int& y0, int& x1, int& y1) { return false; }
    // Asynchronous readback. RequestReadback starts copying what was drawn
    // into the target since the last readback to the CPU and returns true;
    // false means it is not supported and LockTarget must be used.
    // CompleteReadback finishes it into the pixels LockTarget returns (after
    // which LockTarget reads nothing new) and returns true, or returns false
    // when wait is false and the GPU is not done yet. Nothing pending: true.
    virtual bool RequestReadback(void* target) { return false; }
    virtual bool CompleteReadback(void* target, bool wait) { return true; }
    virtual void* GetTargetTexture(void* target) = 0;
    virtual void UpdateTargetTexture(void* target, const uint8_t* pixels,
                                     int width, int height, int pitch) = 0;
    virtual void* CreateTexture(int width, int height) = 0;
    virtual void UpdateTexture(void* texture, const uint8_t* pixels,
                               int width, int height, int pitch) = 0;
    virtual void DestroyTexture(void* texture) = 0;
    virtual uint8_t* LockTexture(void* texture, int& pitch) = 0;
    virtual void SetMask(void* maskTarget) = 0;
    virtual void SetBlendMode(int mode, const float* uniformColor = nullptr) = 0;
    virtual void DrawMesh(const float* vertices, int vertexCount,
                          const uint16_t* indices, int indexCount,
                          void* texture, float opacity) = 0;

    // Called when the host rebuilds the engine in-process and the SDL renderer
    // this backend cached has been destroyed.  Backends that hold no renderer
    // state keep the default no-op.
    virtual void ResetForEngineRestart() {}
};

iTVPRenderBackend* TVPGetRenderBackend();

// Resets whichever backend is selected; see the definition in TVPCompositor.cpp.
void TVPResetRenderBackendForEngineRestart();
} // namespace krkrsdl3


