// WGC window-capture feasibility test (raw COM, no cppwinrt — MinGW friendly).
// Captures the given HWND via Windows.Graphics.Capture and dumps the ALPHA
// channel: for the per-pixel-alpha DesktopLyrics window the alpha should BE
// the text mask (transparent everywhere except glyphs).
// Build: g++ test_wgc.cpp -o test_wgc.exe -ld3d11 -ldxgi -lole32 -O2 -std=c++17
// Usage: test_wgc.exe <hwnd_hex>
#ifndef UNICODE
#define UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <cstdio>
#include <cstdint>
#include <vector>

// ---- IIDs (from Windows SDK 10.0.22621.0 cppwinrt metadata) ----
static const IID kIID_IGraphicsCaptureItem        = {0x79C3F95B,0x31F7,0x4EC2,{0xA4,0x64,0x63,0x2E,0xF5,0xD3,0x07,0x60}};
static const IID kIID_IGraphicsCaptureSession     = {0x814E42A9,0xF70F,0x4AD7,{0x93,0x9B,0xFD,0xDC,0xC6,0xEB,0x88,0x0D}};
static const IID kIID_FramePoolStatics2           = {0x589B103F,0x6BBC,0x5DF5,{0xA9,0x91,0x02,0xE2,0x8B,0x3B,0x66,0xD5}};
static const IID kIID_FramePoolActivation         = {0x24EB6D22,0x1975,0x422E,{0x82,0xE7,0x78,0x0D,0xBD,0x8D,0xDF,0x24}}; // reused: class factory QI target below
static const IID kIID_FramePoolStaticsCls         = {0x24EB6D22,0x1975,0x422E,{0x82,0xE7,0x78,0x0D,0xBD,0x8D,0xDF,0x24}};
static const IID kIID_IDirect3D11CaptureFramePool = {0x24EB6D22,0x1975,0x422E,{0x82,0xE7,0x78,0x0D,0xBD,0x8D,0xDF,0x24}};
static const IID kIID_IDirect3D11CaptureFrame     = {0xFA50C623,0x38DA,0x4B32,{0xAC,0xF3,0xFA,0x97,0x34,0xAD,0x80,0x0E}};
static const IID kIID_IDirect3DDevice             = {0xA37624AB,0x8D5F,0x4650,{0x9D,0x3E,0x9E,0xAE,0x3D,0x9B,0xC6,0x70}};
static const IID kIID_IDirect3DSurface            = {0x0BF4A146,0x13C1,0x4694,{0xBE,0xE3,0x7A,0xBF,0x15,0xEA,0xF5,0x86}};
static const IID kIID_ItemInterop                 = {0x3628E81B,0x3CAC,0x4C60,{0xB7,0xF4,0x23,0xCE,0x0E,0x0C,0x33,0x56}};
static const IID kIID_DxgiAccess                  = {0xA9B3D012,0x3DF2,0x4EE3,{0xB8,0xD1,0x86,0x95,0xF4,0x57,0xD3,0xC1}};

// ---- Minimal IInspectable base vtable (6 slots) ----
struct WrtBase {
    virtual long __stdcall QueryInterface(const IID*, void**) = 0;
    virtual unsigned long __stdcall AddRef() = 0;
    virtual unsigned long __stdcall Release() = 0;
    virtual long __stdcall GetIids(unsigned long*, IID**) = 0;
    virtual long __stdcall GetRuntimeClassName(void**) = 0;
    virtual long __stdcall GetTrustLevel(int*) = 0;
};
// vtable orders verified against SDK 22621 cppwinrt produce<> blocks:
struct IGraphicsCaptureItem : WrtBase {
    virtual long __stdcall get_DisplayName(void** value) = 0;              // 6
    virtual long __stdcall get_Size(SIZE* value) = 0;                      // 7
};
struct IGraphicsCaptureSession : WrtBase {
    virtual long __stdcall StartCapture() = 0;                             // 6
};
struct IDirect3D11CaptureFramePoolStatics2 : WrtBase {
    virtual long __stdcall CreateFreeThreaded(void* device, int pixelFormat,
        int numberOfBuffers, SIZE size, void** result) = 0;                // 6
};
struct IDirect3D11CaptureFramePool : WrtBase {
    virtual long __stdcall Recreate(void*, int, int, SIZE) = 0;            // 6
    virtual long __stdcall TryGetNextFrame(void** result) = 0;             // 7
    virtual long __stdcall add_FrameArrived(void*, void*) = 0;             // 8
    virtual long __stdcall remove_FrameArrived(__int64) = 0;               // 9
    virtual long __stdcall CreateCaptureSession(void* item, void** result) = 0; // 10
};
struct IDirect3D11CaptureFrame : WrtBase {
    virtual long __stdcall get_Surface(void** value) = 0;                  // 6
    virtual long __stdcall get_SystemRelativeTime(__int64* value) = 0;     // 7
    virtual long __stdcall get_ContentSize(SIZE* value) = 0;               // 8
};
struct IGraphicsCaptureItemInterop : IUnknown {
    virtual long __stdcall CreateForWindow(HWND window, const IID* iid, void** result) = 0;
    virtual long __stdcall CreateForMonitor(HMONITOR mon, const IID* iid, void** result) = 0;
};
struct IDirect3DDxgiInterfaceAccess : IUnknown {
    virtual long __stdcall GetInterface(const IID* iid, void** p) = 0;
};

// ---- combase dynamic loading ----
static HRESULT (WINAPI* pRoInitialize)(int);
static HRESULT (WINAPI* pRoGetActivationFactory)(void*, const IID*, void**);
static HRESULT (WINAPI* pWindowsCreateString)(const wchar_t*, unsigned, void**);
static HRESULT (WINAPI* pCreateDirect3D11DeviceFromDXGIDevice)(IDXGIDevice*, void**);

static HRESULT hstr(const wchar_t* s, void** out) { return pWindowsCreateString(s, (unsigned)wcslen(s), out); }
static HRESULT activate(const wchar_t* cls, const IID* iid, void** out) {
    void* h = nullptr;
    HRESULT hr = hstr(cls, &h);
    if (FAILED(hr)) return hr;
    return pRoGetActivationFactory(h, iid, out);
}

#define CHK(hr) do { HRESULT _r = (hr); if (FAILED(_r)) { \
    printf("[x] %s failed: 0x%08lX (line %d)\n", #hr, (unsigned long)_r, __LINE__); return 1; } } while (0)

int main(int argc, char* argv[]) {
    if (argc < 2) { printf("usage: test_wgc.exe <hwnd_hex>\n"); return 1; }
    HWND hwnd = (HWND)(ULONG_PTR)strtoull(argv[1], nullptr, 16);
    if (!IsWindow(hwnd)) { printf("[x] bad hwnd\n"); return 1; }

    HMODULE combase = LoadLibraryW(L"combase.dll");
    if (!combase) { printf("[x] no combase\n"); return 1; }
    pRoInitialize = (HRESULT (WINAPI*)(int))GetProcAddress(combase, "RoInitialize");
    pRoGetActivationFactory = (HRESULT (WINAPI*)(void*, const IID*, void**))GetProcAddress(combase, "RoGetActivationFactory");
    pWindowsCreateString = (HRESULT (WINAPI*)(const wchar_t*, unsigned, void**))GetProcAddress(combase, "WindowsCreateString");
    if (!pRoInitialize || !pRoGetActivationFactory || !pWindowsCreateString) { printf("[x] combase exports missing\n"); return 1; }
    pRoInitialize(0);   // MTA; S_OK or RPC_E_CHANGED_MODE both fine
    // CreateForWindow requires a DPI-aware caller (E_INVALIDARG otherwise)
    HMODULE u32 = GetModuleHandleW(L"user32.dll");
    if (u32) {
        auto SetDpiCtx = (BOOL (WINAPI*)(void*))GetProcAddress(u32, "SetProcessDpiAwarenessContext");
        if (SetDpiCtx) SetDpiCtx((void*)-4);   // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
    }

    // D3D device (BGRA support required by WGC)
    ID3D11Device* dev = nullptr; ID3D11DeviceContext* ctx = nullptr;
    D3D_FEATURE_LEVEL fl;
    CHK(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        nullptr, 0, D3D11_SDK_VERSION, &dev, &fl, &ctx));
    IDXGIDevice* dxgiDev = nullptr; CHK(dev->QueryInterface(IID_IDXGIDevice, (void**)&dxgiDev));
    HMODULE d3d11mod = GetModuleHandleW(L"d3d11.dll");
    pCreateDirect3D11DeviceFromDXGIDevice = (HRESULT (WINAPI*)(IDXGIDevice*, void**))
        GetProcAddress(d3d11mod, "CreateDirect3D11DeviceFromDXGIDevice");
    if (!pCreateDirect3D11DeviceFromDXGIDevice) { printf("[x] no CreateDirect3D11DeviceFromDXGIDevice export\n"); return 1; }
    IUnknown* devInspectable = nullptr;
    CHK(pCreateDirect3D11DeviceFromDXGIDevice(dxgiDev, (void**)&devInspectable));
    void* d3dDeviceWrt = nullptr;
    CHK(devInspectable->QueryInterface(kIID_IDirect3DDevice, (void**)&d3dDeviceWrt));
    printf("[+] IDirect3DDevice ok\n");

    // GraphicsCaptureItem via interop
    IGraphicsCaptureItemInterop* interop = nullptr;
    CHK(activate(L"Windows.Graphics.Capture.GraphicsCaptureItem", &kIID_ItemInterop, (void**)&interop));
    IGraphicsCaptureItem* item = nullptr;
    HRESULT hw = interop->CreateForWindow(hwnd, &kIID_IGraphicsCaptureItem, (void**)&item);
    printf("    CreateForWindow -> 0x%08lX\n", (unsigned long)hw);
    if (FAILED(hw)) {
        HWND others[3];
        others[0] = FindWindowW(L"Notepad", nullptr);
        others[1] = GetForegroundWindow();
        others[2] = FindWindowW(L"ApplicationFrameWindow", nullptr);
        for (int i = 0; i < 3; i++) {
            if (!others[i]) continue;
            IGraphicsCaptureItem* it2 = nullptr;
            HRESULT h2 = interop->CreateForWindow(others[i], &kIID_IGraphicsCaptureItem, (void**)&it2);
            wchar_t cn[64] = {}; GetClassNameW(others[i], cn, 63);
            printf("    try cls=%ls hwnd=0x%p -> 0x%08lX\n", cn, (void*)others[i], (unsigned long)h2);
            if (SUCCEEDED(h2)) it2->Release();
        }
    }
    if (FAILED(hw)) {
        hw = interop->CreateForWindow(hwnd, &IID_IUnknown, (void**)&item);
        printf("    CreateForWindow(IUnknown) -> 0x%08lX\n", (unsigned long)hw);
        if (FAILED(hw)) {
            POINT pt = {}; GetCursorPos(&pt);
            HMONITOR mon = MonitorFromPoint(pt, MONITOR_DEFAULTTOPRIMARY);
            hw = interop->CreateForMonitor(mon, &kIID_IGraphicsCaptureItem, (void**)&item);
            printf("    CreateForMonitor -> 0x%08lX\n", (unsigned long)hw);
        }
    }
    CHK(hw);
    printf("[+] GraphicsCaptureItem ok\n");
    SIZE wsz = {}; item->get_Size(&wsz);
    printf("    item size: %ldx%ld\n", wsz.cx, wsz.cy);

    // Frame pool (free-threaded, no DispatcherQueue needed)
    void* statics = nullptr;
    CHK(activate(L"Windows.Graphics.Capture.Direct3D11CaptureFramePool", &kIID_FramePoolStatics2, &statics));
    IDirect3D11CaptureFramePoolStatics2* statics2 = (IDirect3D11CaptureFramePoolStatics2*)statics;
    IDirect3D11CaptureFramePool* pool = nullptr;
    CHK(statics2->CreateFreeThreaded(d3dDeviceWrt, 87 /*B8G8R8A8UIntNormalized*/, 2, wsz, (void**)&pool));
    printf("[+] frame pool ok\n");

    // Session
    IGraphicsCaptureSession* session = nullptr;
    CHK(pool->CreateCaptureSession(item, (void**)&session));
    CHK(session->StartCapture());
    printf("[+] capture started\n");

    // Grab a few frames, keep the last
    void* frame = nullptr;
    for (int i = 0; i < 40; i++) {
        Sleep(50);
        void* f = nullptr;
        if (SUCCEEDED(pool->TryGetNextFrame(&f)) && f) {
            if (frame) ((IDirect3D11CaptureFrame*)frame)->Release();
            frame = f;
        }
    }
    if (!frame) { printf("[x] no frames\n"); return 1; }
    printf("[+] frames flowing\n");

    // Surface -> DXGI texture -> staging copy -> map
    void* surface = nullptr;
    CHK(((IDirect3D11CaptureFrame*)frame)->get_Surface(&surface));
    IDirect3DDxgiInterfaceAccess* access = nullptr;
    CHK(((IUnknown*)surface)->QueryInterface(kIID_DxgiAccess, (void**)&access));
    IDXGISurface* dxgiSurface = nullptr;
    CHK(access->GetInterface(&IID_IDXGISurface, (void**)&dxgiSurface));
    ID3D11Texture2D* tex = nullptr;
    CHK(dxgiSurface->QueryInterface(IID_ID3D11Texture2D, (void**)&tex));
    D3D11_TEXTURE2D_DESC desc;
    tex->GetDesc(&desc);
    printf("    frame texture: %ux%u fmt=%u\n", desc.Width, desc.Height, desc.Format);

    D3D11_TEXTURE2D_DESC sd = desc;
    sd.Usage = D3D11_USAGE_STAGING;
    sd.BindFlags = 0; sd.MiscFlags = 0; sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ID3D11Texture2D* staging = nullptr;
    CHK(dev->CreateTexture2D(&sd, nullptr, &staging));
    ctx->CopyResource(staging, tex);
    D3D11_MAPPED_SUBRESOURCE map;
    CHK(ctx->Map(staging, 0, D3D11_MAP_READ, 0, &map));

    // Alpha analysis
    int W = (int)desc.Width, H = (int)desc.Height;
    long opaque = 0, minx = W, miny = H, maxx = -1, maxy = -1;
    std::vector<uint8_t> mask((size_t)W * H, 0);
    for (int y = 0; y < H; y++) {
        const uint8_t* row = (const uint8_t*)map.pData + (size_t)y * map.RowPitch;
        for (int x = 0; x < W; x++) {
            uint8_t a = row[x * 4 + 3];
            if (a > 30) {
                mask[(size_t)y * W + x] = 255;
                opaque++;
                if (x < minx) minx = x;
                if (x > maxx) maxx = x;
                if (y < miny) miny = y;
                if (y > maxy) maxy = y;
            }
        }
    }
    printf("    alpha>30: %ld/%d (%.2f%%) bbox=(%ld,%ld)-(%ld,%ld)\n",
           opaque, W * H, 100.0 * opaque / (W * H), minx, miny, maxx, maxy);

    // Save alpha-mask BMP (white = opaque)
    FILE* fp = fopen("C:/Temp/wgc_alpha.bmp", "wb");
    if (fp) {
        int rowsz = (W * 3 + 3) & ~3;
        unsigned filesz = 54 + rowsz * H;
        unsigned char hdr[54] = {};
        hdr[0]='B'; hdr[1]='M';
        *(unsigned*)(hdr+2) = filesz; *(unsigned*)(hdr+10) = 54;
        *(unsigned*)(hdr+14) = 40; *(int*)(hdr+18) = W; *(int*)(hdr+22) = H;
        *(unsigned short*)(hdr+26) = 1; *(unsigned short*)(hdr+28) = 24;
        fwrite(hdr, 1, 54, fp);
        std::vector<uint8_t> row(rowsz, 0);
        for (int y = H - 1; y >= 0; y--) {
            for (int x = 0; x < W; x++) {
                uint8_t v = mask[(size_t)y * W + x];
                row[x*3+0] = v ? 0 : 0; row[x*3+1] = v ? 255 : 0; row[x*3+2] = v ? 0 : 0;
            }
            fwrite(row.data(), 1, rowsz, fp);
        }
        fclose(fp);
        printf("[+] saved C:/Temp/wgc_alpha.bmp\n");
    }
    return 0;
}
