// ---------------------------------------------
// Thread Safety Manager Implementation
// Centralizes all thread synchronization for the Listeningway system
// ---------------------------------------------
#include "thread_safety_manager.h"

namespace Listeningway {

ThreadSafetyManager& ThreadSafetyManager::Instance() {
    static ThreadSafetyManager instance;
    return instance;
}

std::mutex& ThreadSafetyManager::GetMutex(LockType type) {
    switch (type) {
        case LockType::LOGGING:
            return logging_mutex_;
        case LockType::AUDIO_DATA:
            return audio_data_mutex_;
        case LockType::PROVIDER_SWITCH:
            return provider_switch_mutex_;
        default:
            return audio_data_mutex_; // Safe fallback
    }
}

ThreadSafetyManager::SingleLock::SingleLock(LockType type)
    : lock_(ThreadSafetyManager::Instance().GetMutex(type)), type_(type) {
}

ThreadSafetyManager::SingleLock::~SingleLock() = default;

} // namespace Listeningway
