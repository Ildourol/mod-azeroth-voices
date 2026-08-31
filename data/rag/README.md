# Azeroth Voices local RAG data

This directory is the structured, local knowledge corpus used by `mod-azeroth-voices`. It is designed for the Turtle WoW/TortoiseWoW Vanilla-plus level-60 ruleset. Retrieval is performed in C++ before an API request; it needs no Python process, embeddings service, vector database, or additional network call.

The module discovers every `.json` file in the configured directory once at startup. A malformed file is reported and skipped without disabling the remaining corpus. With `AzerothVoices.Debug = 1`, startup logs show the number of entries loaded from each file and each request logs the selected entry titles and similarity scores.

## Included corpus

- `vanilla_world.json`: compact Turtle WoW rules, custom races, custom locations, and server-specific corrections.
- `wow_classes_factions.json`: all nine Vanilla classes, Alliance/Horde, and Turtle WoW High Elf and Goblin races.
- `wow_dungeons_raids.json`: Vanilla dungeons and raids plus Turtle WoW level-60 content such as Emerald Sanctum.
- `wow_general_tips.json`: leveling, economy, groups, addons, preparation, and level-60 activities.
- `wow_items_equipment.json`: class sets, legendaries, tools, consumables, keys, relics, PvP gear, and cosmetics.
- `wow_mechanics.json`: talents, reputation, weapon skill, rested experience and tents, guilds, events, instances, and currencies.
- `wow_npcs_creatures.json`: trainers, vendors, transport, leaders, rare creatures, and world bosses.
- `wow_professions.json`: Vanilla primary and secondary professions, Turtle WoW Survival, skill ranks, and class pairing guidance.
- `wow_pvp.json`: Vanilla honor ranks, battlegrounds, world PvP, duels, objectives, gear, and rewards.
- `wow_quests_storylines.json`: class, zone, dungeon, profession, event, group, repeatable, and hidden quests.
- `wow_zones.json`: core starting zones and Turtle WoW locations including Blackstone Island, Thalassian Highlands, Gilneas, and Mount Hyjal.

The broader reference corpus was retained, but later-expansion assumptions were rewritten for Turtle WoW. Some entries explicitly state that a later system does not exist; those negative facts are intentional safeguards against provider hallucination.

## Supported JSON shapes

The loader accepts a top-level array, an object containing an `entries` array, an object containing an `items` array, or one top-level entry object. Each entry may use:

```json
{
  "id": "unique_identifier",
  "title": "Readable title",
  "category": "topic category",
  "content": "Fact text supplied to the model when selected.",
  "keywords": ["search phrase", "another phrase"],
  "tags": ["optional", "extra terms"],
  "source": "optional source label"
}
```

`text` may be used instead of `content`. `keywords` and `tags` may be arrays; `keywords` may also be a comma-separated string. Entries without usable text are ignored.

## Retrieval behavior

The current incoming message, useful trigger name, map, zone, and area are tokenized locally. Matches in keywords receive the greatest weight, title/category matches receive medium weight, and body matches receive lower weight. Entries below `AzerothVoices.RAG.SimilarityThreshold` are rejected; the best remaining entries are sorted by score and bounded by `MaximumItems` and `MaximumCharacters`.

No RAG block is inserted when nothing passes the threshold. Lower thresholds increase recall and prompt size; higher thresholds increase precision. `0.3` is the default balance for this curated corpus.

## Adding data safely

1. Add a new `.json` file rather than modifying C++.
2. Give each entry a unique, stable `id` and a narrow, descriptive title.
3. Use Turtle WoW/Vanilla facts and name custom content explicitly.
4. Put likely player terms, abbreviations, zones, NPCs, and item names in `keywords`.
5. Keep each content block self-contained and concise enough to be useful in one prompt.
6. Validate the JSON and restart `mangosd`; the corpus is intentionally immutable while workers are running.

The distributed corpus is module data, not generated chat history. It is read-only at runtime and is never written to the character database.
