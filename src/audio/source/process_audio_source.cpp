// ---------------------------------------------
// ProcessAudioSource implementation. See header / ADR-0009 for design.
// ---------------------------------------------
#include "process_audio_source.h"

#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <audioclientactivationparams.h>
#include <avrt.h>
#include <ks.h>
#include <ksmedia.h>
#include <wrl/implements.h>

#include <chrono>
#include <cstring>
#include <vector>

namespace lw::source {

namespace {

// Win10 build 20348 / Win11 21H2 — first build with process-loopback support.
constexpr DWORD kMinBuild = 20348;

bool os_supports_process_loopback() {
    using RtlGetVersionFn = LONG (WINAPI*)(PRTL_OSVERSIONINFOW);
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll) return false;

    auto rtl_get_version = reinterpret_cast<RtlGetVersionFn>(
        GetProcAddress(ntdll, "RtlGetVersion"));
    if (!rtl_get_version) return false;

    RTL_OSVERSIONINFOW vi {};
    vi.dwOSVersionInfoSize = sizeof(vi);
    if (rtl_get_version(&vi) != 0) return false;
    return vi.dwBuildNumber >= kMinBuild;
}

bool process_loopback_api_resolves() {
    HMODULE mmdev = LoadLibraryW(L"MMDevApi.dll");
    if (!mmdev) return false;
    const bool ok = GetProcAddress(mmdev, "ActivateAudioInterfaceAsync") != nullptr;
    FreeLibrary(mmdev);
    return ok;
}

/// Async-completion handler for ActivateAudioInterfaceAsync. The API delivers
/// the IAudioClient via callback; we stash it and signal an event so the
/// caller can wait synchronously. Implements IUnknown manually to avoid a
/// runtime dependency on the WRL macro that pulls in the C++/WinRT runtime.
class ActivationHandler final
    : public IActivateAudioInterfaceCompletionHandler {
public:
    HANDLE   done_event = nullptr;
    HRESULT  activate_hr = E_FAIL;
    HRESULT  result_hr   = E_FAIL;
    IAudioClient* client = nullptr;

    explicit ActivationHandler(HANDLE ev) : done_event(ev) {}

    // IUnknown
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&ref_); }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG r = InterlockedDecrement(&ref_);
        if (r == 0) delete this;
        return r;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == __uuidof(IUnknown)
            || riid == __uuidof(IAgileObject)
            || riid == __uuidof(IActivateAudioInterfaceCompletionHandler)) {
            *ppv = static_cast<IActivateAudioInterfaceCompletionHandler*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }

    HRESULT STDMETHODCALLTYPE ActivateCompleted(
        IActivateAudioInterfaceAsyncOperation* op) override {
        IUnknown* unk = nullptr;
        activate_hr = op ? op->GetActivateResult(&result_hr, &unk) : E_POINTER;
        if (SUCCEEDED(activate_hr) && SUCCEEDED(result_hr) && unk) {
            unk->QueryInterface(__uuidof(IAudioClient),
                                reinterpret_cast<void**>(&client));
        }
        if (unk) unk->Release();
        if (done_event) SetEvent(done_event);
        return S_OK;
    }

private:
    LONG ref_ = 1;
};

template <typename T>
struct ComPtr {
    T* ptr = nullptr;
    ~ComPtr() { if (ptr) ptr->Release(); }
    T*  operator->() const { return ptr; }
    T*  get() const { return ptr; }
    T** put() { if (ptr) { ptr->Release(); ptr = nullptr; } return &ptr; }
    operator bool() const { return ptr != nullptr; }
};

struct HandleGuard {
    HANDLE h = nullptr;
    ~HandleGuard() { if (h && h != INVALID_HANDLE_VALUE) CloseHandle(h); }
};

/// SEH-wrapped GetBuffer. The audio service occasionally faults inside this
/// call when the host loses graphics device focus from exclusive-fullscreen
/// (MS Q&A 1036316). We catch the AV and return a recoverable HRESULT so the
/// capture loop can tear down + re-activate.
///
/// Per MSVC rules, a function with __try/__except cannot use objects that
/// require unwinding. This helper takes raw pointers; the caller owns them.
HRESULT safe_get_buffer(IAudioCaptureClient* capture,
                        BYTE** data, UINT32* frames, DWORD* flags,
                        UINT64* device_pos, UINT64* qpc_pos) {
    __try {
        return capture->GetBuffer(data, frames, flags, device_pos, qpc_pos);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return AUDCLNT_E_DEVICE_INVALIDATED;
    }
}

/// Activate the IAudioClient against our own process via the
/// process-loopback virtual device. Returns nullptr on failure.
IAudioClient* activate_process_loopback_client(HRESULT* out_hr = nullptr) {
    AUDIOCLIENT_ACTIVATION_PARAMS params {};
    params.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
    params.ProcessLoopbackParams.TargetProcessId     = GetCurrentProcessId();
    params.ProcessLoopbackParams.ProcessLoopbackMode =
        PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE;

    PROPVARIANT activate_params {};
    activate_params.vt          = VT_BLOB;
    activate_params.blob.cbSize = sizeof(params);
    activate_params.blob.pBlobData = reinterpret_cast<BYTE*>(&params);

    HandleGuard done;
    done.h = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!done.h) {
        if (out_hr) *out_hr = HRESULT_FROM_WIN32(GetLastError());
        return nullptr;
    }

    auto* handler = new ActivationHandler(done.h);  // ref=1
    IActivateAudioInterfaceAsyncOperation* op = nullptr;
    HRESULT hr = ActivateAudioInterfaceAsync(
        VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK,
        __uuidof(IAudioClient),
        &activate_params,
        handler,
        &op);

    if (FAILED(hr)) {
        if (out_hr) *out_hr = hr;
        handler->Release();
        if (op) op->Release();
        return nullptr;
    }

    // Wait for completion, with a generous timeout. Activation is normally
    // sub-millisecond; 5 seconds is purely defensive.
    WaitForSingleObject(done.h, 5000);

    HRESULT activate_hr = handler->activate_hr;
    HRESULT result_hr   = handler->result_hr;
    IAudioClient* client = handler->client;
    handler->client = nullptr;  // ownership transferred to caller
    handler->Release();
    if (op) op->Release();

    if (FAILED(activate_hr) || FAILED(result_hr) || !client) {
        if (out_hr) *out_hr = FAILED(activate_hr) ? activate_hr : result_hr;
        if (client) client->Release();
        return nullptr;
    }

    if (out_hr) *out_hr = S_OK;
    return client;
}

}  // namespace

ProcessAudioSource::ProcessAudioSource() = default;

ProcessAudioSource::~ProcessAudioSource() {
    stop();
}

Info ProcessAudioSource::info() const {
    return {
        .code = "process",
        .display = "Game Audio Only (Process Loopback)",
        .is_default = false,
        .order = 1,
        .activates_capture = true,
    };
}

bool ProcessAudioSource::available() const {
    return os_supports_process_loopback() && process_loopback_api_resolves();
}

std::optional<Capabilities> ProcessAudioSource::open() {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE && hr != S_FALSE) return std::nullopt;

    if (!available()) return std::nullopt;

    // Smoke-test activation so we report failure during open() rather than
    // surprising the user mid-stream. The capture thread will activate again
    // for its own lifetime.
    IAudioClient* probe = activate_process_loopback_client();
    if (!probe) return std::nullopt;
    probe->Release();

    capabilities_ = {};
    capabilities_.format.sample_rate = 48000;
    capabilities_.format.channels    = 2;
    capabilities_.format.layout      = ChannelLayout::Stereo;
    capabilities_.typical_frame_count = 480;   // ~10 ms at 48 kHz
    capabilities_.max_frame_count     = 2400;  // ~50 ms worst case
    capabilities_.reported_latency_ms = 20.0;
    return capabilities_;
}

bool ProcessAudioSource::start(FrameSink sink) {
    if (running_.load()) return true;
    running_.store(true);
    need_restart_.store(false);
    thread_ = std::thread(&ProcessAudioSource::capture_loop, this, std::move(sink));
    return true;
}

void ProcessAudioSource::stop() {
    running_.store(false);
    if (thread_.joinable()) thread_.join();
}

bool ProcessAudioSource::restart_requested() const {
    return need_restart_.load(std::memory_order_relaxed);
}

void ProcessAudioSource::capture_loop(FrameSink sink) {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool com_initialized_here = SUCCEEDED(hr);
    auto com_uninit = [&] { if (com_initialized_here) CoUninitialize(); };

    DWORD mmcss_task_index = 0;
    HANDLE mmcss = AvSetMmThreadCharacteristicsW(L"Audio", &mmcss_task_index);
    if (mmcss) AvSetMmThreadPriority(mmcss, AVRT_PRIORITY_HIGH);
    auto mmcss_revert = [&] { if (mmcss) AvRevertMmThreadCharacteristics(mmcss); };

    ComPtr<IAudioClient> client;
    HRESULT activate_hr = E_FAIL;
    client.ptr = activate_process_loopback_client(&activate_hr);
    if (!client) {
        running_.store(false);
        mmcss_revert();
        com_uninit();
        return;
    }

    // Hardcoded format — GetMixFormat returns E_NOTIMPL on the virtual device.
    // The audio engine resamples / requantizes / downmixes internally to fit.
    WAVEFORMATEXTENSIBLE want {};
    want.Format.wFormatTag           = WAVE_FORMAT_EXTENSIBLE;
    want.Format.nChannels            = 2;
    want.Format.nSamplesPerSec       = 48000;
    want.Format.wBitsPerSample       = 32;
    want.Format.nBlockAlign          = static_cast<WORD>(
        (want.Format.nChannels * want.Format.wBitsPerSample) / 8);
    want.Format.nAvgBytesPerSec      =
        want.Format.nSamplesPerSec * want.Format.nBlockAlign;
    want.Format.cbSize               =
        static_cast<WORD>(sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX));
    want.Samples.wValidBitsPerSample = 32;
    want.dwChannelMask               = KSAUDIO_SPEAKER_STEREO;
    want.SubFormat                   = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;

    constexpr DWORD kStreamFlags =
        AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK;

    // hnsBufferDuration of 0 lets the engine pick a sensible default for
    // event-driven shared mode. hnsPeriodicity must be 0 for shared mode.
    hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, kStreamFlags,
                             /*hnsBufferDuration*/ 0,
                             /*hnsPeriodicity*/   0,
                             reinterpret_cast<WAVEFORMATEX*>(&want),
                             nullptr);
    if (FAILED(hr)) {
        running_.store(false);
        mmcss_revert();
        com_uninit();
        return;
    }

    HandleGuard event;
    event.h = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!event.h
        || FAILED(client->SetEventHandle(event.h))) {
        running_.store(false);
        mmcss_revert();
        com_uninit();
        return;
    }

    ComPtr<IAudioCaptureClient> capture;
    if (FAILED(client->GetService(__uuidof(IAudioCaptureClient),
                                    reinterpret_cast<void**>(capture.put())))
        || !capture) {
        running_.store(false);
        mmcss_revert();
        com_uninit();
        return;
    }

    if (FAILED(client->Start())) {
        running_.store(false);
        mmcss_revert();
        com_uninit();
        return;
    }

    Format frame_format;
    frame_format.sample_rate = 48000;
    frame_format.channels    = 2;
    frame_format.layout      = ChannelLayout::Stereo;

    // Watchdog state: detect the 60-min drift bug (OBS issues #8064/#8086) by
    // tracking when audio frames last arrived. After 90 s of silence-or-stall
    // while running, signal restart so AudioSystem rebuilds the source.
    auto last_data_time = std::chrono::steady_clock::now();
    constexpr auto kStallThreshold = std::chrono::seconds(90);

    std::vector<float> scratch;
    while (running_.load(std::memory_order_relaxed)) {
        const DWORD wait = WaitForSingleObject(event.h, 200);
        if (!running_.load(std::memory_order_relaxed)) break;

        if (wait == WAIT_TIMEOUT) {
            // No event in 200 ms. Check watchdog; otherwise loop and wait again.
            if (std::chrono::steady_clock::now() - last_data_time > kStallThreshold) {
                need_restart_.store(true, std::memory_order_relaxed);
                break;
            }
            continue;
        }
        if (wait != WAIT_OBJECT_0) break;

        // Drain available packets each event tick.
        UINT32 next_pkt = 0;
        bool   tear_down = false;
        while (SUCCEEDED(capture->GetNextPacketSize(&next_pkt)) && next_pkt > 0) {
            BYTE*  data = nullptr;
            UINT32 frames_available = 0;
            DWORD  flags = 0;
            UINT64 device_pos = 0;
            UINT64 qpc_pos = 0;
            HRESULT gb = safe_get_buffer(capture.get(),
                                          &data, &frames_available, &flags,
                                          &device_pos, &qpc_pos);
            if (FAILED(gb)) {
                tear_down = true;
                break;
            }

            if (frames_available > 0) {
                const size_t total_samples =
                    static_cast<size_t>(frames_available) * frame_format.channels;
                if (scratch.size() < total_samples) scratch.resize(total_samples);

                if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
                    // Push zeros rather than possibly-stale memory, per docs.
                    std::memset(scratch.data(), 0, total_samples * sizeof(float));
                } else if (data) {
                    std::memcpy(scratch.data(), data,
                                total_samples * sizeof(float));
                } else {
                    capture->ReleaseBuffer(frames_available);
                    continue;
                }

                sink(std::span<const float>(scratch.data(), total_samples),
                     frame_format);
                last_data_time = std::chrono::steady_clock::now();
            }
            capture->ReleaseBuffer(frames_available);
        }

        if (tear_down) {
            need_restart_.store(true, std::memory_order_relaxed);
            break;
        }
    }

    client->Stop();
    mmcss_revert();
    com_uninit();
    running_.store(false);
}

}  // namespace lw::source
