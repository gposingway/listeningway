#pragma once

#include "constants.h"
#include <string>

/**
 * @brief Utility functions for audio format operations
 * Eliminates repetitive switch statements and provides type-safe format handling
 */
namespace AudioFormatUtils {

    /**
     * @brief Convert AudioFormat enum to human-readable string
     * @param format The audio format enum value
     * @return String representation of the audio format
     */
    constexpr const char* FormatToString(AudioFormat format) {
        switch (format) {
            case AudioFormat::None: return "None";
            case AudioFormat::Mono: return "Mono";
            case AudioFormat::Stereo: return "Stereo";
            case AudioFormat::Surround51: return "5.1";
            case AudioFormat::Surround71: return "7.1";
            default: return "Unknown";
        }
    }

    /**
     * @brief Convert integer to AudioFormat enum safely
     * @param format_int The integer representation
     * @return AudioFormat enum value
     */
    constexpr AudioFormat IntToFormat(int format_int) {
        switch (format_int) {
            case 0: return AudioFormat::None;
            case 1: return AudioFormat::Mono;
            case 2: return AudioFormat::Stereo;
            case 6: return AudioFormat::Surround51;
            case 8: return AudioFormat::Surround71;
            default: return AudioFormat::None;
        }
    }

    /**
     * @brief Get the number of channels for an audio format
     * @param format The audio format
     * @return Number of channels
     */
    constexpr int GetChannelCount(AudioFormat format) {
        switch (format) {
            case AudioFormat::None: return 0;
            case AudioFormat::Mono: return 1;
            case AudioFormat::Stereo: return 2;
            case AudioFormat::Surround51: return 6;
            case AudioFormat::Surround71: return 8;
            default: return 0;
        }
    }

    /**
     * @brief Check if a channel index is a left channel for the given format
     * @param format The audio format
     * @param channel_index The channel index
     * @return true if it's a left channel
     */
    constexpr bool IsLeftChannel(AudioFormat format, int channel_index) {
        switch (format) {
            case AudioFormat::Mono:
            case AudioFormat::Stereo:
                return channel_index == 0;
            case AudioFormat::Surround51:
            case AudioFormat::Surround71:
                return channel_index == 0 || channel_index == 4; // FL, SL
            default:
                return channel_index == 0;
        }
    }

    /**
     * @brief Check if a channel index is a right channel for the given format
     * @param format The audio format
     * @param channel_index The channel index
     * @return true if it's a right channel
     */    constexpr bool IsRightChannel(AudioFormat format, int channel_index) {
        switch (format) {
            case AudioFormat::Mono:
                return channel_index == 0; // Mono channel counts as right too for balance calculation
            case AudioFormat::Stereo:
                return channel_index == 1;
            case AudioFormat::Surround51:
                return channel_index == 1 || channel_index == 5; // FR, SR
            case AudioFormat::Surround71:
                return channel_index == 1 || channel_index == 5 || channel_index == 7; // FR, RR, SR
            default:
                return channel_index == 1;
        }
    }

    /**
     * @brief Check if a channel index is a center channel for the given format
     * @param format The audio format
     * @param channel_index The channel index
     * @return true if it's a center channel
     */
    constexpr bool IsCenterChannel(AudioFormat format, int channel_index) {
        switch (format) {
            case AudioFormat::Surround51:
            case AudioFormat::Surround71:
                return channel_index == 2; // C
            default:
                return false;
        }
    }

    /**
     * @brief Check if a channel index is a rear channel for the given format
     * @param format The audio format
     * @param channel_index The channel index
     * @return true if it's a rear channel
     */
    constexpr bool IsRearChannel(AudioFormat format, int channel_index) {
        switch (format) {
            case AudioFormat::Surround71:
                return channel_index == 4 || channel_index == 5; // RL, RR
            default:
                return false;
        }
    }

    /**
     * @brief Check if a channel index is a side channel for the given format
     * @param format The audio format
     * @param channel_index The channel index
     * @return true if it's a side channel
     */
    constexpr bool IsSideChannel(AudioFormat format, int channel_index) {
        switch (format) {
            case AudioFormat::Surround51:
                return channel_index == 4 || channel_index == 5; // SL, SR
            case AudioFormat::Surround71:
                return channel_index == 6 || channel_index == 7; // SL, SR
            default:
                return false;
        }
    }

} // namespace AudioFormatUtils
