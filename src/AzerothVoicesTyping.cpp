#include "AzerothVoicesConfig.h"

#include "Config/Config.h"
#include "Log.h"

#undef sConfig
#define sConfig (MaNGOS::Singleton<::Config, ::Config::Lock>::Instance())

#include <algorithm>
#include <cstdint>
#include <limits>
#include <random>

namespace AzerothVoices
{
    namespace
    {
        constexpr uint32_t DefaultMinimumTypingDelayMilliseconds = 100;
        constexpr uint32_t DefaultMaximumTypingDelayMilliseconds = 200;
        constexpr int32_t MissingTypingDelay = std::numeric_limits<int32_t>::min();

        uint32_t NormalizeTypingDelay(char const* key, int32_t value, uint32_t fallback)
        {
            if (value == MissingTypingDelay)
                return fallback;
            if (value < 0)
            {
                sLog.outError("[AzerothVoices][CONFIG] %s=%d is below 0; using 0.", key, value);
                return 0;
            }
            return static_cast<uint32_t>(value);
        }
    }

    TypingDelayRange& TypingDelayRange::operator=(uint32_t legacyDelayMilliseconds)
    {
        int32_t const configuredMinimum = sConfig.GetIntDefault(
            "AzerothVoices.Typing.MinimumDelayPerCharacterMilliseconds", MissingTypingDelay);
        int32_t const configuredMaximum = sConfig.GetIntDefault(
            "AzerothVoices.Typing.MaximumDelayPerCharacterMilliseconds", MissingTypingDelay);
        int32_t const configuredLegacy = sConfig.GetIntDefault(
            "AzerothVoices.Typing.DelayPerCharacterMilliseconds", MissingTypingDelay);

        bool const hasMinimum = configuredMinimum != MissingTypingDelay;
        bool const hasMaximum = configuredMaximum != MissingTypingDelay;
        bool const hasLegacy = configuredLegacy != MissingTypingDelay;

        // Preserve an existing fixed-delay installation when neither new range
        // key is present. New installations default to a 100-200 ms range.
        if (!hasMinimum && !hasMaximum && hasLegacy)
        {
            uint32_t const fixedDelay = NormalizeTypingDelay(
                "AzerothVoices.Typing.DelayPerCharacterMilliseconds",
                configuredLegacy, legacyDelayMilliseconds);
            minimum = fixedDelay;
            maximum = fixedDelay;
            return *this;
        }

        minimum = NormalizeTypingDelay(
            "AzerothVoices.Typing.MinimumDelayPerCharacterMilliseconds",
            configuredMinimum, DefaultMinimumTypingDelayMilliseconds);
        maximum = NormalizeTypingDelay(
            "AzerothVoices.Typing.MaximumDelayPerCharacterMilliseconds",
            configuredMaximum, DefaultMaximumTypingDelayMilliseconds);

        if (maximum < minimum)
        {
            sLog.outError(
                "[AzerothVoices][CONFIG] Typing maximum delay %u is below minimum %u; swapping the values.",
                maximum, minimum);
            std::swap(minimum, maximum);
        }
        return *this;
    }

    TypingDelayRange::operator uint32_t() const
    {
        if (maximum <= minimum)
            return minimum;

        // The existing delivery path evaluates this conversion once per reply
        // line, giving each line a stable but slightly different typing speed.
        static thread_local std::mt19937 engine(std::random_device{}());
        return std::uniform_int_distribution<uint32_t>(minimum, maximum)(engine);
    }
}
