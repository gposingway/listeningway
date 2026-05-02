// Implementation of system-wide audio capture provider (WASAPI loopback)
// Renamed for clarity: audio_capture_provider_system.cpp
#include "audio/capture/providers/audio_capture_provider_system.h"
#include "audio/capture/providers/audio_capture_provider.h"
#include "analysis/audio_analysis.h"
#include "analysis/channel_layout.h"
#include "../../utils/debug_notes.h"
#include "../../utils/logging.h"
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <avrt.h>
#include <vector>
#include <windows.h>
#include <ks.h>
#include <ksmedia.h>
#include <Functiondiscoverykeys_devpkey.h>
#include <combaseapi.h>
#include <chrono>
#include <thread>
#include <cmath>

// Static member definitions
std::atomic_bool AudioCaptureProviderSystem::device_change_pending_(false);
IMMDeviceEnumerator* AudioCaptureProviderSystem::device_enumerator_ = nullptr;
AudioCaptureProviderSystem::DeviceNotificationClient* AudioCaptureProviderSystem::notification_client_ = nullptr;
// Persist restart strategy across StartCapture calls
static std::atomic<int> g_zero_restart_attempt{0};
static std::wstring g_forced_device_id; // set when a working endpoint is found by scan

// Notification client for device changes
class AudioCaptureProviderSystem::DeviceNotificationClient : public IMMNotificationClient {
    LONG ref_ = 1;
public:
    ULONG STDMETHODCALLTYPE AddRef() override { 
        return InterlockedIncrement(&ref_); 
    }
    
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG ref = InterlockedDecrement(&ref_);
        if (ref == 0) delete this;
        return ref;
    }
    
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override {
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IMMNotificationClient)) {
            *ppvObject = static_cast<IMMNotificationClient*>(this);
            AddRef();
            return S_OK;
        }
        *ppvObject = nullptr;
        return E_NOINTERFACE;
    }
    
    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR) override {
        if (flow == eRender && (role == eConsole || role == eMultimedia)) {
            AudioCaptureProviderSystem::SetDeviceChangePending();
        }
        return S_OK;
    }
    
    // Unused notifications
    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR, DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY) override { return S_OK; }
};

bool AudioCaptureProviderSystem::IsAvailable() const {
    // WASAPI is available on Windows Vista and later
    return true;
}

bool AudioCaptureProviderSystem::Initialize() {
    if (!device_enumerator_) {
        HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, 
                                    CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), 
                                    (void**)&device_enumerator_);
        if (FAILED(hr)) {
            LOG_ERROR("[SystemAudioProvider] Failed to create device enumerator: " + std::to_string(hr));
            return false;
        }
    }
    
    if (device_enumerator_ && !notification_client_) {
        notification_client_ = new DeviceNotificationClient();
        HRESULT hr = device_enumerator_->RegisterEndpointNotificationCallback(notification_client_);
        if (FAILED(hr)) {
            LOG_ERROR("[SystemAudioProvider] Failed to register notification callback: " + std::to_string(hr));
            notification_client_->Release();
            notification_client_ = nullptr;
            return false;
        }
    }
    
    device_change_pending_ = false;
    LOG_DEBUG("[SystemAudioProvider] Initialized successfully.");
    if (Listeningway::ConfigurationManager::Snapshot().debug.debugEnabled) {
        DebugNotes::Add("SYS.Initialize OK");
    }
    return true;
}

void AudioCaptureProviderSystem::Uninitialize() {
    if (device_enumerator_ && notification_client_) {
        device_enumerator_->UnregisterEndpointNotificationCallback(notification_client_);
        notification_client_->Release();
        notification_client_ = nullptr;
    }
    
    if (device_enumerator_) {
        device_enumerator_->Release();
        device_enumerator_ = nullptr;
    }
    
    LOG_DEBUG("[SystemAudioProvider] Uninitialized.");
    if (Listeningway::ConfigurationManager::Snapshot().debug.debugEnabled) {
        DebugNotes::Add("SYS.Uninitialize");
    }
}

bool AudioCaptureProviderSystem::StartCapture(const Listeningway::Configuration& config,
                                             std::atomic_bool& running,
                                             std::thread& thread,
                                             AudioAnalysisData& data,
                                             std::mutex& data_mutex) {
    running = true;
    device_change_pending_ = false;
    LOG_DEBUG("[SystemAudioProvider] Starting audio capture thread.");
    if (Listeningway::ConfigurationManager::Snapshot().debug.debugEnabled) {
        DebugNotes::Add("SYS.StartCapture");
    }

    thread = std::thread([&, config]() {
        try {
            struct WasapiResources {
                IMMDevice* pDevice = nullptr;
                IAudioClient* pAudioClient = nullptr;
                IAudioCaptureClient* pCaptureClient = nullptr;
                WAVEFORMATEX* pwfx = nullptr;
                HANDLE hAudioEvent = nullptr;
                HANDLE hMmcssTask = nullptr;
                DWORD mmcssTaskIndex = 0;
                
                ~WasapiResources() {
                    if (pAudioClient) pAudioClient->Stop();
                    if (hAudioEvent) CloseHandle(hAudioEvent);
                    if (pCaptureClient) pCaptureClient->Release();
                    if (pAudioClient) pAudioClient->Release();
                    if (pDevice) pDevice->Release();
                    if (pwfx) CoTaskMemFree(pwfx);
                    if (hMmcssTask) AvRevertMmThreadCharacteristics(hMmcssTask);
                }
            } res;
            
            HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            if (FAILED(hr)) {
                LOG_ERROR("[SystemAudioProvider] Failed to initialize COM: " + std::to_string(hr));
                running = false;
                if (Listeningway::ConfigurationManager::Snapshot().debug.debugEnabled) {
                    DebugNotes::Add("SYS.COM init FAIL");
                }
                return;
            }
            
            // Try to boost capture thread priority using MMCSS
            res.hMmcssTask = AvSetMmThreadCharacteristicsW(L"Audio", &res.mmcssTaskIndex);
            if (res.hMmcssTask) {
                AvSetMmThreadPriority(res.hMmcssTask, AVRT_PRIORITY_HIGH);
                if (Listeningway::ConfigurationManager::Snapshot().debug.debugEnabled) {
                    DebugNotes::Add("SYS.MMCSS on");
                }
            } else {
                if (Listeningway::ConfigurationManager::Snapshot().debug.debugEnabled) {
                    DebugNotes::Add("SYS.MMCSS fail");
                }
            }

            // WASAPI device and client setup
            UINT32 bufferFrameCount = 0;
            
            if (!device_enumerator_) {
                LOG_ERROR("[SystemAudioProvider] Device enumerator is null!");
                running = false;
                CoUninitialize();
                if (Listeningway::ConfigurationManager::Snapshot().debug.debugEnabled) {
                    DebugNotes::Add("SYS.Enumerator null");
                }
                return;
            }
            
            // If persistent zeros after multiple strategies, try to find a working endpoint by scanning
            if (g_zero_restart_attempt.load() >= 4 && g_forced_device_id.empty()) {
                if (Listeningway::ConfigurationManager::Snapshot().debug.debugEnabled) {
                    DebugNotes::Add("SYS.Scan: probing endpoints for non-zero loopback");
                }
                IMMDeviceCollection* coll = nullptr;
                if (SUCCEEDED(device_enumerator_->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &coll)) && coll) {
                    UINT count = 0; coll->GetCount(&count);
                    for (UINT i = 0; i < count && g_forced_device_id.empty(); ++i) {
                        IMMDevice* dev = nullptr;
                        if (FAILED(coll->Item(i, &dev)) || !dev) continue;
                        // Friendly name
                        std::wstring name;
                        IPropertyStore* store = nullptr; PROPVARIANT var; PropVariantInit(&var);
                        if (SUCCEEDED(dev->OpenPropertyStore(STGM_READ, &store)) && store) {
                            if (SUCCEEDED(store->GetValue(PKEY_Device_FriendlyName, &var)) && var.vt == VT_LPWSTR && var.pwszVal) {
                                name = var.pwszVal;
                            }
                        }
                        if (store) store->Release(); PropVariantClear(&var);
                        LPWSTR id = nullptr; dev->GetId(&id);
                        // Probe for non-zero data quickly
                        bool ok = false;
                        IAudioClient* ac = nullptr; IAudioCaptureClient* cap = nullptr; WAVEFORMATEX* pfmt = nullptr;
                        if (SUCCEEDED(dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&ac)) && ac) {
                            if (SUCCEEDED(ac->GetMixFormat(&pfmt)) && pfmt) {
                                if (SUCCEEDED(ac->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, 200000, 0, pfmt, nullptr))) {
                                    if (SUCCEEDED(ac->GetService(__uuidof(IAudioCaptureClient), (void**)&cap)) && cap) {
                                        if (SUCCEEDED(ac->Start())) {
                                            for (int tries = 0; tries < 10 && !ok; ++tries) {
                                                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                                                UINT32 pk = 0; if (FAILED(cap->GetNextPacketSize(&pk)) || pk == 0) continue;
                                                BYTE* d=nullptr; UINT32 nf=0; DWORD fl=0; UINT64 dp=0, qp=0;
                                                if (SUCCEEDED(cap->GetBuffer(&d, &nf, &fl, &dp, &qp)) && d && nf>0 && (fl & AUDCLNT_BUFFERFLAGS_SILENT)==0) {
                                                    // inspect a few samples as float/pcm
                                                    bool isF=false, isP=false; WORD bps = pfmt->wBitsPerSample; WORD vbits = bps; UINT ch=pfmt->nChannels;
                                                    if (pfmt->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) isF = true; else if (pfmt->wFormatTag == WAVE_FORMAT_PCM) isP = true; else if (pfmt->wFormatTag == WAVE_FORMAT_EXTENSIBLE) { auto* e=reinterpret_cast<WAVEFORMATEXTENSIBLE*>(pfmt); vbits=e->Samples.wValidBitsPerSample; if (e->SubFormat==KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) isF=true; else if (e->SubFormat==KSDATAFORMAT_SUBTYPE_PCM) isP=true; }
                                                    size_t n = std::min<size_t>((size_t)nf*ch, 256);
                                                    float mn=1e9f, mx=-1e9f;
                                                    if (isF) { const float* f=reinterpret_cast<const float*>(d); for (size_t i=0;i<n;++i){ float v=f[i]; mn=std::min(mn,v); mx=std::max(mx,v);} }
                                                    else if (isP) {
                                                        if (bps==16){ const int16_t* s=reinterpret_cast<const int16_t*>(d); for (size_t i=0;i<n;++i){ float v=float(s[i])/32768.0f; mn=std::min(mn,v); mx=std::max(mx,v);} }
                                                        else if (bps==24 || (bps==32 && vbits==24)){ if (bps==24){ const uint8_t* b=reinterpret_cast<const uint8_t*>(d); for (size_t i=0;i<n;++i){ int32_t v=(int32_t(b[i*3+2])<<24)|(int32_t(b[i*3+1])<<16)|(int32_t(b[i*3+0])<<8); v>>=8; float fv=std::max(-1.0f,std::min(1.0f,float(v)/8388608.0f)); mn=std::min(mn,fv); mx=std::max(mx,fv);} } else { const int32_t* s32=reinterpret_cast<const int32_t*>(d); for (size_t i=0;i<n;++i){ int32_t v=s32[i]>>8; float fv=std::max(-1.0f,std::min(1.0f,float(v)/8388608.0f)); mn=std::min(mn,fv); mx=std::max(mx,fv);} } }
                                                        else if (bps==32){ const int32_t* s=reinterpret_cast<const int32_t*>(d); for (size_t i=0;i<n;++i){ float v=std::max(-1.0f,std::min(1.0f,float(s[i])/2147483648.0f)); mn=std::min(mn,v); mx=std::max(mx,v);} }
                                                    }
                                                    if (!(mn==0.0f && mx==0.0f)) ok = true;
                                                }
                                                if (cap) cap->ReleaseBuffer(nf);
                                            }
                                            ac->Stop();
                                        }
                                    }
                                }
                            }
                        }
                        if (cap) cap->Release();
                        if (ac) ac->Release();
                        if (pfmt) CoTaskMemFree(pfmt);
                        if (ok && id) {
                            g_forced_device_id = id;
                            if (Listeningway::ConfigurationManager::Snapshot().debug.debugEnabled) {
                                std::string n = name.empty() ? std::string("<unnamed>") : std::string(name.begin(), name.end());
                                DebugNotes::Add(std::string("SYS.Endpoint: forced=") + n);
                            }
                        }
                        if (id) CoTaskMemFree(id);
                        dev->Release();
                    }
                    coll->Release();
                }
                if (g_forced_device_id.empty()) {
                    if (Listeningway::ConfigurationManager::Snapshot().debug.debugEnabled) {
                        DebugNotes::Add("SYS.Scan: no endpoint produced signal");
                        DebugNotes::Add("SYS.Hint: Spatial/bitstream (Atmos/DTS/RAW) may mute loopback; try Spatial=Off, PCM output");
                    }
                }
            }

            // Pick endpoint role and capture mode based on restart strategy
            const int attempt = g_zero_restart_attempt.load();
            const bool useConsoleRole = (attempt % 2) == 1; // alternate roles: 0=Multimedia,1=Console,2=Multimedia,3=Console...
            const bool useEventMode = (attempt / 2) % 2 == 0; // 0-1: event, 2-3: polling, 4-5: event...
            // Select role
            if (!g_forced_device_id.empty()) {
                hr = device_enumerator_->GetDevice(g_forced_device_id.c_str(), &res.pDevice);
            } else {
                hr = device_enumerator_->GetDefaultAudioEndpoint(eRender, useConsoleRole ? eConsole : eMultimedia, &res.pDevice);
            }
            if (FAILED(hr)) {
                LOG_ERROR("[SystemAudioProvider] Failed to get default audio endpoint: " + std::to_string(hr));
                running = false;
                CoUninitialize();
                if (Listeningway::ConfigurationManager::Snapshot().debug.debugEnabled) {
                    DebugNotes::Add("SYS.GetDefaultEndpoint FAIL");
                }
                return;
            }
            if (Listeningway::ConfigurationManager::Snapshot().debug.debugEnabled) {
                if (!g_forced_device_id.empty()) {
                    DebugNotes::Add("SYS.Endpoint: using forced device");
                } else {
                    DebugNotes::Add(std::string("SYS.Endpoint: role=") + (useConsoleRole ? "Console" : "Multimedia"));
                }
            }
            
            hr = res.pDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&res.pAudioClient);
            if (FAILED(hr)) {
                LOG_ERROR("[SystemAudioProvider] Failed to activate audio client: " + std::to_string(hr));
                running = false;
                CoUninitialize();
                if (Listeningway::ConfigurationManager::Snapshot().debug.debugEnabled) {
                    DebugNotes::Add("SYS.Activate client FAIL");
                }
                return;
            }
            
            hr = res.pAudioClient->GetMixFormat(&res.pwfx);
            if (FAILED(hr)) {
                LOG_ERROR("[SystemAudioProvider] Failed to get mix format: " + std::to_string(hr));
                running = false;
                CoUninitialize();
                if (Listeningway::ConfigurationManager::Snapshot().debug.debugEnabled) {
                    DebugNotes::Add("SYS.GetMixFormat FAIL");
                }
                return;
            }
            
            // Determine format characteristics
            bool isFloatFormat = false;
            bool isPcmFormat = false;
            WORD bitsPerSample = res.pwfx->wBitsPerSample;
            WORD validBits = res.pwfx->wBitsPerSample;
            if (res.pwfx->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
                isFloatFormat = true;
            } else if (res.pwfx->wFormatTag == WAVE_FORMAT_PCM) {
                isPcmFormat = true;
            } else if (res.pwfx->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
                WAVEFORMATEXTENSIBLE* pwfex = reinterpret_cast<WAVEFORMATEXTENSIBLE*>(res.pwfx);
                validBits = pwfex->Samples.wValidBitsPerSample;
                if (pwfex->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) {
                    isFloatFormat = true;
                } else if (pwfex->SubFormat == KSDATAFORMAT_SUBTYPE_PCM) {
                    isPcmFormat = true;
                }
            }
            if (Listeningway::ConfigurationManager::Snapshot().debug.debugEnabled) {
                std::string fmt = isFloatFormat ? "Float" : (isPcmFormat ? "PCM" : "Other");
                std::string extra;
                if (res.pwfx->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
                    auto* pwfex = reinterpret_cast<WAVEFORMATEXTENSIBLE*>(res.pwfx);
                    extra = ", mask=0x" + std::to_string(static_cast<unsigned long>(pwfex->dwChannelMask));
                }
                DebugNotes::Add(
                    std::string("SYS.Mix: ch=") + std::to_string(res.pwfx->nChannels) +
                    ", "+fmt+", "+std::to_string(bitsPerSample)+"b (valid="+std::to_string(validBits)+
                    "), "+std::to_string(res.pwfx->nSamplesPerSec)+"Hz" + extra);
            }

            // Detect 7.1 layout variant based on channel mask if available
            if (res.pwfx->nChannels == 8 && res.pwfx->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
                auto* pwfex = reinterpret_cast<WAVEFORMATEXTENSIBLE*>(res.pwfx);
                // Typical masks
                // Classic (rear first): SPEAKER_BACK_LEFT|SPEAKER_BACK_RIGHT present (0x60), sides present (0x600)
                // Surround (side first): also present; differentiate by order is not possible via mask alone.
                // Heuristic: If mask includes BACK_LEFT/BACK_RIGHT and SIDE_LEFT/SIDE_RIGHT, prefer Surround order for many drivers
                // but we’ll expose detection and let analyzer use vector sum; still set a hint.
                using namespace lw::audio::analysis;
                Layout71 hint = Layout71::Surround; // default to SL,SR first which is common on Windows
                SetLayout71(hint);
                if (Listeningway::ConfigurationManager::Snapshot().debug.debugEnabled) {
                    DebugNotes::Add("SYS.7.1 layout: hint=Surround");
                }
            }
            
            if (useEventMode) {
                res.hAudioEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
                if (!res.hAudioEvent) {
                    LOG_ERROR("[SystemAudioProvider] Failed to create audio event.");
                    running = false;
                    CoUninitialize();
                    if (Listeningway::ConfigurationManager::Snapshot().debug.debugEnabled) {
                        DebugNotes::Add("SYS.CreateEvent FAIL");
                    }
                    return;
                }
            }
            
            REFERENCE_TIME hnsRequestedDuration = 200000;
            // Prefer a canonical downmixed float stereo format with auto-convert to avoid exotic 7.1/object paths returning zeros
            // We'll request 2ch float at the device sample rate with AUTOCONVERT+SRC and fall back to device mix format if unsupported
            WAVEFORMATEXTENSIBLE wfxStereo = {};
            wfxStereo.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
            wfxStereo.Format.nChannels = 2;
            wfxStereo.Format.nSamplesPerSec = res.pwfx->nSamplesPerSec;
            wfxStereo.Format.wBitsPerSample = 32;
            wfxStereo.Format.nBlockAlign = (wfxStereo.Format.nChannels * wfxStereo.Format.wBitsPerSample) / 8;
            wfxStereo.Format.nAvgBytesPerSec = wfxStereo.Format.nSamplesPerSec * wfxStereo.Format.nBlockAlign;
            wfxStereo.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
            wfxStereo.Samples.wValidBitsPerSample = 32;
            wfxStereo.dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
            wfxStereo.SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
            WAVEFORMATEX* pChosenInitFormat = res.pwfx; // default to mix format
            DWORD initFlags = AUDCLNT_STREAMFLAGS_LOOPBACK | (useEventMode ? AUDCLNT_STREAMFLAGS_EVENTCALLBACK : 0);
            // Try stereo downmix with autoconvert first
            WAVEFORMATEX* pClosest = nullptr;
            HRESULT hrStereo = res.pAudioClient->IsFormatSupported(AUDCLNT_SHAREMODE_SHARED, reinterpret_cast<WAVEFORMATEX*>(&wfxStereo), &pClosest);
            if (hrStereo == S_OK) {
                pChosenInitFormat = reinterpret_cast<WAVEFORMATEX*>(&wfxStereo);
                initFlags |= AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
                if (Listeningway::ConfigurationManager::Snapshot().debug.debugEnabled) {
                    DebugNotes::Add("SYS.Init: request f32 stereo downmix");
                }
            } else {
                if (Listeningway::ConfigurationManager::Snapshot().debug.debugEnabled) {
                    DebugNotes::Add("SYS.Init: stereo downmix unsupported -> using mix format");
                }
            }
            if (pClosest) { CoTaskMemFree(pClosest); pClosest = nullptr; }
            hr = res.pAudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                            initFlags,
                                            hnsRequestedDuration, 0, pChosenInitFormat, nullptr);
            if (SUCCEEDED(hr) && pChosenInitFormat == reinterpret_cast<WAVEFORMATEX*>(&wfxStereo)) {
                // We successfully initialized stereo float with auto-convert
                isFloatFormat = true;
                isPcmFormat = false;
                bitsPerSample = 32;
                validBits = 32;
                if (Listeningway::ConfigurationManager::Snapshot().debug.debugEnabled) {
                    DebugNotes::Add("SYS.Init: f32 stereo active");
                }
            }
            
            if (hr == AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED) {
                hr = res.pAudioClient->GetBufferSize(&bufferFrameCount);
                if (FAILED(hr)) {
                    LOG_ERROR("[SystemAudioProvider] Failed to get buffer size: " + std::to_string(hr));
                    running = false;
                    CoUninitialize();
                    return;
                }
                hnsRequestedDuration = (REFERENCE_TIME)((10000.0 * 1000 / res.pwfx->nSamplesPerSec * bufferFrameCount) + 0.5);
                hr = res.pAudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, 
                                                AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK, 
                                                hnsRequestedDuration, 0, res.pwfx, nullptr);
            }
            
            if (FAILED(hr)) {
                // Attempt a PCM 16-bit fallback WAVEFORMATEXTENSIBLE in shared mode
                WAVEFORMATEXTENSIBLE wfx_pcm = {};
                wfx_pcm.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
                // match channels and rate to the chosen init format (stereo or mix)
                wfx_pcm.Format.nChannels = (pChosenInitFormat ? pChosenInitFormat->nChannels : res.pwfx->nChannels);
                wfx_pcm.Format.nSamplesPerSec = (pChosenInitFormat ? pChosenInitFormat->nSamplesPerSec : res.pwfx->nSamplesPerSec);
                wfx_pcm.Format.wBitsPerSample = 16;
                wfx_pcm.Format.nBlockAlign = (wfx_pcm.Format.nChannels * wfx_pcm.Format.wBitsPerSample) / 8;
                wfx_pcm.Format.nAvgBytesPerSec = wfx_pcm.Format.nSamplesPerSec * wfx_pcm.Format.nBlockAlign;
                wfx_pcm.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
                wfx_pcm.Samples.wValidBitsPerSample = 16;
                if (pChosenInitFormat && pChosenInitFormat->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
                    wfx_pcm.dwChannelMask = reinterpret_cast<WAVEFORMATEXTENSIBLE*>(pChosenInitFormat)->dwChannelMask;
                } else if (pChosenInitFormat && pChosenInitFormat->nChannels == 2) {
                    wfx_pcm.dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
                } else {
                    wfx_pcm.dwChannelMask = (res.pwfx->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
                        ? reinterpret_cast<WAVEFORMATEXTENSIBLE*>(res.pwfx)->dwChannelMask
                        : 0;
                }
                wfx_pcm.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
                WAVEFORMATEX* closest = nullptr;
                HRESULT hrSup = res.pAudioClient->IsFormatSupported(AUDCLNT_SHAREMODE_SHARED, reinterpret_cast<WAVEFORMATEX*>(&wfx_pcm), &closest);
                if (hrSup == S_OK) {
                    hr = res.pAudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED,
                        initFlags,
                        hnsRequestedDuration, 0, reinterpret_cast<WAVEFORMATEX*>(&wfx_pcm), nullptr);
                    if (SUCCEEDED(hr)) {
                        isFloatFormat = false;
                        isPcmFormat = true;
                        bitsPerSample = 16;
                        validBits = 16;
                        if (Listeningway::ConfigurationManager::Snapshot().debug.debugEnabled) {
                            DebugNotes::Add("SYS.Init: fallback PCM16");
                        }
                    }
                }
                if (closest) CoTaskMemFree(closest);
                if (FAILED(hr)) {
                    LOG_ERROR("[SystemAudioProvider] Failed to initialize audio client: " + std::to_string(hr));
                    running = false;
                    CoUninitialize();
                    if (Listeningway::ConfigurationManager::Snapshot().debug.debugEnabled) {
                        DebugNotes::Add("SYS.AudioClient init FAIL");
                    }
                    return;
                }
            }
            
            hr = res.pAudioClient->GetBufferSize(&bufferFrameCount);
            if (FAILED(hr)) {
                LOG_ERROR("[SystemAudioProvider] Failed to get buffer size (2): " + std::to_string(hr));
                running = false;
                CoUninitialize();
                if (Listeningway::ConfigurationManager::Snapshot().debug.debugEnabled) {
                    DebugNotes::Add("SYS.GetBufferSize FAIL");
                }
                return;
            }
            if (useEventMode) {
                hr = res.pAudioClient->SetEventHandle(res.hAudioEvent);
                if (FAILED(hr)) {
                    LOG_ERROR("[SystemAudioProvider] Failed to set event handle: " + std::to_string(hr));
                    running = false;
                    CoUninitialize();
                    if (Listeningway::ConfigurationManager::Snapshot().debug.debugEnabled) {
                        DebugNotes::Add("SYS.SetEventHandle FAIL");
                    }
                    return;
                }
            }
            
            hr = res.pAudioClient->GetService(__uuidof(IAudioCaptureClient), (void**)&res.pCaptureClient);
            if (FAILED(hr)) {
                LOG_ERROR("[SystemAudioProvider] Failed to get capture client: " + std::to_string(hr));
                running = false;
                CoUninitialize();
                if (Listeningway::ConfigurationManager::Snapshot().debug.debugEnabled) {
                    DebugNotes::Add("SYS.GetCaptureClient FAIL");
                }
                return;
            }
            
            hr = res.pAudioClient->Start();
            if (FAILED(hr)) {
                LOG_ERROR("[SystemAudioProvider] Failed to start audio client: " + std::to_string(hr));
                running = false;
                CoUninitialize();
                if (Listeningway::ConfigurationManager::Snapshot().debug.debugEnabled) {
                    DebugNotes::Add("SYS.AudioClient start FAIL");
                }
                return;
            }
            
            LOG_DEBUG("[SystemAudioProvider] Entering main capture loop.");
            if (Listeningway::ConfigurationManager::Snapshot().debug.debugEnabled) {
                DebugNotes::Add(std::string("SYS.Loop: enter ") + (useEventMode ? "mode=event" : "mode=poll"));
            }
            
            // Main capture loop
            using clock = std::chrono::steady_clock;
            auto lastWaitTimeoutLog = clock::now();
            auto lastZeroPacketLog = clock::now();
            auto lastSilentLog = clock::now();
            auto lastAnalysisDisabledLog = clock::now();
            auto lastPacketInfoLog = clock::now();
            int zero_inspect_streak = 0;
            while (running.load()) {
                if (device_change_pending_.load()) {
                    LOG_DEBUG("[SystemAudioProvider] Audio device change detected. Restarting capture.");
                    break; // Exit thread to allow restart
                }
                if (useEventMode) {
                    DWORD waitResult = WaitForSingleObject(res.hAudioEvent, 200);
                    if (!running.load()) break;
                    if (waitResult != WAIT_OBJECT_0) {
                        if (waitResult == WAIT_TIMEOUT) {
                            // Periodic timeout logging and padding introspection
                            auto now = clock::now();
                            if (Listeningway::ConfigurationManager::Snapshot().debug.debugEnabled &&
                                now - lastWaitTimeoutLog > std::chrono::seconds(3)) {
                                UINT32 padding = 0;
                                if (res.pAudioClient && SUCCEEDED(res.pAudioClient->GetCurrentPadding(&padding))) {
                                    DebugNotes::Add(std::string("SYS.Wait: timeout, pad=") + std::to_string(padding));
                                } else {
                                    DebugNotes::Add("SYS.Wait: timeout");
                                }
                                lastWaitTimeoutLog = now;
                            }
                            continue;
                        } else {
                            auto now = clock::now();
                            if (Listeningway::ConfigurationManager::Snapshot().debug.debugEnabled &&
                                now - lastWaitTimeoutLog > std::chrono::seconds(3)) {
                                DebugNotes::Add(std::string("SYS.Wait: other ") + std::to_string(waitResult));
                                lastWaitTimeoutLog = now;
                            }
                            continue;
                        }
                    }
                } else {
                    // polling mode pacing
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }

                // Drain packets (common for both modes)
                    // Drain all available packets
                    for (;;) {
                        UINT32 packetFrames = 0;
                        hr = res.pCaptureClient->GetNextPacketSize(&packetFrames);
                        if (FAILED(hr)) {
                            LOG_ERROR("[SystemAudioProvider] GetNextPacketSize failed: 0x" + std::to_string(static_cast<unsigned long>(hr)));
                            break;
                        }
                        if (packetFrames == 0) {
                            // Occasionally note zero packets after event
                            auto now = clock::now();
                            if (Listeningway::ConfigurationManager::Snapshot().debug.debugEnabled &&
                                now - lastZeroPacketLog > std::chrono::seconds(3)) {
                                DebugNotes::Add(useEventMode ? "SYS.Packet: none after event" : "SYS.Packet: none");
                                lastZeroPacketLog = now;
                            }
                            break;
                        }

                        BYTE* pData = nullptr;
                        UINT32 numFramesAvailable = 0;
                        DWORD flags = 0;
                        UINT64 devicePosition = 0;
                        UINT64 qpcPosition = 0;
                        hr = res.pCaptureClient->GetBuffer(&pData, &numFramesAvailable, &flags, &devicePosition, &qpcPosition);
                        if (SUCCEEDED(hr)) {
                            // Periodic packet info
                            auto now = clock::now();
                            if (Listeningway::ConfigurationManager::Snapshot().debug.debugEnabled &&
                                now - lastPacketInfoLog > std::chrono::seconds(1)) {
                                std::string s = std::string("SYS.Packet: frames=") + std::to_string(numFramesAvailable) +
                                                ", flags=0x" + std::to_string(flags) +
                                                (flags & AUDCLNT_BUFFERFLAGS_SILENT ? ", silent=Y" : ", silent=N");
                                DebugNotes::Add(s);
                                lastPacketInfoLog = now;
                            }

                            const bool isSilent = (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0;
                            if (!isSilent && pData && numFramesAvailable > 0) {
                                // Check if analysis is enabled in config (thread-safe snapshot)
                                if (!Listeningway::ConfigurationManager::Snapshot().audio.analysisEnabled) {
                                    auto now2 = clock::now();
                                    if (Listeningway::ConfigurationManager::Snapshot().debug.debugEnabled &&
                                        now2 - lastAnalysisDisabledLog > std::chrono::seconds(5)) {
                                        DebugNotes::Add("SYS.Drop: analysis disabled");
                                        lastAnalysisDisabledLog = now2;
                                    }
                                    res.pCaptureClient->ReleaseBuffer(numFramesAvailable);
                                    continue;
                                }
                                size_t channelsChosen = (pChosenInitFormat ? pChosenInitFormat->nChannels : res.pwfx->nChannels);
                                const size_t channelsDevice = res.pwfx->nChannels;
                                size_t channelsForAnalysis = channelsChosen;
                                bool analyzed = false;
                if (isFloatFormat) {
                                    // Direct float path
                                    // Quick sample inspection (rate-limited)
                                    auto nowi = clock::now();
                                    static thread_local clock::time_point last_inspect;
                                    if (last_inspect.time_since_epoch().count() == 0) last_inspect = nowi - std::chrono::seconds(2);
                                    if (Listeningway::ConfigurationManager::Snapshot().debug.debugEnabled &&
                                        nowi - last_inspect > std::chrono::seconds(1)) {
                                        const float* f = reinterpret_cast<const float*>(pData);
                                        size_t totalChosen = static_cast<size_t>(numFramesAvailable) * channelsChosen;
                                        size_t n = std::min<size_t>(totalChosen, 256);
                                        float mn = 1e9f, mx = -1e9f, a = 0.0f;
                                        for (size_t i = 0; i < n; ++i) { float v = f[i]; mn = std::min(mn, v); mx = std::max(mx, v); a += std::fabs(v); }
                                        a /= std::max<size_t>(1, n);
                                        // Optional: also inspect as device mix channels if different
                                        if (channelsDevice != channelsChosen) {
                                            size_t totalDev = static_cast<size_t>(numFramesAvailable) * channelsDevice;
                                            size_t n2 = std::min<size_t>(totalDev, 256);
                                            float mn2 = 1e9f, mx2 = -1e9f, a2 = 0.0f;
                                            const float* f2 = reinterpret_cast<const float*>(pData);
                                            for (size_t i = 0; i < n2; ++i) { float v = f2[i]; mn2 = std::min(mn2, v); mx2 = std::max(mx2, v); a2 += std::fabs(v); }
                                            a2 /= std::max<size_t>(1, n2);
                                            DebugNotes::Add(std::string("SYS.SamplesF: min=") + std::to_string(mn) + ", max=" + std::to_string(mx) + ", aavg=" + std::to_string(a));
                                            DebugNotes::Add(std::string("SYS.SamplesF[assume8]: min=") + std::to_string(mn2) + ", max=" + std::to_string(mx2) + ", aavg=" + std::to_string(a2));
                                            // If chosen scan is zero but device scan shows signal, switch to device channels for analysis
                                            if (mn == 0.0f && mx == 0.0f && !(mn2 == 0.0f && mx2 == 0.0f)) {
                                                channelsForAnalysis = channelsDevice;
                                            }
                                            if ((mn == 0.0f && mx == 0.0f) && (mn2 == 0.0f && mx2 == 0.0f)) {
                                                zero_inspect_streak++;
                                            } else {
                                                zero_inspect_streak = 0;
                                                if (g_zero_restart_attempt.load() != 0) g_zero_restart_attempt.store(0);
                                            }
                                        } else {
                                            DebugNotes::Add(std::string("SYS.SamplesF: min=") + std::to_string(mn) + ", max=" + std::to_string(mx) + ", aavg=" + std::to_string(a));
                                            if (mn == 0.0f && mx == 0.0f) { zero_inspect_streak++; } else { zero_inspect_streak = 0; if (g_zero_restart_attempt.load() != 0) g_zero_restart_attempt.store(0); }
                                        }
                    last_inspect = nowi;
                                    }
                                    std::lock_guard<std::mutex> _audio_lock(data_mutex);
                                    extern AudioAnalyzer g_audio_analyzer;
                                    g_audio_analyzer.AnalyzeAudioBuffer(reinterpret_cast<float*>(pData),
                                                                        numFramesAvailable,
                                                                        channelsForAnalysis,
                                                                        data);
                                    analyzed = true;
                                } else if (isPcmFormat) {
                                    // Convert PCM to float [-1,1]
                                    static thread_local std::vector<float> tl_conv;
                                    const size_t totalChosen = static_cast<size_t>(numFramesAvailable) * channelsChosen;
                                    if (tl_conv.size() < totalChosen) tl_conv.resize(totalChosen);
                                    const WORD bps = bitsPerSample;
                                    if (bps == 16) {
                                        const int16_t* s = reinterpret_cast<const int16_t*>(pData);
                                        for (size_t i = 0; i < totalChosen; ++i) {
                                            tl_conv[i] = static_cast<float>(s[i]) / 32768.0f;
                                        }
                                    } else if (bps == 24 || (bps == 32 && validBits == 24)) {
                                        // 24-bit packed or 24-in-32: read little-endian 24-bit and sign-extend
                                        const uint8_t* b = reinterpret_cast<const uint8_t*>(pData);
                                        if (bps == 24) {
                                            for (size_t i = 0; i < totalChosen; ++i) {
                                                int32_t v = (int32_t(b[i*3+2]) << 24) | (int32_t(b[i*3+1]) << 16) | (int32_t(b[i*3+0]) << 8);
                                                v >>= 8; // sign-extend 24-bit
                                                tl_conv[i] = std::max(-1.0f, std::min(1.0f, float(v) / 8388608.0f));
                                            }
                                        } else { // 24-in-32 container
                                            const int32_t* s32 = reinterpret_cast<const int32_t*>(pData);
                                            for (size_t i = 0; i < totalChosen; ++i) {
                                                int32_t v = s32[i] >> 8; // assume 24 valid MSBs
                                                tl_conv[i] = std::max(-1.0f, std::min(1.0f, float(v) / 8388608.0f));
                                            }
                                        }
                                    } else if (bps == 32) {
                                        // 32-bit signed PCM
                                        const int32_t* s = reinterpret_cast<const int32_t*>(pData);
                                        for (size_t i = 0; i < totalChosen; ++i) {
                                            tl_conv[i] = std::max(-1.0f, std::min(1.0f, float(s[i]) / 2147483648.0f));
                                        }
                                    } else {
                                        // Unsupported PCM depth; skip
                                        res.pCaptureClient->ReleaseBuffer(numFramesAvailable);
                                        continue;
                                    }
                                    // Quick sample inspection (rate-limited)
                                    auto nowi2 = clock::now();
                                    static thread_local clock::time_point last_inspect2;
                                    if (last_inspect2.time_since_epoch().count() == 0) last_inspect2 = nowi2 - std::chrono::seconds(2);
                                    if (Listeningway::ConfigurationManager::Snapshot().debug.debugEnabled &&
                                        nowi2 - last_inspect2 > std::chrono::seconds(1)) {
                                        size_t n2 = std::min<size_t>(totalChosen, 256);
                                        float mn2 = 1e9f, mx2 = -1e9f, a2 = 0.0f;
                                        for (size_t i = 0; i < n2; ++i) { float v = tl_conv[i]; mn2 = std::min(mn2, v); mx2 = std::max(mx2, v); a2 += std::fabs(v); }
                                        a2 /= std::max<size_t>(1, n2);
                                        if (channelsDevice != channelsChosen) {
                                            // Reinterpret source as device channels directly from pData (no additional conversion) just for inspection
                                            size_t totalDev = static_cast<size_t>(numFramesAvailable) * channelsDevice;
                                            size_t n3 = std::min<size_t>(totalDev, 256);
                                            float mn3 = 1e9f, mx3 = -1e9f, a3 = 0.0f;
                                            // Convert a tiny window on-the-fly for inspection
                                            const WORD bpsLoc = bitsPerSample;
                                            if (bpsLoc == 16) {
                                                const int16_t* s = reinterpret_cast<const int16_t*>(pData);
                                                for (size_t i = 0; i < n3; ++i) { float v = float(s[i]) / 32768.0f; mn3 = std::min(mn3, v); mx3 = std::max(mx3, v); a3 += std::fabs(v); }
                                            } else if (bpsLoc == 24 || (bpsLoc == 32 && validBits == 24)) {
                                                if (bpsLoc == 24) {
                                                    const uint8_t* b = reinterpret_cast<const uint8_t*>(pData);
                                                    for (size_t i = 0; i < n3; ++i) { int32_t v = (int32_t(b[i*3+2]) << 24) | (int32_t(b[i*3+1]) << 16) | (int32_t(b[i*3+0]) << 8); v >>= 8; float fv = std::max(-1.0f, std::min(1.0f, float(v) / 8388608.0f)); mn3 = std::min(mn3, fv); mx3 = std::max(mx3, fv); a3 += std::fabs(fv); }
                                                } else {
                                                    const int32_t* s32 = reinterpret_cast<const int32_t*>(pData);
                                                    for (size_t i = 0; i < n3; ++i) { int32_t v = s32[i] >> 8; float fv = std::max(-1.0f, std::min(1.0f, float(v) / 8388608.0f)); mn3 = std::min(mn3, fv); mx3 = std::max(mx3, fv); a3 += std::fabs(fv); }
                                                }
                                            } else if (bpsLoc == 32) {
                                                const int32_t* s = reinterpret_cast<const int32_t*>(pData);
                                                for (size_t i = 0; i < n3; ++i) { float v = std::max(-1.0f, std::min(1.0f, float(s[i]) / 2147483648.0f)); mn3 = std::min(mn3, v); mx3 = std::max(mx3, v); a3 += std::fabs(v); }
                                            }
                                            a3 /= std::max<size_t>(1, n3);
                                            DebugNotes::Add(std::string("SYS.SamplesP: min=") + std::to_string(mn2) + ", max=" + std::to_string(mx2) + ", aavg=" + std::to_string(a2));
                                            DebugNotes::Add(std::string("SYS.SamplesP[assume8]: min=") + std::to_string(mn3) + ", max=" + std::to_string(mx3) + ", aavg=" + std::to_string(a3));
                                            if (mn2 == 0.0f && mx2 == 0.0f && !(mn3 == 0.0f && mx3 == 0.0f)) {
                                                channelsForAnalysis = channelsDevice;
                                            }
                                            if ((mn2 == 0.0f && mx2 == 0.0f) && (mn3 == 0.0f && mx3 == 0.0f)) { zero_inspect_streak++; } else { zero_inspect_streak = 0; if (g_zero_restart_attempt.load() != 0) g_zero_restart_attempt.store(0); }
                                        } else {
                                            DebugNotes::Add(std::string("SYS.SamplesP: min=") + std::to_string(mn2) + ", max=" + std::to_string(mx2) + ", aavg=" + std::to_string(a2));
                                            if (mn2 == 0.0f && mx2 == 0.0f) { zero_inspect_streak++; } else { zero_inspect_streak = 0; if (g_zero_restart_attempt.load() != 0) g_zero_restart_attempt.store(0); }
                                        }
                                        last_inspect2 = nowi2;
                                    }
                                    std::lock_guard<std::mutex> _audio_lock(data_mutex);
                                    extern AudioAnalyzer g_audio_analyzer;
                                    g_audio_analyzer.AnalyzeAudioBuffer(tl_conv.data(),
                                                                        numFramesAvailable,
                                                                        channelsForAnalysis,
                                                                        data);
                                    analyzed = true;
                                }
                                if (analyzed) {
                                    auto now3 = clock::now();
                                    if (Listeningway::ConfigurationManager::Snapshot().debug.debugEnabled &&
                                        now3 - lastPacketInfoLog > std::chrono::seconds(1)) {
                                        DebugNotes::Add(std::string("SYS.Analyze: ch=") + std::to_string(channelsForAnalysis) +
                                                        ", vol=" + std::to_string(data.volume));
                                        // reuse lastPacketInfoLog for 1s pacing
                                        lastPacketInfoLog = now3;
                                    }
                                }
                            } else {
                                auto now2 = clock::now();
                                if (Listeningway::ConfigurationManager::Snapshot().debug.debugEnabled &&
                                    now2 - lastSilentLog > std::chrono::seconds(3)) {
                                    DebugNotes::Add("SYS.Packet: silent");
                                    lastSilentLog = now2;
                                }
                            }
                            res.pCaptureClient->ReleaseBuffer(numFramesAvailable);
                            // If we consistently see zeros in samples despite non-silent flags, trigger a provider restart to try another strategy
                            if (zero_inspect_streak >= 5) {
                                if (Listeningway::ConfigurationManager::Snapshot().debug.debugEnabled) {
                                    DebugNotes::Add("SYS.ZeroSamples: restart requested");
                                }
                                g_zero_restart_attempt.fetch_add(1);
                                device_change_pending_ = true;
                                return; // exit thread to restart
                            }
                        } else {
                            // Improved WASAPI error handling
                            switch (hr) {
                                case AUDCLNT_E_BUFFER_ERROR:
                                    LOG_ERROR("[SystemAudioProvider] GetBuffer failed: Audio buffer error");
                                    break;
                                case AUDCLNT_E_BUFFER_TOO_LARGE:
                                    LOG_ERROR("[SystemAudioProvider] GetBuffer failed: Buffer too large");
                                    break;
                                case AUDCLNT_E_BUFFER_SIZE_ERROR:
                                    LOG_ERROR("[SystemAudioProvider] GetBuffer failed: Buffer size error");
                                    break;
                                case AUDCLNT_E_OUT_OF_ORDER:
                                    LOG_ERROR("[SystemAudioProvider] GetBuffer failed: Out of order");
                                    break;
                                case AUDCLNT_E_DEVICE_INVALIDATED:
                                    LOG_ERROR("[SystemAudioProvider] GetBuffer failed: Device invalidated - attempting recovery");
                                    // Device was removed or became invalid, need to restart capture
                                    device_change_pending_ = true;
                                    if (Listeningway::ConfigurationManager::Snapshot().debug.debugEnabled) {
                                        DebugNotes::Add("SYS.Error: device invalidated");
                                    }
                                    return;
                                case AUDCLNT_E_RESOURCES_INVALIDATED:
                                    LOG_ERROR("[SystemAudioProvider] GetBuffer failed: Resources invalidated - attempting recovery");
                                    // Resources were invalidated, need to restart capture
                                    device_change_pending_ = true;
                                    if (Listeningway::ConfigurationManager::Snapshot().debug.debugEnabled) {
                                        DebugNotes::Add("SYS.Error: resources invalidated");
                                    }
                                    return;
                                case AUDCLNT_E_SERVICE_NOT_RUNNING:
                                    LOG_ERROR("[SystemAudioProvider] GetBuffer failed: Audio service not running");
                                    break;
                                case E_POINTER:
                                    LOG_ERROR("[SystemAudioProvider] GetBuffer failed: Invalid pointer");
                                    break;
                                default:
                                    LOG_ERROR("[SystemAudioProvider] GetBuffer failed with HRESULT: 0x" + 
                                             std::to_string(static_cast<unsigned long>(hr)) + " (decimal: " + 
                                             std::to_string(hr) + ")");
                                    break;
                            }
                            
                            // For certain errors, wait before retrying to avoid tight loop
                            if (hr == AUDCLNT_E_BUFFER_ERROR || hr == AUDCLNT_E_OUT_OF_ORDER) {
                                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                            }
                        }
                    }
                
            }
            
            LOG_DEBUG("[SystemAudioProvider] Exiting capture loop.");
            if (Listeningway::ConfigurationManager::Snapshot().debug.debugEnabled) {
                DebugNotes::Add("SYS.Loop: exit");
            }
            CoUninitialize();
            running = false;
            LOG_DEBUG("[SystemAudioProvider] Audio capture thread stopped.");
            
        } catch (const std::exception& ex) {
            LOG_ERROR(std::string("[SystemAudioProvider] Exception in capture thread: ") + ex.what());
            running = false;
            if (Listeningway::ConfigurationManager::Snapshot().debug.debugEnabled) {
                DebugNotes::Add("SYS.Exception");
            }
        } catch (...) {
            LOG_ERROR("[SystemAudioProvider] Unknown exception in capture thread.");
            running = false;
            if (Listeningway::ConfigurationManager::Snapshot().debug.debugEnabled) {
                DebugNotes::Add("SYS.Exception unknown");
            }
        }
    });
    
    return true;
}

void AudioCaptureProviderSystem::StopCapture(std::atomic_bool& running, std::thread& thread) {
    running = false;
    if (thread.joinable()) {
        thread.join();
    }
}

AudioProviderInfo AudioCaptureProviderSystem::GetProviderInfo() const {
    return AudioProviderInfo{
        "system", // code as string
        "System Audio", // name
        true, // is_default
        2, // order
        true // activates_capture
    };
}
