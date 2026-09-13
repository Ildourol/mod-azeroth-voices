#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>

namespace AzerothVoices
{
    struct InstanceLoreEntry
    {
        uint32_t mapId = 0;
        std::string name;
        std::string lore;
    };

    using InstanceLoreRegistry = std::map<uint32_t, InstanceLoreEntry>;

    // data/instance_lore.json is keyed by map ID. A value is either an object
    // with "name" and "lore" fields or a bare lore string. Malformed rows are
    // reported in `error` and skipped so one bad entry cannot disable the rest.
    bool ParseInstanceLore(std::string const& json, InstanceLoreRegistry& registry,
                           std::string& error);

    // Curated lore when the map has an entry; otherwise the engine map name and
    // the current area with an explicit instruction not to invent what is
    // missing.
    std::string BuildInstanceLoreContext(uint32_t mapId, std::string const& mapName,
                                         std::string const& area,
                                         InstanceLoreRegistry const& registry,
                                         size_t maximumCharacters = 700);
}
