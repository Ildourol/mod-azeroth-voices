#include "AzerothVoicesInstanceLore.h"

#include "json.hpp"

#include <algorithm>
#include <cctype>
#include <exception>
#include <sstream>

namespace AzerothVoices
{
    namespace
    {
        using Json = nlohmann::json;

        std::string Trim(std::string value)
        {
            auto notSpace = [](unsigned char c) { return !std::isspace(c); };
            value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
            value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
            return value;
        }

        std::string Clean(std::string const& value)
        {
            std::string result;
            result.reserve(value.size());
            bool previousSpace = false;
            for (unsigned char input : value)
            {
                char output = static_cast<char>(input);
                if (std::iscntrl(input))
                    output = ' ';
                bool const space = std::isspace(static_cast<unsigned char>(output)) != 0;
                if (space)
                {
                    if (previousSpace)
                        continue;
                    output = ' ';
                }
                result.push_back(output);
                previousSpace = space;
            }
            return Trim(result);
        }

        std::string HeadBounded(std::string value, size_t maximum)
        {
            if (value.size() <= maximum)
                return value;
            if (maximum <= 3)
                return value.substr(0, maximum);
            value.resize(maximum - 3);
            value += "...";
            return value;
        }
    }

    bool ParseInstanceLore(std::string const& json, InstanceLoreRegistry& registry,
                           std::string& error)
    {
        registry.clear();
        error.clear();

        Json root;
        try
        {
            root = Json::parse(json);
        }
        catch (std::exception const& exception)
        {
            error = std::string("instance lore JSON error: ") + exception.what();
            return false;
        }
        if (!root.is_object())
        {
            error = "instance lore must be a JSON object keyed by map ID";
            return false;
        }

        for (auto it = root.begin(); it != root.end(); ++it)
        {
            uint32_t mapId = 0;
            try
            {
                size_t consumed = 0;
                unsigned long const parsed = std::stoul(it.key(), &consumed, 10);
                if (consumed != it.key().size() || parsed == 0 ||
                    parsed > 0xFFFFFFFFul)
                    throw std::exception();
                mapId = static_cast<uint32_t>(parsed);
            }
            catch (std::exception const&)
            {
                error += "instance lore key '" + it.key() + "' is not a map ID; ";
                continue;
            }

            InstanceLoreEntry entry;
            entry.mapId = mapId;
            if (it->is_string())
            {
                entry.lore = Clean(it->get<std::string>());
            }
            else if (it->is_object())
            {
                if (it->count("name") && (*it)["name"].is_string())
                    entry.name = Clean((*it)["name"].get<std::string>());
                if (it->count("lore") && (*it)["lore"].is_string())
                    entry.lore = Clean((*it)["lore"].get<std::string>());
            }
            else
            {
                error += "instance lore map " + std::to_string(mapId) + " is not an object or string; ";
                continue;
            }

            if (entry.lore.empty())
            {
                error += "instance lore map " + std::to_string(mapId) + " has no lore text; ";
                continue;
            }
            registry[mapId] = std::move(entry);
        }
        return true;
    }

    std::string BuildInstanceLoreContext(uint32_t mapId, std::string const& mapName,
                                         std::string const& area,
                                         InstanceLoreRegistry const& registry,
                                         size_t maximumCharacters)
    {
        auto found = registry.find(mapId);
        if (found != registry.end() && !found->second.lore.empty())
        {
            std::ostringstream text;
            text << "Instance: " << (found->second.name.empty() ? mapName : found->second.name)
                 << ". Curated lore: " << found->second.lore;
            if (!area.empty())
                text << " Current area: " << area << '.';
            return HeadBounded(text.str(), maximumCharacters);
        }

        std::ostringstream text;
        text << "Instance: " << (mapName.empty() ? "an unnamed place" : mapName);
        if (!area.empty())
            text << " (current area: " << area << ')';
        text << ". No curated lore is available for this instance; use only what is directly "
                "observable here and do not invent its history, bosses, or layout.";
        return HeadBounded(text.str(), maximumCharacters);
    }
}
