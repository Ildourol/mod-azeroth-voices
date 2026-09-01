#include "AzerothVoicesNaturalCommands.h"

#include "json.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

namespace AzerothVoices::NaturalCommands
{
    namespace
    {
        using Json = nlohmann::json;

        // This is the checked-out Turtle PlayerBots ChatTriggerContext input
        // registry. Six administrative/debug inputs are intentionally absent:
        // cdebug, cheat, debug, destroy, reset values, and set value.
        constexpr ActionDefinition Actions[] = {
            { "quests", "quests", 1 },
            { "quest reward", "quest reward", 2 },
            { "stats", "stats", 1 },
            { "leave", "leave", 2 },
            { "rep", "reputation", 2 },
            { "reputation", "reputation", 3 },
            { "log", "log", 3 },
            { "los", "los", 3 },
            { "drop", "drop", 3 },
            { "roll", "roll", 2 },
            { "share", "share", 2 },
            { "q", "q", 3 },
            { "ll", "ll", 3 },
            { "ss", "ss", 3 },
            { "loot", "add all loot", 1 },
            { "add all loot", "add all loot", 3 },
            { "release", "release", 1 },
            { "corpse run", "corpse run", 2 },
            { "teleport", "teleport", 2 },
            { "taxi", "taxi", 2 },
            { "repair", "repair", 2 },
            { "u", "use", 3 },
            { "use", "use", 1 },
            { "c", "c", 3 },
            { "items", "c", 3 },
            { "inventory", "c", 2 },
            { "inv", "c", 3 },
            { "e", "e", 3 },
            { "equip", "e", 1 },
            { "keep", "keep", 2 },
            { "ue", "ue", 2 },
            { "s", "s", 2 },
            { "b", "b", 2 },
            { "bb", "bb", 2 },
            { "r", "r", 3 },
            { "t", "t", 3 },
            { "nt", "nt", 3 },
            { "talents", "talents", 2 },
            { "spells", "spells", 2 },
            { "co", "co", 2 },
            { "nc", "nc", 2 },
            { "de", "de", 3 },
            { "react", "react", 3 },
            { "all", "all", 3 },
            { "trainer", "trainer", 2 },
            { "attack", "attack", 1 },
            { "attack rti", "attack rti", 2 },
            { "pull", "pull", 1 },
            { "pull rti", "pull rti", 2 },
            { "chat", "chat", 3 },
            { "accept", "accept", 1 },
            { "home", "home", 2 },
            { "load ai", "load ai", 3 },
            { "list ai", "list ai", 2 },
            { "save ai", "save ai", 3 },
            { "reset ai", "reset ai", 2 },
            { "reset strats", "reset strats", 2 },
            { "emote", "emote", 2 },
            { "buff", "buff", 1 },
            { "help", "help", 2 },
            { "gb", "gb", 3 },
            { "gbank", "gb", 2 },
            { "bank", "bank", 2 },
            { "follow", "follow", 1 },
            { "wander", "wander", 2 },
            { "stay", "stay", 1 },
            { "guard", "guard", 1 },
            { "free", "free", 1 },
            { "wait for attack time", "wait for attack time", 2 },
            { "pet", "pet", 2 },
            { "focus heal", "focus heal", 2 },
            { "follow target", "follow target", 2 },
            { "boost target", "boost target", 2 },
            { "buff target", "buff target", 2 },
            { "revive target", "revive target", 2 },
            { "self res", "self res", 2 },
            { "flee", "flee", 1 },
            { "grind", "grind", 2 },
            { "tank attack", "tank attack", 2 },
            { "talk", "talk", 1 },
            { "cast", "cast", 2 },
            { "castnc", "castnc", 2 },
            { "invite", "invite", 2 },
            { "join", "join", 2 },
            { "lfg", "lfg", 2 },
            { "spell", "spell", 2 },
            { "rti", "rti", 2 },
            { "revive", "revive", 1 },
            { "runaway", "runaway", 2 },
            { "warning", "warning", 3 },
            { "position", "position", 1 },
            { "summon", "summon", 2 },
            { "who", "who", 1 },
            { "where", "where", 2 },
            { "save mana", "save mana", 3 },
            { "max dps", "max dps", 3 },
            { "possible attack targets", "possible attack targets", 3 },
            { "attackers", "attackers", 3 },
            { "formation", "formation", 3 },
            { "stance", "stance", 3 },
            { "sendmail", "sendmail", 3 },
            { "mail", "mail", 3 },
            { "outfit", "outfit", 2 },
            { "go", "go", 3 },
            { "ready", "ready", 1 },
            { "cs", "cs", 3 },
            { "wts", "wts", 3 },
            { "hire", "hire", 3 },
            { "craft", "craft", 3 },
            { "flag", "flag", 3 },
            { "range", "range", 3 },
            { "ra", "ra", 3 },
            { "give leader", "give leader", 3 },
            { "rtsc", "rtsc", 3 },
            { "ah", "ah", 3 },
            { "ah bid", "ah bid", 3 },
            { "guild invite", "guild invite", 3 },
            { "guild join", "guild join", 3 },
            { "guild promote", "guild promote", 3 },
            { "guild demote", "guild demote", 3 },
            { "guild remove", "guild remove", 3 },
            { "guild leave", "guild leave", 3 },
            { "guild leader", "guild leader", 3 },
            { "bg free", "bg free", 3 },
            { "move style", "move style", 3 },
            { "jump", "jump", 3 },
            { "doquest", "doquest", 3 },
            { "skill", "skill", 3 },
            { "faction", "faction", 3 },
            { "glyph", "glyph", 3 },
            { "speak", "speak", 3 },
        };

        static_assert(std::size(Actions) == 131,
            "The Turtle PlayerBots natural-command registry must contain exactly 131 allowed inputs.");

        std::string Trim(std::string value)
        {
            auto notSpace = [](unsigned char c) { return !std::isspace(c); };
            value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
            value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
            return value;
        }

        std::vector<std::string> Split(std::string const& value)
        {
            std::vector<std::string> result;
            std::stringstream stream(value);
            std::string item;
            while (std::getline(stream, item, ','))
            {
                item = Normalize(Trim(item));
                if (!item.empty())
                    result.push_back(item);
            }
            return result;
        }

        bool Contains(std::vector<std::string> const& values, std::string const& value)
        {
            return std::find(values.begin(), values.end(), value) != values.end();
        }

        std::string Meaning(std::string const& action)
        {
            if (action == "rep" || action == "reputation") return "show reputation standing";
            if (action == "log") return "change PlayerBots logging level";
            if (action == "los") return "report line of sight";
            if (action == "drop") return "abandon drop quest";
            if (action == "q") return "query a linked quest or item usage";
            if (action == "ll") return "configure loot strategy list";
            if (action == "ss") return "configure skipped spell list";
            if (action == "follow") return "come with me follow behind";
            if (action == "stay") return "wait here hold position stop moving";
            if (action == "attack" || action == "attack rti") return "attack kill fight target enemy mob";
            if (action == "tank attack") return "tank hold threat attack target";
            if (action == "pull" || action == "pull rti") return "pull lure target enemy";
            if (action == "flee" || action == "runaway") return "flee retreat run away escape";
            if (action == "summon") return "come here teleport to me summon";
            if (action == "release") return "release spirit after death";
            if (action == "revive" || action == "self res") return "revive resurrect spirit healer";
            if (action == "revive target") return "resurrect revive selected target";
            if (action == "buff" || action == "buff target") return "buff apply beneficial spells";
            if (action == "focus heal") return "heal prioritize healing target";
            if (action == "loot" || action == "add all loot") return "loot corpses collect items";
            if (action == "equip" || action == "e") return "equip wear item gear";
            if (action == "ue") return "unequip remove gear item";
            if (action == "keep") return "keep item instead of selling or destroying";
            if (action == "s") return "sell item to vendor";
            if (action == "b") return "buy item from vendor";
            if (action == "bb") return "buy back sold item";
            if (action == "r") return "give trade reward item";
            if (action == "t" || action == "nt") return "trade items or money";
            if (action == "use" || action == "u") return "use item object";
            if (action == "inventory" || action == "items" || action == "inv" || action == "c") return "show inventory items bags";
            if (action == "accept") return "accept quests from selected quest giver";
            if (action == "talk") return "talk interact with selected npc quest giver vendor";
            if (action == "quests" || action == "q") return "show list quests quest log";
            if (action == "quest reward") return "choose quest reward";
            if (action == "doquest") return "complete work on quest";
            if (action == "trainer") return "train learn from selected trainer";
            if (action == "co") return "change combat strategies";
            if (action == "nc") return "change non combat strategies";
            if (action == "de") return "change dead state strategies";
            if (action == "react") return "change reaction strategies";
            if (action == "all") return "change strategies in all states";
            if (action == "chat") return "change PlayerBot chat strategy";
            if (action == "load ai") return "load saved bot ai strategies";
            if (action == "list ai") return "list current bot ai strategies";
            if (action == "save ai") return "save bot ai strategies";
            if (action == "reset ai") return "reset bot ai";
            if (action == "reset strats") return "reset bot strategies";
            if (action == "repair") return "repair equipment at vendor";
            if (action == "bank") return "use personal bank";
            if (action == "gbank" || action == "gb") return "use guild bank";
            if (action == "home") return "set home hearth innkeeper";
            if (action == "taxi") return "take flight path taxi";
            if (action == "teleport") return "teleport travel";
            if (action == "cast" || action == "castnc") return "cast spell";
            if (action == "invite") return "invite player to group";
            if (action == "leave") return "leave party group";
            if (action == "join") return "join group activity";
            if (action == "ready") return "ready check confirm readiness";
            if (action == "guard") return "guard protect this position";
            if (action == "free") return "act independently autonomous";
            if (action == "wander") return "wander roam nearby";
            if (action == "grind") return "grind kill nearby enemies";
            if (action == "position") return "report or change combat position";
            if (action == "formation") return "change group formation";
            if (action == "stance") return "change stance behavior";
            if (action == "range") return "set combat range distance";
            if (action == "wait for attack time") return "set attack wait timing";
            if (action == "save mana") return "save conserve mana";
            if (action == "max dps") return "maximize damage per second";
            if (action == "possible attack targets") return "list possible attack targets";
            if (action == "attackers") return "list current attackers";
            if (action == "pet") return "control pet";
            if (action == "talents") return "show talents build specialization";
            if (action == "spells" || action == "spell") return "show spell abilities";
            if (action == "stats") return "show character stats";
            if (action == "who") return "identify player bot";
            if (action == "where") return "report location";
            if (action == "roll") return "roll dice loot roll";
            if (action == "share") return "share quest";
            if (action == "sendmail") return "send items or money by mail";
            if (action == "mail") return "check or collect mailbox mail";
            if (action == "outfit") return "manage equipment outfit";
            if (action == "go") return "travel go to a location";
            if (action == "cs") return "edit custom strategy";
            if (action == "wts") return "announce want to sell item";
            if (action == "hire") return "hire or recruit bot";
            if (action == "craft") return "configure crafting profession order";
            if (action == "flag") return "set or report pvp flag";
            if (action == "ra") return "remove aura";
            if (action == "rtsc") return "real time strategy control";
            if (action == "ah") return "use auction house";
            if (action == "ah bid") return "bid on auction house item";
            if (action == "guild invite") return "invite player to guild";
            if (action == "guild join") return "join guild";
            if (action == "guild promote") return "promote guild member";
            if (action == "guild demote") return "demote guild member";
            if (action == "guild remove") return "remove guild member";
            if (action == "guild leave") return "leave guild";
            if (action == "guild leader") return "transfer guild leadership";
            if (action == "bg free") return "leave battleground activity freely";
            if (action == "move style") return "change movement style";
            if (action == "skill") return "show or use character skill";
            if (action == "faction") return "show faction information";
            if (action == "glyph") return "manage glyph";
            if (action == "warning") return "announce a warning";
            if (action == "give leader") return "transfer party leadership";
            if (action == "jump") return "jump";
            if (action == "emote") return "perform emote";
            if (action == "speak") return "speak say message";
            return action;
        }

        std::set<std::string> Words(std::string const& value)
        {
            std::set<std::string> result;
            std::stringstream stream(Normalize(value));
            std::string word;
            while (stream >> word)
                if (word.size() >= 2)
                    result.insert(word);
            return result;
        }

        bool IsAllowed(std::vector<std::string> const& allowed, std::string const& action)
        {
            return std::find(allowed.begin(), allowed.end(), action) != allowed.end();
        }
    }

    std::string Normalize(std::string const& value)
    {
        std::string result;
        result.reserve(value.size());
        bool space = true;
        for (unsigned char c : value)
        {
            if (std::isalnum(c))
            {
                result.push_back(static_cast<char>(std::tolower(c)));
                space = false;
            }
            else if (!space)
            {
                result.push_back(' ');
                space = true;
            }
        }
        if (!result.empty() && result.back() == ' ')
            result.pop_back();
        return result;
    }

    ActionDefinition const* FindAction(std::string const& input)
    {
        std::string const normalized = Normalize(input);
        for (ActionDefinition const& action : Actions)
            if (normalized == action.input)
                return &action;
        return nullptr;
    }

    std::vector<std::string> ResolveAllowedActions(std::string const& configured,
                                                    std::vector<std::string>& invalid)
    {
        std::vector<std::string> result;
        invalid.clear();
        auto add = [&](std::string const& input) {
            if (!Contains(result, input))
                result.push_back(input);
        };

        for (std::string const& token : Split(configured))
        {
            uint8_t tier = 0;
            if (token == "light") tier = 1;
            else if (token == "medium") tier = 2;
            else if (token == "heavy") tier = 3;
            if (tier)
            {
                for (ActionDefinition const& action : Actions)
                    if (action.presetTier <= tier)
                        add(action.input);
                continue;
            }

            if (ActionDefinition const* action = FindAction(token))
                add(action->input);
            else if (!Contains(invalid, token))
                invalid.push_back(token);
        }
        return result;
    }

    std::vector<std::string> BuildShortlist(std::vector<std::string> const& allowed,
                                            std::string const& message,
                                            bool dynamic,
                                            size_t maximum)
    {
        if (!dynamic || maximum == 0 || maximum >= allowed.size())
            return allowed;

        std::string const normalizedMessage = Normalize(message);
        std::set<std::string> const messageWords = Words(normalizedMessage);
        struct Scored
        {
            std::string action;
            int score = 0;
            size_t order = 0;
        };
        std::vector<Scored> scored;
        scored.reserve(allowed.size());
        for (size_t i = 0; i < allowed.size(); ++i)
        {
            ActionDefinition const* definition = FindAction(allowed[i]);
            if (!definition)
                continue;
            int score = 4 - definition->presetTier;
            std::string const action = definition->input;
            std::string const dispatch = definition->dispatch;
            std::string const padded = " " + normalizedMessage + " ";
            if (padded.find(" " + action + " ") != std::string::npos)
                score += 200;
            if (dispatch != action && padded.find(" " + dispatch + " ") != std::string::npos)
                score += 160;
            for (std::string const& word : Words(Meaning(action)))
                if (messageWords.count(word))
                    score += 20;
            scored.push_back({ action, score, i });
        }

        std::stable_sort(scored.begin(), scored.end(), [](Scored const& left, Scored const& right) {
            if (left.score != right.score)
                return left.score > right.score;
            return left.order < right.order;
        });
        std::vector<std::string> result;
        for (size_t i = 0; i < scored.size() && i < maximum; ++i)
            result.push_back(scored[i].action);
        return result;
    }

    std::string BuildSystemAddendum(std::vector<std::string> const& shortlist,
                                    uint32_t maximumActions,
                                    std::vector<std::string> const& nearbyEnemies)
    {
        std::ostringstream prompt;
        prompt << "\nIMPORTANT NATURAL-COMMAND OUTPUT FORMAT: The real player speaking is your master. "
               << "Use the same single response for conversation and commands. Return ONLY one single-line "
               << "JSON object, with nothing before or after it: {\"commands\":[{\"action\":\"<allowed "
               << "action>\",\"arguments\":\"<native trailing arguments or empty>\"}],\"say\":\"<short "
               << "in-character reply>\"}. A clear imperative may produce at most " << maximumActions
               << " command(s), in the order requested. Questions, discussion, hypothetical plans, ordinary "
               << "conversation, or unsupported requests must use an empty commands array. Never invent an "
               << "action and never include a command prefix or another command inside arguments. All selected "
               << "PlayerBots receive the same command sequence. Allowed actions for this message are: ";
        for (size_t i = 0; i < shortlist.size(); ++i)
        {
            if (i)
                prompt << ", ";
            prompt << '"' << shortlist[i] << "\" (" << Meaning(shortlist[i]) << ')';
        }
        prompt << ". Use arguments only for the exact text that the native PlayerBots action needs; otherwise "
               << "use an empty string. For attack, tank attack, or pull, copy a named enemy into arguments only "
               << "when it appears in the nearby-enemy list; otherwise use the master's current selection. ";
        if (!nearbyEnemies.empty())
        {
            prompt << "Nearby enemies: ";
            for (size_t i = 0; i < nearbyEnemies.size(); ++i)
            {
                if (i)
                    prompt << ", ";
                prompt << nearbyEnemies[i];
            }
            prompt << ". ";
        }
        else
            prompt << "Nearby enemies: none listed. ";
        prompt << "Keep say concise, truthful about what you understood, in character, and in the same language "
               << "as the player.";
        return prompt.str();
    }

    ParsedReply ParseReply(std::string const& response, uint32_t maximumActions)
    {
        ParsedReply result;
        size_t const first = response.find('{');
        size_t const last = response.rfind('}');
        if (first == std::string::npos || last == std::string::npos || last < first)
        {
            result.say = response;
            return result;
        }

        result.attemptedEnvelope = true;
        try
        {
            Json const root = Json::parse(response.substr(first, last - first + 1));
            if (!root.is_object())
                throw std::runtime_error("natural-command envelope is not an object");
            Json::const_iterator say = root.find("say");
            if (say != root.end() && say->is_string())
                result.say = Trim(say->get<std::string>());
            Json::const_iterator commands = root.find("commands");
            if (commands != root.end() && commands->is_array())
            {
                for (Json const& item : *commands)
                {
                    if (result.actions.size() >= maximumActions || !item.is_object())
                        continue;
                    Json::const_iterator actionValue = item.find("action");
                    if (actionValue == item.end() || !actionValue->is_string())
                        continue;
                    ParsedAction action;
                    action.action = Normalize(actionValue->get<std::string>());
                    Json::const_iterator arguments = item.find("arguments");
                    if (arguments != item.end() && arguments->is_string())
                        action.arguments = Trim(arguments->get<std::string>());
                    if (!action.action.empty())
                        result.actions.push_back(std::move(action));
                }
            }
        }
        catch (...)
        {
            result.parseMiss = true;
            result.actions.clear();
            result.say = "Hm? Say that again.";
        }
        if (result.say.empty())
            result.say = result.actions.empty() ? "Hm? Say that again." : "On it.";
        return result;
    }

    bool MatchFastPath(std::string const& commandText,
                       std::vector<std::string> const& allowed,
                       FastPathAction& result)
    {
        result = FastPathAction{};
        std::string const message = Normalize(commandText);
        auto match = [&](std::string const& action, std::string const& acknowledgement) {
            if (!IsAllowed(allowed, action))
                return false;
            result.action = action;
            result.acknowledgement = acknowledgement;
            return true;
        };

        if (message == "follow" || message == "follow me" || message == "come with me")
            return match("follow", "On it - right behind you.");
        if (message == "stay" || message == "stay here" || message == "stay put" ||
            message == "wait here" || message == "hold here" || message == "hold position")
            return match("stay", "Holding here.");
        if (message == "attack" || message == "attack my target" || message == "attack it" ||
            message == "kill it")
            return match("attack", "On it - attacking your mark.");
        if (message == "flee" || message == "fall back" || message == "retreat")
            return match("flee", "Falling back to you!");
        if (message == "come here" || message == "come to me")
            return match("summon", "On my way to you.");
        if (message == "release" || message == "release spirit")
            return match("release", "Releasing.");
        if (message == "revive" || message == "revive yourself")
            return match("revive", "Heading for a spirit healer.");
        return false;
    }

    std::vector<std::string> AllActionInputs()
    {
        std::vector<std::string> result;
        result.reserve(std::size(Actions));
        for (ActionDefinition const& action : Actions)
            result.push_back(action.input);
        return result;
    }
}
