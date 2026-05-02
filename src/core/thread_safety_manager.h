// ---------------------------------------------
// Thread Safety Manager
// Centralizes all thread synchronization for the Listeningway system
// Eliminates anti-pattern of multiple scattered mutex instances
// ---------------------------------------------
#pragma once
#include <mutex>
#include <memory>

namespace Listeningway {

/**
 * @brief Centralized thread safety management for Listeningway
 * 
 * This class consolidates all mutex usage to eliminate the anti-pattern of 
 * multiple scattered mutexes throughout the codebase. It provides:
 * - Unified audio data protection
 * - Provider switching synchronization
 * - Logging coordination
 * - Deadlock prevention through consistent lock ordering
 */
class ThreadSafetyManager {
public:
    /**
     * @brief Lock types for establishing consistent lock ordering
     */
    enum class LockType {
        LOGGING = 0,        // Lowest priority - can be held with other locks
        AUDIO_DATA = 1,     // Medium priority - protects audio analysis data
        PROVIDER_SWITCH = 2 // Highest priority - protects provider operations
    };

    /**
     * @brief RAII lock guard for single mutex
     */
    class SingleLock {
    public:
        explicit SingleLock(LockType type);
        ~SingleLock();
        SingleLock(const SingleLock&) = delete;
        SingleLock& operator=(const SingleLock&) = delete;

    private:
        std::unique_lock<std::mutex> lock_;
        LockType type_;
    };

    /**
     * @brief Get the singleton instance
     */
    static ThreadSafetyManager& Instance();

    /**
     * @brief Get mutex by type (for advanced use cases)
     */
    std::mutex& GetMutex(LockType type);

    // Convenience methods for common locking patterns
    SingleLock LockAudioData() { return SingleLock(LockType::AUDIO_DATA); }
    SingleLock LockLogging() { return SingleLock(LockType::LOGGING); }
    SingleLock LockProviderSwitch() { return SingleLock(LockType::PROVIDER_SWITCH); }

private:
    ThreadSafetyManager() = default;
    
    mutable std::mutex logging_mutex_;
    mutable std::mutex audio_data_mutex_;
    mutable std::mutex provider_switch_mutex_;
};

// Convenience macros for common locking patterns
#define LOCK_AUDIO_DATA() auto _audio_lock = Listeningway::ThreadSafetyManager::Instance().LockAudioData()
#define LOCK_LOGGING() auto _log_lock = Listeningway::ThreadSafetyManager::Instance().LockLogging()
#define LOCK_PROVIDER_SWITCH() auto _provider_lock = Listeningway::ThreadSafetyManager::Instance().LockProviderSwitch()

} // namespace Listeningway
