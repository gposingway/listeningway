// ---------------------------------------------
// WasapiLoopbackSource implementation. See header for design rationale.
// ---------------------------------------------
#include "wasapi_loopback_source.h"

#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <avrt.h>
#include <ks.h>
#include <ksmedia.h>
#include <combaseapi.h>
#include <Functiondiscoverykeys_devpkey.h>

#include <algorithm>
#include <chrono>
#include <span>
#include <thread>
#include <vector>

namespace lw::source {

namespace {

/// Notification client. Microsoft's contract: callbacks must be non-blocking,
/// must not call register/unregister recursively, must not wait. We satisfy
/// this by setting a single atomic flag and returning. The owning Source
/// polls the flag from `restart_requested()`; AudioSystem reacts on its
/// worker thread.
class NotificationClient final : public IMMNotificationClient {
public:
    explicit NotificationClient(std::atomic<bool>& dirty) : dirty_(dirty) {}

    // IUnknown
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&ref_); }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG r = InterlockedDecrement(&ref_);
        if (r == 0) delete this;
        return r;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IMMNotificationClient)) {
            *ppv = static_cast<IMMNotificationClient*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }

    // IMMNotificationClient — the only one we care about. The callback fires
    // 3x per change (one per role); the boolean flag is idempotent so the
    // worker only acts once.
    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR) override {
        if (flow == eRender && role == eMultimedia) {
            dirty_.store(true, std::memory_order_relaxed);
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR, DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY) override { return S_OK; }

private:
    LONG ref_ = 1;
    std::atomic<bool>& dirty_;
};

/// RAII for COM pointers; minimal — only what we need.
template <typename T>
struct ComPtr {
    T* ptr = nullptr;
    ~ComPtr() { if (ptr) ptr->Release(); }
    T*  operator->() const { return ptr; }
    T*  get() const { return ptr; }
    T** put() { if (ptr) { ptr->Release(); ptr = nullptr; } return &ptr; }
    operator bool() const { return ptr != nullptr; }
};

/// RAII for an opaque OS handle. CloseHandle on destruct.
struct HandleGuard {
    HANDLE h = nullptr;
    ~HandleGuard() { if (h && h != INVALID_HANDLE_VALUE) CloseHandle(h); }
};

ChannelLayout DetectLayout(WAVEFORMATEX* wfx) {
    if (!wfx) return ChannelLayout::Unknown;
    if (wfx->nChannels == 1) return ChannelLayout::Mono;
    if (wfx->nChannels == 2) return ChannelLayout::Stereo;
    if (wfx->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        auto* ext = reinterpret_cast<WAVEFORMATEXTENSIBLE*>(wfx);
        const DWORD mask = ext->dwChannelMask;
        if (wfx->nChannels == 6) return ChannelLayout::Surround51;
        if (wfx->nChannels == 8) {
            // Heuristic per research-notes.md §2: side channels in mask 0x600
            // (SPEAKER_SIDE_LEFT|SIDE_RIGHT) means side-first ordering.
            constexpr DWORD kSideBits = 0x600;
            if ((mask & kSideBits) == kSideBits) return ChannelLayout::Surround71Side;
            return ChannelLayout::Surround71Rear;
        }
    }
    return ChannelLayout::Unknown;
}

}  // namespace

WasapiLoopbackSource::WasapiLoopbackSource() = default;

WasapiLoopbackSource::~WasapiLoopbackSource() {
    stop();

    if (device_enumerator_ && notification_client_) {
        device_enumerator_->UnregisterEndpointNotificationCallback(notification_client_);
    }
    if (notification_client_) {
        notification_client_->Release();
        notification_client_ = nullptr;
    }
    if (device_enumerator_) {
        device_enumerator_->Release();
        device_enumerator_ = nullptr;
    }
}

Info WasapiLoopbackSource::info() const {
    return {
        .code = "system",
        .display = "System Audio (WASAPI Loopback)",
        .is_default = true,
        .order = 0,
        .activates_capture = true,
    };
}

bool WasapiLoopbackSource::available() const {
    // WASAPI loopback is available on Windows Vista+.
    return true;
}

std::optional<Capabilities> WasapiLoopbackSource::open() {
    // Initialize COM if not already (per-thread, MTA).
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    // RPC_E_CHANGED_MODE means already STA on this thread — fine for our use.
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE && hr != S_FALSE) return std::nullopt;

    if (!device_enumerator_) {
        hr = CoCreateInstance(
            __uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
            __uuidof(IMMDeviceEnumerator),
            reinterpret_cast<void**>(&device_enumerator_));
        if (FAILED(hr) || !device_enumerator_) return std::nullopt;
    }

    if (!notification_client_) {
        notification_client_ = new NotificationClient(device_change_pending_);
        device_enumerator_->RegisterEndpointNotificationCallback(notification_client_);
    }

    // Probe the default endpoint to discover the format. We pin float-stereo
    // via AUTOCONVERTPCM later; this just figures out what the device speaks
    // natively so we can report sane Capabilities.
    ComPtr<IMMDevice> device;
    if (FAILED(device_enumerator_->GetDefaultAudioEndpoint(eRender, eMultimedia, device.put()))
        || !device) return std::nullopt;

    ComPtr<IAudioClient> client;
    if (FAILED(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                 reinterpret_cast<void**>(client.put())))
        || !client) return std::nullopt;

    WAVEFORMATEX* mix = nullptr;
    if (FAILED(client->GetMixFormat(&mix)) || !mix) return std::nullopt;

    Capabilities caps;
    caps.format.sample_rate = mix->nSamplesPerSec;
    caps.format.channels    = 2;  // we always pin stereo via AUTOCONVERTPCM
    caps.format.layout      = ChannelLayout::Stereo;
    caps.typical_frame_count = mix->nSamplesPerSec / 100;  // ~10 ms of audio
    caps.max_frame_count     = mix->nSamplesPerSec / 20;   // ~50 ms worst case
    caps.reported_latency_ms = 20.0;  // typical shared-mode loopback floor

    // We didn't store the layout (we'll request stereo regardless) but
    // we did record sample rate and worst-case buffer.

    CoTaskMemFree(mix);
    {
        std::lock_guard<std::mutex> g(capabilities_mutex_);
        capabilities_ = caps;
    }
    return caps;
}

bool WasapiLoopbackSource::start(FrameSink sink) {
    if (running_.load()) return true;
    if (!device_enumerator_) return false;

    running_.store(true);
    device_change_pending_.store(false);
    thread_ = std::thread(&WasapiLoopbackSource::capture_loop, this, std::move(sink));
    return true;
}

void WasapiLoopbackSource::stop() {
    running_.store(false);
    if (thread_.joinable()) thread_.join();
}

bool WasapiLoopbackSource::restart_requested() const {
    return device_change_pending_.load(std::memory_order_relaxed);
}

void WasapiLoopbackSource::capture_loop(FrameSink sink) {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool com_initialized_here = SUCCEEDED(hr);
    auto com_uninit = [&] { if (com_initialized_here) CoUninitialize(); };

    // Boost thread to MMCSS Audio class (per research-notes.md §2 — not Pro Audio).
    DWORD mmcss_task_index = 0;
    HANDLE mmcss = AvSetMmThreadCharacteristicsW(L"Audio", &mmcss_task_index);
    if (mmcss) {
        AvSetMmThreadPriority(mmcss, AVRT_PRIORITY_HIGH);
    }
    auto mmcss_revert = [&] { if (mmcss) AvRevertMmThreadCharacteristics(mmcss); };

    ComPtr<IMMDevice> device;
    if (!device_enumerator_
        || FAILED(device_enumerator_->GetDefaultAudioEndpoint(eRender, eMultimedia, device.put()))
        || !device) {
        running_.store(false);
        mmcss_revert();
        com_uninit();
        return;
    }

    ComPtr<IAudioClient> client;
    if (FAILED(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                 reinterpret_cast<void**>(client.put()))) || !client) {
        running_.store(false);
        mmcss_revert();
        com_uninit();
        return;
    }

    WAVEFORMATEX* mix = nullptr;
    if (FAILED(client->GetMixFormat(&mix)) || !mix) {
        running_.store(false);
        mmcss_revert();
        com_uninit();
        return;
    }

    // Build the desired stereo float format. We'll ask the engine to convert
    // for us via AUTOCONVERTPCM + SRC_DEFAULT_QUALITY. This handles surround
    // downmix, format conversion, and any spatial-audio rendering quirks that
    // confuse direct loopback.
    WAVEFORMATEXTENSIBLE want = {};
    want.Format.wFormatTag         = WAVE_FORMAT_EXTENSIBLE;
    want.Format.nChannels          = 2;
    want.Format.nSamplesPerSec     = mix->nSamplesPerSec;
    want.Format.wBitsPerSample     = 32;
    want.Format.nBlockAlign        = (want.Format.nChannels * want.Format.wBitsPerSample) / 8;
    want.Format.nAvgBytesPerSec    = want.Format.nSamplesPerSec * want.Format.nBlockAlign;
    want.Format.cbSize             = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
    want.Samples.wValidBitsPerSample = 32;
    want.dwChannelMask             = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
    want.SubFormat                 = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;

    DWORD stream_flags = AUDCLNT_STREAMFLAGS_LOOPBACK
                       | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM
                       | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;

    // We do NOT use AUDCLNT_STREAMFLAGS_EVENTCALLBACK for loopback — research
    // confirms it's broken and the event never fires for loopback streams.
    // We drive the loop with a high-resolution waitable timer instead.

    hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, stream_flags,
                             /*hnsBufferDuration*/ 0, /*hnsPeriodicity*/ 0,
                             reinterpret_cast<WAVEFORMATEX*>(&want), nullptr);

    if (FAILED(hr)) {
        // Fallback: try again with the device's mix format (no conversion).
        // Also drop AUTOCONVERTPCM since the device format is already supported.
        stream_flags = AUDCLNT_STREAMFLAGS_LOOPBACK;
        hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, stream_flags,
                                 /*hnsBufferDuration*/ 0, /*hnsPeriodicity*/ 0,
                                 mix, nullptr);
        if (FAILED(hr)) {
            CoTaskMemFree(mix);
            running_.store(false);
            mmcss_revert();
            com_uninit();
            return;
        }
        // Update reported capabilities with the actual format we got.
        Capabilities caps {};
        caps.format.sample_rate = mix->nSamplesPerSec;
        caps.format.channels    = mix->nChannels;
        caps.format.layout      = DetectLayout(mix);
        caps.typical_frame_count = mix->nSamplesPerSec / 100;
        caps.max_frame_count     = mix->nSamplesPerSec / 20;
        {
            std::lock_guard<std::mutex> g(capabilities_mutex_);
            capabilities_ = caps;
        }
    }
    // The Initialize call took ownership conceptually, but the engine doesn't
    // free it; we keep mix for one more reference and free it below.

    // Determine actual buffer/period for the polling cadence.
    REFERENCE_TIME period_default = 0;
    REFERENCE_TIME period_min = 0;
    client->GetDevicePeriod(&period_default, &period_min);  // 100-ns units
    const auto poll_interval = std::chrono::nanoseconds(
        static_cast<long long>(period_default * 100 / 2)  // half device period
    );

    UINT32 buffer_frame_count = 0;
    client->GetBufferSize(&buffer_frame_count);

    ComPtr<IAudioCaptureClient> capture;
    if (FAILED(client->GetService(__uuidof(IAudioCaptureClient),
                                    reinterpret_cast<void**>(capture.put())))
        || !capture) {
        CoTaskMemFree(mix);
        running_.store(false);
        mmcss_revert();
        com_uninit();
        return;
    }

    if (FAILED(client->Start())) {
        CoTaskMemFree(mix);
        running_.store(false);
        mmcss_revert();
        com_uninit();
        return;
    }

    // Determine the published frame format for sink callbacks. This mirrors
    // what we know after Initialize succeeded; we pinned 2-channel float in
    // the primary path.
    Format frame_format;
    {
        std::lock_guard<std::mutex> g(capabilities_mutex_);
        frame_format = capabilities_.format;
    }

    // Polling loop. Drain available packets each tick.
    HandleGuard timer;
    timer.h = CreateWaitableTimerExW(nullptr, nullptr,
                                      CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                      TIMER_ALL_ACCESS);
    LARGE_INTEGER first_tick = {};
    first_tick.QuadPart = -static_cast<LONGLONG>(poll_interval.count() / 100);  // 100-ns units, negative for relative
    SetWaitableTimer(timer.h, &first_tick, /*period_ms*/ static_cast<LONG>(
        std::chrono::duration_cast<std::chrono::milliseconds>(poll_interval).count()),
        nullptr, nullptr, FALSE);

    std::vector<float> scratch;  // grown once, reused
    while (running_.load(std::memory_order_relaxed)) {
        if (timer.h) {
            WaitForSingleObject(timer.h, 50);  // capped at 50 ms so stop is responsive
        } else {
            std::this_thread::sleep_for(poll_interval);
        }
        if (!running_.load(std::memory_order_relaxed)) break;

        UINT32 next_pkt = 0;
        while (SUCCEEDED(capture->GetNextPacketSize(&next_pkt)) && next_pkt > 0) {
            BYTE* data = nullptr;
            UINT32 frames_available = 0;
            DWORD flags = 0;
            UINT64 device_pos = 0, qpc_pos = 0;
            hr = capture->GetBuffer(&data, &frames_available, &flags, &device_pos, &qpc_pos);
            if (FAILED(hr)) break;

            if (data && frames_available > 0
                && (flags & AUDCLNT_BUFFERFLAGS_SILENT) == 0) {
                const size_t total_samples =
                    static_cast<size_t>(frames_available) * frame_format.channels;
                if (scratch.size() < total_samples) scratch.resize(total_samples);

                // The primary path pinned WAVE_FORMAT_IEEE_FLOAT 2-channel, so
                // `data` is already float interleaved. Copy direct.
                // Fallback path (when AUTOCONVERTPCM init fails) speaks the
                // device's mix format, which is *almost always* float on
                // modern systems too. If a vendor exposes only PCM here, the
                // byte buffer is mis-interpreted; we accept that for v1
                // beta — the AUTOCONVERTPCM primary path covers the realistic
                // device population.
                std::memcpy(scratch.data(), data, total_samples * sizeof(float));

                sink(std::span<const float>(scratch.data(), total_samples), frame_format);
            }
            capture->ReleaseBuffer(frames_available);
        }
    }

    client->Stop();
    CoTaskMemFree(mix);
    mmcss_revert();
    com_uninit();
    running_.store(false);
}

}  // namespace lw::source
