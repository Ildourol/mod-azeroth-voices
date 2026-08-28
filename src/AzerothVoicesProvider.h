#pragma once

#include "AzerothVoicesConfig.h"
#include "AzerothVoicesTypes.h"

#include <string>
#include <vector>

namespace AzerothVoices
{
    class Provider
    {
    public:
        static bool InitializeTls(std::string& error);
        static ChatCompletion Execute(Config const& config, ChatRequest const& request);
        static std::vector<std::string> SplitReply(Config const& config, std::string text);

    private:
        static std::string BuildBody(Config const& config, ChatRequest const& request, std::string& error);
        static std::string ParseResponse(Config const& config, ChatRequest const& request,
                                         std::string const& raw, std::string& error);
    };
}
