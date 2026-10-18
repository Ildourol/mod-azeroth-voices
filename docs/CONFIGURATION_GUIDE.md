# Azeroth Voices — Comprehensive Configuration & Tuning Guide

> **Module**: `mod-azeroth-voices` (Autonomous Native C++ LLM Engine for Turtle WoW / vMaNGOS)

> **Reference Configuration**: [`conf/mod-azeroth-voices.conf.dist`](file:///C:/Users/Admin/AntigravityProfiles/Projects%20Twow/Turtle%20wow%20modules/mod-azeroth-voices/conf/mod-azeroth-voices.conf.dist)

> **Runtime Module Path**: `Server/etc/modules/mod-azeroth-voices.conf`


---

## Overview & Architecture

`mod-azeroth-voices` provides zero-bridge, native in-process C++ LLM chatter for Turtle WoW / vMaNGOS. All settings are loaded on worldserver boot from `mod-azeroth-voices.conf` (or fallback to compiled defaults). Configuration can be safely reloaded in-game by Game Masters using `.av reload` or `.av restart`.


### Key Conventions

- **Booleans**: `1` (Enabled / True), `0` (Disabled / False).

- **Percentages**: `0` (never triggers) through `100` (guaranteed / deterministic trigger).

- **Time**: Explicitly indicated in milliseconds (`ms`) or seconds (`s`).

- **Distance**: Spatial distance in 3D yards inside the game world.

- **Context Budget**: Measured in character length or LLM tokens.

- **Secrets & Environment Variables**: API keys can be supplied as raw strings or securely via `env:VARIABLE_NAME`.


---

## Table of Contents

1. [Module ownership and diagnostics](#module-ownership-and-diagnostics) (5 settings)
2. [Provider and HTTPS](#provider-and-https) (19 settings)
3. [Worker pool, queue, limits, and cooldowns](#worker-pool-queue-limits-and-cooldowns) (13 settings)
4. [Shared prompts and PlayerBots-compatible prompt names](#shared-prompts-and-playerbots-compatible-prompt-names) (15 settings)
5. [Persistent PlayerBot personalities](#persistent-playerbot-personalities) (10 settings)
6. [PlayerBot-to-real-player sentiment](#playerbot-to-real-player-sentiment) (11 settings)
7. [PlayerBot delivery scopes, response selection, and distances](#playerbot-delivery-scopes-response-selection-and-distances) (36 settings)
8. [NPC dialogue: reactions, combat-start, and the ordered /say reply chain](#npc-dialogue-reactions-combat-start-and-the-ordered-say-reply-chain) (18 settings)
9. [Dedicated proximity chatter (ordinary Say)](#dedicated-proximity-chatter-ordinary-say) (19 settings)
10. [Boss and miniboss pre-aggro dialogue (dungeons and raids only)](#boss-and-miniboss-pre-aggro-dialogue-dungeons-and-raids-only) (18 settings)
11. [Party and raid chatter](#party-and-raid-chatter) (63 settings)
12. [Guild chatter](#guild-chatter) (38 settings)
13. [Deterministic bot memory ledger](#deterministic-bot-memory-ledger) (17 settings)
14. [Random nearby, guild, and world chatter](#random-nearby-guild-and-world-chatter) (11 settings)
15. [Automated World/custom-channel pacing](#automated-worldcustom-channel-pacing) (11 settings)
16. [PartyGate per-party delivery pacing](#partygate-per-party-delivery-pacing) (8 settings)
17. [Lightweight live environment context](#lightweight-live-environment-context) (6 settings)
18. [Event chatter](#event-chatter) (27 settings)
19. [Reply length and PlayerBots-compatible typing delay](#reply-length-and-playerbots-compatible-typing-delay) (7 settings)
20. [Bounded conversation and surrounding-chat history](#bounded-conversation-and-surrounding-chat-history) (17 settings)
21. [SNAPSHOT SYSTEM](#snapshot-system) (23 settings)
22. [Structured local RAG (optional, no embeddings or extra provider call)](#structured-local-rag-optional-no-embeddings-or-extra-provider-call) (7 settings)
23. [Azeroth Voices Companion addon and Chatter compatibility](#azeroth-voices-companion-addon-and-chatter-compatibility) (2 settings)
24. [Recommended Deployment Presets](#recommended-deployment-presets)
25. [Maintenance & Update Protocol](#maintenance--update-protocol)


---

## 1. Module ownership and diagnostics

| Setting Name | Type / Range | Default | Summary |
| :--- | :--- | :--- | :--- |
| [`AzerothVoices.Enable`](#azerothvoicesenable) | Boolean (`0` or `1`) | `1` | Master switch for every module feature and HTTP request. (Default 1.) |
| [`AzerothVoices.Debug`](#azerothvoicesdebug) | Boolean (`0` or `1`) | `0` | Enables lightweight Azeroth Voices activity messages for administrators; 0 ... |
| [`AzerothVoices.Console.GeneratedMessages`](#azerothvoicesconsolegeneratedmessages) | Boolean (`0` or `1`) | `0` | Logs each cleaned reply with request ID, actor, actor kind, scope, trigger,... |
| [`AzerothVoices.Console.ApiCallStats`](#azerothvoicesconsoleapicallstats) | Boolean (`0` or `1`) | `0` | Logs HTTP-attempt, success, failure, generated-message, and preflight-rejec... |
| [`AzerothVoices.Console.ApiCallStatsIntervalSeconds`](#azerothvoicesconsoleapicallstatsintervalseconds) | Integer (Seconds, minimum 5; default 60.) | `60` | Length of each terminal reporting window. (Seconds, minimum 5; default 60.) |


### <a id="azerothvoicesenable"></a>`AzerothVoices.Enable`

- **Data Type**: Boolean (`0` or `1`)
- **Default Value**: `1`
- **Description**: Master switch for every module feature and HTTP request. (Default 1.)
- **Operator Guidance & Impact**: Keep enabled (`1`) for standard functionality. Set to `0` to completely bypass this feature and eliminate its processing overhead.

### <a id="azerothvoicesdebug"></a>`AzerothVoices.Debug`

- **Data Type**: Boolean (`0` or `1`)
- **Default Value**: `0`
- **Description**: Enables lightweight Azeroth Voices activity messages for administrators; 0 = disabled, 1 = enabled. (Default 0.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicesconsolegeneratedmessages"></a>`AzerothVoices.Console.GeneratedMessages`

- **Data Type**: Boolean (`0` or `1`)
- **Default Value**: `0`
- **Description**: Logs each cleaned reply with request ID, actor, actor kind, scope, trigger, speaker, HTTP status, generation time, and text; this may expose private chat in server logs. (Default 0.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicesconsoleapicallstats"></a>`AzerothVoices.Console.ApiCallStats`

- **Data Type**: Boolean (`0` or `1`)
- **Default Value**: `0`
- **Description**: Logs HTTP-attempt, success, failure, generated-message, and preflight-rejection totals every configured interval; each retry counts as another API call. It never repeats generated text. (Default 0.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicesconsoleapicallstatsintervalseconds"></a>`AzerothVoices.Console.ApiCallStatsIntervalSeconds`

- **Data Type**: Integer (Seconds, minimum 5; default 60.)
- **Default Value**: `60`
- **Description**: Length of each terminal reporting window. (Seconds, minimum 5; default 60.)
- **Operator Guidance & Impact**: Controls background evaluation frequency. Shorter intervals increase responsiveness at the cost of more frequent world-thread scans; longer intervals conserve server CPU.

---

## 2. Provider and HTTPS

| Setting Name | Type / Range | Default | Summary |
| :--- | :--- | :--- | :--- |
| [`AzerothVoices.ProviderMode`](#azerothvoicesprovidermode) | Enum (`ChatCompletions/Responses`) | `ChatCompletions` | ChatCompletions supports OpenAI, Gemini's OpenAI-compatible endpoint, OpenR... |
| [`AiPlayerbot.LLMApiEndpoint`](#aiplayerbotllmapiendpoint) | String (HTTP/HTTPS URL) | `https://api.openai.com/v1/chat/completions` | Full provider endpoint. Remote plain HTTP is rejected. (URL; default OpenAI... |
| [`AiPlayerbot.LLMApiKey`](#aiplayerbotllmapikey) | String (API Key or `env:VARIABLE`) | `env:OPENAI_API_KEY` | Supplies the Bearer API token. (Secret or env:VARIABLE; default env:OPENAI_... |
| [`AzerothVoices.Model`](#azerothvoicesmodel) | String | `gpt-4o-mini` | Exact model identifier accepted by the selected endpoint. (String; default ... |
| [`AiPlayerbot.LLMApiJson`](#aiplayerbotllmapijson) | String (Template with placeholders) | *(Empty)* | Empty lets the module generate provider-safe JSON; a custom legacy template... |
| [`AzerothVoices.ParserMode`](#azerothvoicesparsermode) | Enum (`ProviderJson/LegacyRegex`) | `ProviderJson` | ProviderJson parses OpenAI chat/Responses, Gemini candidates, and Ollama JS... |
| [`AzerothVoices.CACertFile`](#azerothvoicescacertfile) | String | *(Empty)* | Empty uses system CAs; otherwise uses this custom CA bundle. (File path; de... |
| [`AzerothVoices.AllowLocalHttp`](#azerothvoicesallowlocalhttp) | Boolean (`0` or `1`) | `1` | Allows plain HTTP only for local/loopback providers, never remote HTTP. (De... |
| [`AzerothVoices.ConnectTimeoutSeconds`](#azerothvoicesconnecttimeoutseconds) | Integer (Seconds, minimum 1; default 10.) | `10` | TCP/TLS connection timeout. (Seconds, minimum 1; default 10.) |
| [`AiPlayerbot.LLMGenerationTimeout`](#aiplayerbotllmgenerationtimeout) | Integer (Seconds, minimum 1; default 60.) | `60` | Provider read/write generation timeout. (Seconds, minimum 1; default 60.) |
| [`AzerothVoices.MaxResponseBytes`](#azerothvoicesmaxresponsebytes) | Integer | `65536` | Rejects oversized HTTP responses. (Bytes, minimum 1024; default 65536.) |
| [`AzerothVoices.MaxTokens`](#azerothvoicesmaxtokens) | Integer (Tokens, minimum 1; default 512.) | `512` | Requested output limit in generated provider JSON. (Tokens, minimum 1; defa... |
| [`AzerothVoices.Thinking.Mode`](#azerothvoicesthinkingmode) | Enum (`Auto/On/Off`) | `Auto` | Official-endpoint Thinking policy. Only api.openai.com and generativelangua... |
| [`AzerothVoices.Thinking.AutoDetect`](#azerothvoicesthinkingautodetect) | Boolean (`0` or `1`) | `1` | Recognises the provider and model capability from the endpoint and model na... |
| [`AzerothVoices.Thinking.Effort`](#azerothvoicesthinkingeffort) | Enum (`Minimal/Low/Medium/High`) | `Low` | Requested reasoning level. Minimal collapses to Low on the Responses API an... |
| [`AzerothVoices.Thinking.TokenReserve`](#azerothvoicesthinkingtokenreserve) | Integer (Tokens, 0-100000; default 1024.) | `1024` | Added to the active output-token field only when a Thinking control is actu... |
| [`AzerothVoices.Thinking.AutoKinds`](#azerothvoicesthinkingautokinds) | String (Comma-separated list) | `PersonalityGeneration,EventChat` | Request purposes that Auto mode may control. PersonalityGeneration covers i... |
| [`AzerothVoices.Temperature`](#azerothvoicestemperature) | Decimal (Decimal 0.0-2.0; default 0.8.) | `0.8` | Sampling randomness; invalid values are clamped. (Decimal 0.0-2.0; default ... |
| [`AzerothVoices.TopP`](#azerothvoicestopp) | Decimal (Decimal 0.0-1.0; default 0.95.) | `0.95` | Nucleus sampling; invalid values are clamped. (Decimal 0.0-1.0; default 0.9... |


### <a id="azerothvoicesprovidermode"></a>`AzerothVoices.ProviderMode`

- **Data Type**: Enum (`ChatCompletions/Responses`)
- **Default Value**: `ChatCompletions`
- **Description**: ChatCompletions supports OpenAI, Gemini's OpenAI-compatible endpoint, OpenRouter, and compatible gateways; Responses uses OpenAI's /v1/responses request shape. (Enum ChatCompletions/Responses; default ChatCompletions.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="aiplayerbotllmapiendpoint"></a>`AiPlayerbot.LLMApiEndpoint`

- **Data Type**: String (HTTP/HTTPS URL)
- **Default Value**: `https://api.openai.com/v1/chat/completions`
- **Description**: Full provider endpoint. Remote plain HTTP is rejected. (URL; default OpenAI chat/completions.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="aiplayerbotllmapikey"></a>`AiPlayerbot.LLMApiKey`

- **Data Type**: String (API Key or `env:VARIABLE`)
- **Default Value**: `env:OPENAI_API_KEY`
- **Description**: Supplies the Bearer API token. (Secret or env:VARIABLE; default env:OPENAI_API_KEY.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicesmodel"></a>`AzerothVoices.Model`

- **Data Type**: String
- **Default Value**: `gpt-4o-mini`
- **Description**: Exact model identifier accepted by the selected endpoint. (String; default gpt-4o-mini.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="aiplayerbotllmapijson"></a>`AiPlayerbot.LLMApiJson`

- **Data Type**: String (Template with placeholders)
- **Default Value**: *(Empty)*
- **Description**: Empty lets the module generate provider-safe JSON; a custom legacy template may use <pre prompt>, <context>, <prompt>, <model>, and documented chat placeholders. (JSON template; default empty.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicesparsermode"></a>`AzerothVoices.ParserMode`

- **Data Type**: Enum (`ProviderJson/LegacyRegex`)
- **Default Value**: `ProviderJson`
- **Description**: ProviderJson parses OpenAI chat/Responses, Gemini candidates, and Ollama JSON; LegacyRegex uses the regex settings below. (Enum ProviderJson/LegacyRegex; default ProviderJson.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicescacertfile"></a>`AzerothVoices.CACertFile`

- **Data Type**: String
- **Default Value**: *(Empty)*
- **Description**: Empty uses system CAs; otherwise uses this custom CA bundle. (File path; default empty.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicesallowlocalhttp"></a>`AzerothVoices.AllowLocalHttp`

- **Data Type**: Boolean (`0` or `1`)
- **Default Value**: `1`
- **Description**: Allows plain HTTP only for local/loopback providers, never remote HTTP. (Default 1.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicesconnecttimeoutseconds"></a>`AzerothVoices.ConnectTimeoutSeconds`

- **Data Type**: Integer (Seconds, minimum 1; default 10.)
- **Default Value**: `10`
- **Description**: TCP/TLS connection timeout. (Seconds, minimum 1; default 10.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="aiplayerbotllmgenerationtimeout"></a>`AiPlayerbot.LLMGenerationTimeout`

- **Data Type**: Integer (Seconds, minimum 1; default 60.)
- **Default Value**: `60`
- **Description**: Provider read/write generation timeout. (Seconds, minimum 1; default 60.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicesmaxresponsebytes"></a>`AzerothVoices.MaxResponseBytes`

- **Data Type**: Integer
- **Default Value**: `65536`
- **Description**: Rejects oversized HTTP responses. (Bytes, minimum 1024; default 65536.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicesmaxtokens"></a>`AzerothVoices.MaxTokens`

- **Data Type**: Integer (Tokens, minimum 1; default 512.)
- **Default Value**: `512`
- **Description**: Requested output limit in generated provider JSON. (Tokens, minimum 1; default 512.)
- **Operator Guidance & Impact**: Directly limits output length. Lower values keep chat punchy and reduce API completion token costs; higher values allow more descriptive storytelling.

### <a id="azerothvoicesthinkingmode"></a>`AzerothVoices.Thinking.Mode`

- **Data Type**: Enum (`Auto/On/Off`)
- **Default Value**: `Auto`
- **Description**: Official-endpoint Thinking policy. Only api.openai.com and generativelanguage.googleapis.com receive Thinking fields; every proxy or other OpenAI-compatible provider stays a normal non-thinking provider. The former AzerothVoices.ReasoningEffort key is retired and ignored. Auto applies a control only for an officially recognised provider/model and an enabled AutoKind; On applies it for any recognised provider/model; Off omits the optional control and, for models whose reasoning cannot be disabled, sends the lowest documented level without pretending to disable reasoning. (Enum Auto/On/Off; default Auto.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicesthinkingautodetect"></a>`AzerothVoices.Thinking.AutoDetect`

- **Data Type**: Boolean (`0` or `1`)
- **Default Value**: `1`
- **Description**: Recognises the provider and model capability from the endpoint and model name without probing the provider. With 0 only an explicit Thinking.Mode = On applies a control; the module never guesses for an unrecognised model. (Accepted 0/1; default 1.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicesthinkingeffort"></a>`AzerothVoices.Thinking.Effort`

- **Data Type**: Enum (`Minimal/Low/Medium/High`)
- **Default Value**: `Low`
- **Description**: Requested reasoning level. Minimal collapses to Low on the Responses API and on Gemini and always-on models. (Enum Minimal/Low/Medium/High; default Low.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicesthinkingtokenreserve"></a>`AzerothVoices.Thinking.TokenReserve`

- **Data Type**: Integer (Tokens, 0-100000; default 1024.)
- **Default Value**: `1024`
- **Description**: Added to the active output-token field only when a Thinking control is actually applied, so a reasoning budget never truncates the visible reply. (Tokens, 0-100000; default 1024.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicesthinkingautokinds"></a>`AzerothVoices.Thinking.AutoKinds`

- **Data Type**: String (Comma-separated list)
- **Default Value**: `PersonalityGeneration,EventChat`
- **Description**: Request purposes that Auto mode may control. PersonalityGeneration covers identity generation; DirectChat covers PlayerBot direct replies, TargetedNpc the named/selected/scene NPC triggers, EventChat event reactions, AmbientChat random chatter, FollowupChat generated follow-ups. Overheard chat, GM tests, boss, proximity, group and guild triggers stay non-reasoning in Auto. (Comma-separated kinds, case-insensitive, duplicates ignored; default PersonalityGeneration,EventChat.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicestemperature"></a>`AzerothVoices.Temperature`

- **Data Type**: Decimal (Decimal 0.0-2.0; default 0.8.)
- **Default Value**: `0.8`
- **Description**: Sampling randomness; invalid values are clamped. (Decimal 0.0-2.0; default 0.8.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicestopp"></a>`AzerothVoices.TopP`

- **Data Type**: Decimal (Decimal 0.0-1.0; default 0.95.)
- **Default Value**: `0.95`
- **Description**: Nucleus sampling; invalid values are clamped. (Decimal 0.0-1.0; default 0.95.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

---

## 3. Worker pool, queue, limits, and cooldowns

| Setting Name | Type / Range | Default | Summary |
| :--- | :--- | :--- | :--- |
| [`AzerothVoices.WorkerThreads`](#azerothvoicesworkerthreads) | Integer | `8` | Fixed background HTTP workers; core objects stay on world thread. (1-32; de... |
| [`AiPlayerbot.LLMMaxSimultaniousGenerations`](#aiplayerbotllmmaxsimultaniousgenerations) | Integer | `8` | Legacy misspelled fallback used only if WorkerThreads is absent. (1-32; def... |
| [`AzerothVoices.QueueMaximum`](#azerothvoicesqueuemaximum) | Integer | `128` | Hard pending-queue memory bound. (Requests, minimum 1; default 128.) |
| [`AzerothVoices.HighPriorityReserve`](#azerothvoiceshighpriorityreserve) | Integer | `32` | Queue capacity reserved for direct/group replies. (0-QueueMaximum; default ... |
| [`AzerothVoices.GlobalRequestsPerMinute`](#azerothvoicesglobalrequestsperminute) | Integer | `60` | Global budget shared by all workers. (Requests/minute, minimum 1; default 6... |
| [`AzerothVoices.RequestTTLSeconds`](#azerothvoicesrequestttlseconds) | Integer (Seconds, minimum 1; default 90.) | `90` | Drops queued requests that are no longer timely. (Seconds, minimum 1; defau... |
| [`AzerothVoices.AmbientActorCooldownSeconds`](#azerothvoicesambientactorcooldownseconds) | Integer (Seconds, minimum 0; default 120.) | `120` | Longer per-actor random-chatter cooldown. (Seconds, minimum 0; default 120.... |
| [`AzerothVoices.BotSpeakerCooldownSeconds`](#azerothvoicesbotspeakercooldownseconds) | Integer (Seconds) | `900` | Per-bot cooldown (seconds) for General channel speech. After a bot speaks i... |
| [`AzerothVoices.SpeakerCoalesce.Enable`](#azerothvoicesspeakercoalesceenable) | Boolean (`0` or `1`) | `1` | Enables merging rapid consecutive messages from a real human player into a ... |
| [`AzerothVoices.SpeakerCoalesce.DelayMilliseconds`](#azerothvoicesspeakercoalescedelaymilliseconds) | Integer (Milliseconds, minimum 500, default 2000.) | `2000` | Time window in milliseconds to wait for additional follow-up lines from the... |
| [`AzerothVoices.SpeakerCoalesce.MaxLines`](#azerothvoicesspeakercoalescemaxlines) | Integer | `3` | Maximum number of consecutive lines to merge before forcing dispatch. (Coun... |
| [`AzerothVoices.RetryMaximum`](#azerothvoicesretrymaximum) | Integer | `1` | Retries transient connection, 429, and 5xx failures. (Retries, minimum 0; d... |
| [`AzerothVoices.RetryBackoffMilliseconds`](#azerothvoicesretrybackoffmilliseconds) | Integer (Milliseconds, minimum 0; default 500.) | `500` | Base increasing delay between retries. (Milliseconds, minimum 0; default 50... |


### <a id="azerothvoicesworkerthreads"></a>`AzerothVoices.WorkerThreads`

- **Data Type**: Integer
- **Default Value**: `8`
- **Description**: Fixed background HTTP workers; core objects stay on world thread. (1-32; default 8.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="aiplayerbotllmmaxsimultaniousgenerations"></a>`AiPlayerbot.LLMMaxSimultaniousGenerations`

- **Data Type**: Integer
- **Default Value**: `8`
- **Description**: Legacy misspelled fallback used only if WorkerThreads is absent. (1-32; default 8.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicesqueuemaximum"></a>`AzerothVoices.QueueMaximum`

- **Data Type**: Integer
- **Default Value**: `128`
- **Description**: Hard pending-queue memory bound. (Requests, minimum 1; default 128.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoiceshighpriorityreserve"></a>`AzerothVoices.HighPriorityReserve`

- **Data Type**: Integer
- **Default Value**: `32`
- **Description**: Queue capacity reserved for direct/group replies. (0-QueueMaximum; default 32.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicesglobalrequestsperminute"></a>`AzerothVoices.GlobalRequestsPerMinute`

- **Data Type**: Integer
- **Default Value**: `60`
- **Description**: Global budget shared by all workers. (Requests/minute, minimum 1; default 60.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicesrequestttlseconds"></a>`AzerothVoices.RequestTTLSeconds`

- **Data Type**: Integer (Seconds, minimum 1; default 90.)
- **Default Value**: `90`
- **Description**: Drops queued requests that are no longer timely. (Seconds, minimum 1; default 90.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicesambientactorcooldownseconds"></a>`AzerothVoices.AmbientActorCooldownSeconds`

- **Data Type**: Integer (Seconds, minimum 0; default 120.)
- **Default Value**: `120`
- **Description**: Longer per-actor random-chatter cooldown. (Seconds, minimum 0; default 120.)
- **Operator Guidance & Impact**: Controls rate-limiting. Increasing this value prevents repetitive triggers and reduces API call frequency; setting to `0` removes time-based gating for this specific event.

### <a id="azerothvoicesbotspeakercooldownseconds"></a>`AzerothVoices.BotSpeakerCooldownSeconds`

- **Data Type**: Integer (Seconds)
- **Default Value**: `900`
- **Description**: Per-bot cooldown (seconds) for General channel speech. After a bot speaks in General, it cannot speak again until this cooldown expires. Spreads voices across different bots so the same one doesn't dominate. Does NOT affect party chat or event reactions. Default: 900 (15 minutes)
- **Operator Guidance & Impact**: Controls rate-limiting. Increasing this value prevents repetitive triggers and reduces API call frequency; setting to `0` removes time-based gating for this specific event.

### <a id="azerothvoicesspeakercoalesceenable"></a>`AzerothVoices.SpeakerCoalesce.Enable`

- **Data Type**: Boolean (`0` or `1`)
- **Default Value**: `1`
- **Description**: Enables merging rapid consecutive messages from a real human player into a single prompt for bots and NPCs. For example, typing "hi" followed by "ready for deadmines?" within the window is combined into "hi ready for deadmines?" before calling the LLM. Recommended: 1. (Boolean, default 1.)
- **Operator Guidance & Impact**: Keep enabled (`1`) for standard functionality. Set to `0` to completely bypass this feature and eliminate its processing overhead.

### <a id="azerothvoicesspeakercoalescedelaymilliseconds"></a>`AzerothVoices.SpeakerCoalesce.DelayMilliseconds`

- **Data Type**: Integer (Milliseconds, minimum 500, default 2000.)
- **Default Value**: `2000`
- **Description**: Time window in milliseconds to wait for additional follow-up lines from the player before dispatching the combined message to the LLM. (Milliseconds, minimum 500, default 2000.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicesspeakercoalescemaxlines"></a>`AzerothVoices.SpeakerCoalesce.MaxLines`

- **Data Type**: Integer
- **Default Value**: `3`
- **Description**: Maximum number of consecutive lines to merge before forcing dispatch. (Count, minimum 2, default 3.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicesretrymaximum"></a>`AzerothVoices.RetryMaximum`

- **Data Type**: Integer
- **Default Value**: `1`
- **Description**: Retries transient connection, 429, and 5xx failures. (Retries, minimum 0; default 1.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicesretrybackoffmilliseconds"></a>`AzerothVoices.RetryBackoffMilliseconds`

- **Data Type**: Integer (Milliseconds, minimum 0; default 500.)
- **Default Value**: `500`
- **Description**: Base increasing delay between retries. (Milliseconds, minimum 0; default 500.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

---

## 4. Shared prompts and PlayerBots-compatible prompt names

| Setting Name | Type / Range | Default | Summary |
| :--- | :--- | :--- | :--- |
| [`AzerothVoices.GlobalMode`](#azerothvoicesglobalmode) | Enum (`Roleplay/Normal`) | `Normal` | Selects PlayerBot conversation style for PlayerBot-to-real-player and Playe... |
| [`AzerothVoices.GlobalPrompt.Roleplay`](#azerothvoicesglobalpromptroleplay) | String | `You create short, natural in-world dialogue for Vanilla/Turtle WoW. The speaker lives in Azeroth and treats its places, factions, quests, creatures, dungeons, travel, danger, and events as real lived experience. Stay in character and do not narrate actions. Never mention AI, prompts, policies, APIs, automation, game servers, controlling a character, or being a real-world player. Never prefix your response with your name, character name, or dialogue tags (such as 'Name:' or '[Name] whispers:'). Return only what the speaker says.` | System prompt selected by GlobalMode=Roleplay; it keeps PlayerBots and NPCs... |
| [`AzerothVoices.GlobalPrompt.Normal`](#azerothvoicesglobalpromptnormal) | String | `You create short, natural World of Warcraft chat. Interaction metadata: speaker=<bot type>; counterpart=<other type>; counterpart-race=<other race>. If the speaker is an NPC or counterpart-race is NPC, speak entirely in-world as a believable Vanilla/Turtle WoW character and never talk like someone at a computer. Otherwise the speaker is a PlayerBot representing a believable real human playing World of Warcraft: talk naturally like a real player about quests, leveling, gear, builds, talents, dungeons, guilds, PvP, travel, wipes, groups, and game mechanics when relevant; do not pretend Azeroth is real unless the conversation itself is roleplay. Avoid forced gamer slang and repetitive stereotypes. Never mention AI, prompts, policies, APIs, automation, or being a narrator. Never prefix your response with your name, character name, or dialogue tags (such as 'Name:' or '[Name] whispers:'). Return only what the speaker says.` | System prompt selected by GlobalMode=Normal for PlayerBot-to-real-player an... |
| [`AzerothVoices.GlobalPrompt`](#azerothvoicesglobalprompt) | String | `You create short, natural in-game dialogue for World of Warcraft. Stay in the game world. Never mention AI, prompts, policies, APIs, or being a narrator. Never prefix your response with your name or dialogue tags. Return only what the speaker says.` | Legacy compatibility fallback for GlobalPrompt.Roleplay when that key is ab... |
| [`AiPlayerbot.LLMPrePrompt`](#aiplayerbotllmpreprompt) | String (Template with placeholders) | `You are <bot name>, a level <bot level> <bot race> <bot class> in <bot subzone>, <bot zone>. <bot personality block>` | Describes a PlayerBot before a normal response. (Text template.) |
| [`AiPlayerbot.LLMPrompt`](#aiplayerbotllmprompt) | String (Template with placeholders) | `<sender name>: <initial message>` | Formats incoming chat for the provider using the actual incoming speaker na... |
| [`AiPlayerbot.LLMPostPrompt`](#aiplayerbotllmpostprompt) | String (Template with placeholders) | *(Empty)* | Optional response cue appended after incoming chat; empty by default becaus... |
| [`AiPlayerbot.LLMRpgPrompt`](#aiplayerbotllmrpgprompt) | String (Template with placeholders) | `You are <bot name>, an NPC in <bot subzone>, <bot zone>. Reply briefly in character.` | Shared prompt for a nearby NPC speaker. (Text template.) |
| [`AiPlayerbot.LLMContextLength`](#aiplayerbotllmcontextlength) | Integer (Characters, minimum 0; default 4096.) | `4096` | Caps the combined personality, live environment, current/older snapshots, h... |
| [`AiPlayerbot.LLMGlobalContext`](#aiplayerbotllmglobalcontext) | Boolean (`0` or `1`) | `0` | 0 isolates history by speaker/scope; 1 shares actor-wide history. (Default ... |
| [`AiPlayerbot.LLMResponseStartPattern`](#aiplayerbotllmresponsestartpattern) | String | `("content":\s*")` | LegacyRegex-only response start matcher. (ECMAScript regex.) |
| [`AiPlayerbot.LLMResponseEndPattern`](#aiplayerbotllmresponseendpattern) | String | `("|\b(?!<sender name>\b)(\w+):)` | LegacyRegex-only response end matcher. (ECMAScript regex.) |
| [`AiPlayerbot.LLMResponseDeletePattern`](#aiplayerbotllmresponsedeletepattern) | String | `(\\n|<sender name>:|\\[^ ]+)` | LegacyRegex-only unwanted-fragment remover. (ECMAScript regex.) |
| [`AiPlayerbot.LLMResponseSplitPattern`](#aiplayerbotllmresponsesplitpattern) | String | *(Empty)* | Optional LegacyRegex multi-line split matcher. (ECMAScript regex; default e... |
| [`AiPlayerbot.LLMBlockedReplyChannels`](#aiplayerbotllmblockedreplychannels) | String (Comma-separated list) | *(Empty)* | Blocks matching replies, ignoring case. (Comma-separated scope/channel name... |


### <a id="azerothvoicesglobalmode"></a>`AzerothVoices.GlobalMode`

- **Data Type**: Enum (`Roleplay/Normal`)
- **Default Value**: `Normal`
- **Description**: Selects PlayerBot conversation style for PlayerBot-to-real-player and PlayerBot-to-PlayerBot dialogue; Roleplay keeps them in-world, Normal makes them speak like believable real WoW players, and any interaction involving an NPC is forced to Roleplay by typed C++ participant policy before provider JSON is sent. (Enum Roleplay/Normal; default Normal.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicesglobalpromptroleplay"></a>`AzerothVoices.GlobalPrompt.Roleplay`

- **Data Type**: String
- **Default Value**: `You create short, natural in-world dialogue for Vanilla/Turtle WoW. The speaker lives in Azeroth and treats its places, factions, quests, creatures, dungeons, travel, danger, and events as real lived experience. Stay in character and do not narrate actions. Never mention AI, prompts, policies, APIs, automation, game servers, controlling a character, or being a real-world player. Never prefix your response with your name, character name, or dialogue tags (such as 'Name:' or '[Name] whispers:'). Return only what the speaker says.`
- **Description**: System prompt selected by GlobalMode=Roleplay; it keeps PlayerBots and NPCs inside Azeroth and forbids real-world player/meta framing. The older AzerothVoices.GlobalPrompt key is used only as a compatibility fallback when this key is absent. (Text.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicesglobalpromptnormal"></a>`AzerothVoices.GlobalPrompt.Normal`

- **Data Type**: String
- **Default Value**: `You create short, natural World of Warcraft chat. Interaction metadata: speaker=<bot type>; counterpart=<other type>; counterpart-race=<other race>. If the speaker is an NPC or counterpart-race is NPC, speak entirely in-world as a believable Vanilla/Turtle WoW character and never talk like someone at a computer. Otherwise the speaker is a PlayerBot representing a believable real human playing World of Warcraft: talk naturally like a real player about quests, leveling, gear, builds, talents, dungeons, guilds, PvP, travel, wipes, groups, and game mechanics when relevant; do not pretend Azeroth is real unless the conversation itself is roleplay. Avoid forced gamer slang and repetitive stereotypes. Never mention AI, prompts, policies, APIs, automation, or being a narrator. Never prefix your response with your name, character name, or dialogue tags (such as 'Name:' or '[Name] whispers:'). Return only what the speaker says.`
- **Description**: System prompt selected by GlobalMode=Normal for PlayerBot-to-real-player and PlayerBot-to-PlayerBot dialogue; NPC-involved dialogue is switched to GlobalPrompt.Roleplay by C++ before provider serialization, so customizing this prompt cannot opt NPCs into Normal mode. (Text.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicesglobalprompt"></a>`AzerothVoices.GlobalPrompt`

- **Data Type**: String
- **Default Value**: `You create short, natural in-game dialogue for World of Warcraft. Stay in the game world. Never mention AI, prompts, policies, APIs, or being a narrator. Never prefix your response with your name or dialogue tags. Return only what the speaker says.`
- **Description**: Legacy compatibility fallback for GlobalPrompt.Roleplay when that key is absent; existing installations may keep this value during migration. (Text.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="aiplayerbotllmpreprompt"></a>`AiPlayerbot.LLMPrePrompt`

- **Data Type**: String (Template with placeholders)
- **Default Value**: `You are <bot name>, a level <bot level> <bot race> <bot class> in <bot subzone>, <bot zone>. <bot personality block>`
- **Description**: Describes a PlayerBot before a normal response. (Text template.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="aiplayerbotllmprompt"></a>`AiPlayerbot.LLMPrompt`

- **Data Type**: String (Template with placeholders)
- **Default Value**: `<sender name>: <initial message>`
- **Description**: Formats incoming chat for the provider using the actual incoming speaker name. (Text template.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="aiplayerbotllmpostprompt"></a>`AiPlayerbot.LLMPostPrompt`

- **Data Type**: String (Template with placeholders)
- **Default Value**: *(Empty)*
- **Description**: Optional response cue appended after incoming chat; empty by default because the system/pre-prompt already identifies the responding actor. (Text template; default empty.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="aiplayerbotllmrpgprompt"></a>`AiPlayerbot.LLMRpgPrompt`

- **Data Type**: String (Template with placeholders)
- **Default Value**: `You are <bot name>, an NPC in <bot subzone>, <bot zone>. Reply briefly in character.`
- **Description**: Shared prompt for a nearby NPC speaker. (Text template.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="aiplayerbotllmcontextlength"></a>`AiPlayerbot.LLMContextLength`

- **Data Type**: Integer (Characters, minimum 0; default 4096.)
- **Default Value**: `4096`
- **Description**: Caps the combined personality, live environment, current/older snapshots, histories, surrounding chat, and RAG context added to a request. (Characters, minimum 0; default 4096.)
- **Operator Guidance & Impact**: Limits prompt context injected into LLM requests. Keep balanced to ensure bot personalities, memories, and surrounding dialogue fit comfortably within LLM context windows without token bloat.

### <a id="aiplayerbotllmglobalcontext"></a>`AiPlayerbot.LLMGlobalContext`

- **Data Type**: Boolean (`0` or `1`)
- **Default Value**: `0`
- **Description**: 0 isolates history by speaker/scope; 1 shares actor-wide history. (Default 0.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="aiplayerbotllmresponsestartpattern"></a>`AiPlayerbot.LLMResponseStartPattern`

- **Data Type**: String
- **Default Value**: `("content":\s*")`
- **Description**: LegacyRegex-only response start matcher. (ECMAScript regex.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="aiplayerbotllmresponseendpattern"></a>`AiPlayerbot.LLMResponseEndPattern`

- **Data Type**: String
- **Default Value**: `("|\b(?!<sender name>\b)(\w+):)`
- **Description**: LegacyRegex-only response end matcher. (ECMAScript regex.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="aiplayerbotllmresponsedeletepattern"></a>`AiPlayerbot.LLMResponseDeletePattern`

- **Data Type**: String
- **Default Value**: `(\\n|<sender name>:|\\[^ ]+)`
- **Description**: LegacyRegex-only unwanted-fragment remover. (ECMAScript regex.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="aiplayerbotllmresponsesplitpattern"></a>`AiPlayerbot.LLMResponseSplitPattern`

- **Data Type**: String
- **Default Value**: *(Empty)*
- **Description**: Optional LegacyRegex multi-line split matcher. (ECMAScript regex; default empty.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="aiplayerbotllmblockedreplychannels"></a>`AiPlayerbot.LLMBlockedReplyChannels`

- **Data Type**: String (Comma-separated list)
- **Default Value**: *(Empty)*
- **Description**: Blocks matching replies, ignoring case. (Comma-separated scope/channel names; default empty.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

---

## 5. Persistent PlayerBot personalities

| Setting Name | Type / Range | Default | Summary |
| :--- | :--- | :--- | :--- |
| [`AzerothVoices.Personality.Enable`](#azerothvoicespersonalityenable) | Boolean (`0` or `1`) | `1` | Enables persistent PlayerBot personalities and backgrounds; accepted values... |
| [`AzerothVoices.Personality.BackgroundMode`](#azerothvoicespersonalitybackgroundmode) | Boolean (`0` or `1`) | `1` | Selects background style; 0 = roleplay Azeroth character, 1 = fictional rea... |
| [`AzerothVoices.Personality.GenerateBackground`](#azerothvoicespersonalitygeneratebackground) | Boolean (`0` or `1`) | `1` | Generates and persists a background story for missing personalities; accept... |
| [`AzerothVoices.Personality.GenerateTone`](#azerothvoicespersonalitygeneratetone) | Boolean (`0` or `1`) | `1` | Traits are compile-time fixed at exactly three. The former AzerothVoices.Pe... |
| [`AzerothVoices.Personality.GenerateOnDemand`](#azerothvoicespersonalitygenerateondemand) | Boolean (`0` or `1`) | `1` | Generates missing personalities when a PlayerBot first enters an accepted d... |
| [`AzerothVoices.Personality.UseInRandom`](#azerothvoicespersonalityuseinrandom) | Boolean (`0` or `1`) | `1` | Uses the persistent PlayerBot personality in random/ambient and generated-f... |
| [`AzerothVoices.Personality.UseInEvents`](#azerothvoicespersonalityuseinevents) | Boolean (`0` or `1`) | `1` | Uses the persistent PlayerBot personality in automatic and adapter event pr... |
| [`AzerothVoices.Personality.GenerationRetrySeconds`](#azerothvoicespersonalitygenerationretryseconds) | Integer (Seconds) | `300` | Sets the minimum delay after failed generation before the same bot can retr... |
| [`AzerothVoices.Personality.MaxBackgroundChars`](#azerothvoicespersonalitymaxbackgroundchars) | Integer | `500` | Bounds a generated stored background; accepted range 100-1500 characters; d... |
| [`AzerothVoices.Personality.MaxPromptChars`](#azerothvoicespersonalitymaxpromptchars) | Integer | `700` | Bounds personality and background text inserted into each prompt; accepted ... |


### <a id="azerothvoicespersonalityenable"></a>`AzerothVoices.Personality.Enable`

- **Data Type**: Boolean (`0` or `1`)
- **Default Value**: `1`
- **Description**: Enables persistent PlayerBot personalities and backgrounds; accepted values 0/1; default 1; disabling preserves stored data and previous chat behavior.
- **Operator Guidance & Impact**: Keep enabled (`1`) for standard functionality. Set to `0` to completely bypass this feature and eliminate its processing overhead.

### <a id="azerothvoicespersonalitybackgroundmode"></a>`AzerothVoices.Personality.BackgroundMode`

- **Data Type**: Boolean (`0` or `1`)
- **Default Value**: `1`
- **Description**: Selects background style; 0 = roleplay Azeroth character, 1 = fictional real-world WoW player persona with a randomly generated adult age (18-60) included in the story; default 1.
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicespersonalitygeneratebackground"></a>`AzerothVoices.Personality.GenerateBackground`

- **Data Type**: Boolean (`0` or `1`)
- **Default Value**: `1`
- **Description**: Generates and persists a background story for missing personalities; accepted values 0/1; default 1; disabling preserves stored backgrounds.
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicespersonalitygeneratetone"></a>`AzerothVoices.Personality.GenerateTone`

- **Data Type**: Boolean (`0` or `1`)
- **Default Value**: `1`
- **Description**: Traits are compile-time fixed at exactly three. The former AzerothVoices.Personality.TraitCount option was removed; stored rows with any other trait count are treated as stale and regenerated lazily. Generates a persistent speaking tone derived from traits and background; accepted values 0/1; default 1.
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicespersonalitygenerateondemand"></a>`AzerothVoices.Personality.GenerateOnDemand`

- **Data Type**: Boolean (`0` or `1`)
- **Default Value**: `1`
- **Description**: Generates missing personalities when a PlayerBot first enters an accepted dialogue request; accepted values 0/1; default 1.
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicespersonalityuseinrandom"></a>`AzerothVoices.Personality.UseInRandom`

- **Data Type**: Boolean (`0` or `1`)
- **Default Value**: `1`
- **Description**: Uses the persistent PlayerBot personality in random/ambient and generated-follow-up prompts; accepted values 0/1; default 1.
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicespersonalityuseinevents"></a>`AzerothVoices.Personality.UseInEvents`

- **Data Type**: Boolean (`0` or `1`)
- **Default Value**: `1`
- **Description**: Uses the persistent PlayerBot personality in automatic and adapter event prompts; accepted values 0/1; default 1.
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicespersonalitygenerationretryseconds"></a>`AzerothVoices.Personality.GenerationRetrySeconds`

- **Data Type**: Integer (Seconds)
- **Default Value**: `300`
- **Description**: Sets the minimum delay after failed generation before the same bot can retry; accepted range 10-86400 seconds; default 300.
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicespersonalitymaxbackgroundchars"></a>`AzerothVoices.Personality.MaxBackgroundChars`

- **Data Type**: Integer
- **Default Value**: `500`
- **Description**: Bounds a generated stored background; accepted range 100-1500 characters; default 500.
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicespersonalitymaxpromptchars"></a>`AzerothVoices.Personality.MaxPromptChars`

- **Data Type**: Integer
- **Default Value**: `700`
- **Description**: Bounds personality and background text inserted into each prompt; accepted range 100-2000 characters; default 700.
- **Operator Guidance & Impact**: Limits prompt context injected into LLM requests. Keep balanced to ensure bot personalities, memories, and surrounding dialogue fit comfortably within LLM context windows without token bloat.

---

## 6. PlayerBot-to-real-player sentiment

> Sentiment is one directional: an AI-controlled PlayerBot actor toward one online real-player
target. NPC actors, NPC targets, PlayerBot targets, and PlayerBot-to-PlayerBot pairs are rejected.
The model sees only the C++-derived tier; GMs can inspect the exact -100..100 score. Dialogue
piggybacks one small hidden delta on the existing provider response, so no second API call is made.
A valid delta is applied only after the first generated line is delivered successfully. Score
changes are limited to a real player's direct Whisper, Party with exactly one PlayerBot in the
subgroup, named Party when several PlayerBots share the subgroup, or named Say/World message.
Event and Random requests can read the tier when enabled below but never change it. Neutral SQL
rows older than 90 days are removed by the existing hourly database cleanup.


| Setting Name | Type / Range | Default | Summary |
| :--- | :--- | :--- | :--- |
| [`AzerothVoices.Sentiment.Enable`](#azerothvoicessentimentenable) | Boolean (`0` or `1`) | `0` | Enables bounded sentiment context, updates, lazy decay, cache, and persiste... |
| [`AzerothVoices.Sentiment.UseInRandom`](#azerothvoicessentimentuseinrandom) | Boolean (`0` or `1`) | `0` | Uses the anchor real player's relationship tier in random/ambient prompts w... |
| [`AzerothVoices.Sentiment.UseInEvents`](#azerothvoicessentimentuseinevents) | Boolean (`0` or `1`) | `0` | Uses the real event subject's relationship tier in event prompts without ch... |
| [`AzerothVoices.Sentiment.ConversationMaximumDelta`](#azerothvoicessentimentconversationmaximumdelta) | Integer | `2` | Hard maximum magnitude for a model-reported qualifying direct player-chat c... |
| [`AzerothVoices.Sentiment.InactivityGraceDays`](#azerothvoicessentimentinactivitygracedays) | Integer | `7` | Full inactive days before lazy decay can begin; accepted range 0-365; defau... |
| [`AzerothVoices.Sentiment.PositiveDecayPerDay`](#azerothvoicessentimentpositivedecayperday) | Integer | `1` | Positive points removed per full inactive day after the grace period; accep... |
| [`AzerothVoices.Sentiment.NegativeDecayPerDay`](#azerothvoicessentimentnegativedecayperday) | Integer | `2` | Negative points moved toward neutral per full inactive day after the grace ... |
| [`AzerothVoices.Sentiment.CacheMaximumEntries`](#azerothvoicessentimentcachemaximumentries) | Integer | `4096` | Maximum lazily loaded relationship pairs in the world-thread RAM hot cache;... |
| [`AzerothVoices.Sentiment.PendingWriteMaximum`](#azerothvoicessentimentpendingwritemaximum) | Integer | `1024` | Maximum distinct dirty relationship pairs waiting for SQL; accepted range 1... |
| [`AzerothVoices.Sentiment.DatabaseFlushSeconds`](#azerothvoicessentimentdatabaseflushseconds) | Integer (Seconds) | `5` | Maximum delay before dirty sentiment upserts are queued; accepted range 1-3... |
| [`AzerothVoices.Sentiment.DatabaseFlushBatchSize`](#azerothvoicessentimentdatabaseflushbatchsize) | Integer | `50` | Distinct dirty pairs queued per normal flush; accepted range 1-PendingWrite... |


### <a id="azerothvoicessentimentenable"></a>`AzerothVoices.Sentiment.Enable`

- **Data Type**: Boolean (`0` or `1`)
- **Default Value**: `0`
- **Description**: Enables bounded sentiment context, updates, lazy decay, cache, and persistence; accepted values 0/1; default 0.
- **Operator Guidance & Impact**: Disabled (`0`) by default. Enable (`1`) only if you wish to activate this optional subsystem.

### <a id="azerothvoicessentimentuseinrandom"></a>`AzerothVoices.Sentiment.UseInRandom`

- **Data Type**: Boolean (`0` or `1`)
- **Default Value**: `0`
- **Description**: Uses the anchor real player's relationship tier in random/ambient prompts without changing the score; accepted values 0/1; default 0.
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicessentimentuseinevents"></a>`AzerothVoices.Sentiment.UseInEvents`

- **Data Type**: Boolean (`0` or `1`)
- **Default Value**: `0`
- **Description**: Uses the real event subject's relationship tier in event prompts without changing the score; accepted values 0/1; default 0.
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicessentimentconversationmaximumdelta"></a>`AzerothVoices.Sentiment.ConversationMaximumDelta`

- **Data Type**: Integer
- **Default Value**: `2`
- **Description**: Hard maximum magnitude for a model-reported qualifying direct player-chat change; accepted range 0-2; default 2. Zero keeps tier context but disables dialogue changes.
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicessentimentinactivitygracedays"></a>`AzerothVoices.Sentiment.InactivityGraceDays`

- **Data Type**: Integer
- **Default Value**: `7`
- **Description**: Full inactive days before lazy decay can begin; accepted range 0-365; default 7. No background sweep is performed.
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicessentimentpositivedecayperday"></a>`AzerothVoices.Sentiment.PositiveDecayPerDay`

- **Data Type**: Integer
- **Default Value**: `1`
- **Description**: Positive points removed per full inactive day after the grace period; accepted range 0-100; default 1.
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicessentimentnegativedecayperday"></a>`AzerothVoices.Sentiment.NegativeDecayPerDay`

- **Data Type**: Integer
- **Default Value**: `2`
- **Description**: Negative points moved toward neutral per full inactive day after the grace period; accepted range 0-100; default 2, so grudges fade faster than friendship.
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicessentimentcachemaximumentries"></a>`AzerothVoices.Sentiment.CacheMaximumEntries`

- **Data Type**: Integer
- **Default Value**: `4096`
- **Description**: Maximum lazily loaded relationship pairs in the world-thread RAM hot cache; accepted range 1-100000; default 4096.
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicessentimentpendingwritemaximum"></a>`AzerothVoices.Sentiment.PendingWriteMaximum`

- **Data Type**: Integer
- **Default Value**: `1024`
- **Description**: Maximum distinct dirty relationship pairs waiting for SQL; accepted range 1-100000; default 1024. Repeated changes to one pair are coalesced.
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicessentimentdatabaseflushseconds"></a>`AzerothVoices.Sentiment.DatabaseFlushSeconds`

- **Data Type**: Integer (Seconds)
- **Default Value**: `5`
- **Description**: Maximum delay before dirty sentiment upserts are queued; accepted range 1-3600 seconds; default 5.
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicessentimentdatabaseflushbatchsize"></a>`AzerothVoices.Sentiment.DatabaseFlushBatchSize`

- **Data Type**: Integer
- **Default Value**: `50`
- **Description**: Distinct dirty pairs queued per normal flush; accepted range 1-PendingWriteMaximum; default 50, clamped to PendingWriteMaximum when that cap is lower.
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

---

## 7. PlayerBot delivery scopes, response selection, and distances

| Setting Name | Type / Range | Default | Summary |
| :--- | :--- | :--- | :--- |
| [`AzerothVoices.Replies.Whisper`](#azerothvoicesreplieswhisper) | Boolean (`0` or `1`) | `1` | Enables direct PlayerBot whisper replies. (Default 1.) |
| [`AzerothVoices.Replies.Say`](#azerothvoicesrepliessay) | Boolean (`0` or `1`) | `1` | Enables PlayerBot Say replies plus eligible NPC ambient/event/combat-start ... |
| [`AzerothVoices.Replies.Yell`](#azerothvoicesrepliesyell) | Boolean (`0` or `1`) | `1` | Enables PlayerBot Yell replies. Azeroth Voices NPCs never use Yell. (Defaul... |
| [`AzerothVoices.Replies.Party`](#azerothvoicesrepliesparty) | Boolean (`0` or `1`) | `1` | Enables PlayerBot party-subgroup replies. (Default 1.) |
| [`AzerothVoices.Replies.Raid`](#azerothvoicesrepliesraid) | Boolean (`0` or `1`) | `1` | Enables PlayerBot raid replies. (Default 1.) |
| [`AzerothVoices.Replies.Guild`](#azerothvoicesrepliesguild) | Boolean (`0` or `1`) | `1` | Enables guild replies, guild ambient chatter, and guild-scoped event chatte... |
| [`AzerothVoices.Replies.Officer`](#azerothvoicesrepliesofficer) | Boolean (`0` or `1`) | `0` | Enables officer-channel replies for suitably ranked bots. (Default 0.) |
| [`AzerothVoices.Replies.World`](#azerothvoicesrepliesworld) | Boolean (`0` or `1`) | `1` | Enables cross-faction replies and ambient lines in WorldChannelName when th... |
| [`AzerothVoices.Replies.CustomChannels`](#azerothvoicesrepliescustomchannels) | Boolean (`0` or `1`) | `0` | Enables cross-faction replies in channels other than WorldChannelName when ... |
| [`AzerothVoices.WorldChannelName`](#azerothvoicesworldchannelname) | String | `World` | Used to classify and deliver world-chat messages. (Channel name; default Wo... |
| [`AzerothVoices.DisableRepliesInCombat`](#azerothvoicesdisablerepliesincombat) | Boolean (`0` or `1`) | `0` | Prevents actors currently in combat from being selected when set to 1. Comb... |
| [`AzerothVoices.MaxResponders`](#azerothvoicesmaxresponders) | Integer | `2` | Maximum responders to one incoming message. (Actors, minimum 1; default 2.) |
| [`AzerothVoices.ResponderFalloff.Enable`](#azerothvoicesresponderfalloffenable) | Boolean (`0` or `1`) | `1` | Enables progressive falloff only for responders to one normal non-whisper i... |
| [`AzerothVoices.ResponderFalloff.SecondChance`](#azerothvoicesresponderfalloffsecondchance) | Percentage (`0` - `100`) | `50` | Chance to open the second responder slot after one responder was successful... |
| [`AzerothVoices.ResponderFalloff.Delta`](#azerothvoicesresponderfalloffdelta) | Percentage (`0` - `100`) | `20` | Percentage points removed from each later successfully queued responder slo... |
| [`AzerothVoices.SayDistance`](#azerothvoicessaydistance) | Decimal Yards (Decimal yards, minimum 1; default 25.) | `25` | Local Say listener/responder and real-human observer distance for PlayerBot... |
| [`AzerothVoices.YellDistance`](#azerothvoicesyelldistance) | Decimal Yards (Decimal yards, minimum SayDistance; default 100.) | `100` | Local yell listener/responder distance. Bot-originated yell can trigger AI ... |
| [`AzerothVoices.NameMention.ExclusiveResponder`](#azerothvoicesnamementionexclusiveresponder) | Boolean (`0` or `1`) | `1` | When 1, any full online PlayerBot name explicitly mentioned in Say, Yell, P... |
| [`AzerothVoices.CandidateScore.DirectTarget`](#azerothvoicescandidatescoredirecttarget) | Integer (`0` - `200`) | `100` | Candidate priority base score for an actor directly targeted by the player or whispered. (Score, 0-200; default 100.) |
| [`AzerothVoices.CandidateScore.NameMention`](#azerothvoicescandidatescorenamemention) | Integer (`0` - `100`) | `80` | Candidate priority base score for an actor whose name is explicitly mentioned in the message. (Score, 0-100; default 80.) |
| [`AzerothVoices.CandidateScore.Untargeted`](#azerothvoicescandidatescoreuntargeted) | Integer (`0` - `100`) | `25` | Candidate priority base score for an untargeted and unmentioned actor in group or local chat. (Score, 0-100; default 25.) |
| [`AzerothVoices.Chance.DirectAddress`](#azerothvoiceschancedirectaddress) | Percentage (`0` - `100`) | `100` | Chance a selected or directly whispered actor answers. (Percentage; default... |
| [`AzerothVoices.Chance.NameMention`](#azerothvoiceschancenamemention) | Percentage (`0` - `100`) | `70` | Chance an actor named in the message answers. (Percentage; default 70.) |
| [`AzerothVoices.Chance.Overhear`](#azerothvoiceschanceoverhear) | Percentage (`0` - `100`) | `8` | Chance an otherwise eligible nearby actor joins in. (Percentage; default 8.... |
| [`AzerothVoices.Say.ConversationReplyWindowSeconds`](#azerothvoicessayconversationreplywindowseconds) | Integer (Seconds, minimum 5; default 30.) | `30` | Time window in seconds where an active /say conversation partner (NPC or Pl... |
| [`AzerothVoices.Say.ConversationMaxTurns`](#azerothvoicessayconversationmaxturns) | Integer | `5` | Maximum consecutive reply turns an active /say conversation partner will ta... |
| [`AzerothVoices.Party.SpeakerRotationPenalty`](#azerothvoicespartyspeakerrotationpenalty) | Percentage (`0` - `100`) | `40` | Score penalty applied to the bot that spoke last in party or raid chat when... |
| [`AzerothVoices.PlayerReplyChance.Say`](#azerothvoicesplayerreplychancesay) | Percentage (`0` - `100`) | `90` | Bot response chance to a real player's say/yell. (Percentage; default 90.) |
| [`AzerothVoices.BotReplyChance.Say`](#azerothvoicesbotreplychancesay) | Percentage (`0` - `100`) | `10` | Bot response chance to another bot's say/yell. (Percentage; default 10.) |
| [`AzerothVoices.PlayerReplyChance.Channel`](#azerothvoicesplayerreplychancechannel) | Percentage (`0` - `100`) | `60` | Bot response chance to a real player in world/custom chat. (Percentage; def... |
| [`AzerothVoices.BotReplyChance.Channel`](#azerothvoicesbotreplychancechannel) | Percentage (`0` - `100`) | `3` | Bot response chance to another bot in world/custom chat. Bot-originated cha... |
| [`AzerothVoices.PlayerReplyChance.Party`](#azerothvoicesplayerreplychanceparty) | Percentage (`0` - `100`) | `90` | Bot response chance to a real player in party/raid. (Percentage; default 90... |
| [`AzerothVoices.PlayerReplyChance.TargetActive.Enable`](#azerothvoicesplayerreplychancetargetactiveenable) | Boolean (`0` or `1`) | `1` | When enabled, if a human player has an active target in Say, Yell, Party, or Raid chat, untargeted companion bots suppress reply probability to TargetActive.Chance instead of full ambient rates. |
| [`AzerothVoices.PlayerReplyChance.TargetActive.Chance`](#azerothvoicesplayerreplychancetargetactivechance) | Percentage (`0` - `100`) | `60` | Bot response chance when the speaking player has an active target (applies to untargeted bots in Say, Yell, Party, Raid). (Percentage, 0-100; default 60.) |
| [`AzerothVoices.BotReplyChance.Party`](#azerothvoicesbotreplychanceparty) | Percentage (`0` - `100`) | `10` | Bot response chance to another bot in party/raid. Bot-originated replies al... |
| [`AzerothVoices.PlayerReplyChance.Guild`](#azerothvoicesplayerreplychanceguild) | Percentage (`0` - `100`) | `70` | Bot response chance to a real player in guild/officer chat. (Percentage; de... |
| [`AzerothVoices.BotReplyChance.Guild`](#azerothvoicesbotreplychanceguild) | Percentage (`0` - `100`) | `5` | Bot response chance to another bot in guild/officer chat. Bot-originated re... |
| [`AiPlayerbot.LLMBotToBotChatChance`](#aiplayerbotllmbottobotchatchance) | Percentage (`0` - `100`) | `10` | Global upper cap for PlayerBot-to-PlayerBot replies. Bot-to-bot whisper is ... |
| [`AiPlayerbot.LLMRpgAIChatChance`](#aiplayerbotllmrpgaichatchance) | Percentage (`0` - `100`) | `30` | Upper cap for NPC reactions to PlayerBot Say and for generated follow-ups w... |
| [`AzerothVoices.CommandBlacklist`](#azerothvoicescommandblacklist) | String (Comma-separated list) | `.,!,/,#,$,autogear,talents,summon,release,revive,attack,follow,stay,cast,quest,trainer,teleport,addon,DBM,Recount,Questie` | Messages beginning with any listed PlayerBots command or addon prefix are n... |
| [`AzerothVoices.CommandIgnoreList`](#azerothvoicescommandignorelist) | String (Comma-separated list) | *(Empty)* | Additional list of ignored command tokens, strategy commands, or prefixes. ... |


### <a id="azerothvoicesreplieswhisper"></a>`AzerothVoices.Replies.Whisper`

- **Data Type**: Boolean (`0` or `1`)
- **Default Value**: `1`
- **Description**: Enables direct PlayerBot whisper replies. (Default 1.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicesrepliessay"></a>`AzerothVoices.Replies.Say`

- **Data Type**: Boolean (`0` or `1`)
- **Default Value**: `1`
- **Description**: Enables PlayerBot Say replies plus eligible NPC ambient/event/combat-start Say and NPC reactions to human or PlayerBot Say. (Default 1.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicesrepliesyell"></a>`AzerothVoices.Replies.Yell`

- **Data Type**: Boolean (`0` or `1`)
- **Default Value**: `1`
- **Description**: Enables PlayerBot Yell replies. Azeroth Voices NPCs never use Yell. (Default 1.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicesrepliesparty"></a>`AzerothVoices.Replies.Party`

- **Data Type**: Boolean (`0` or `1`)
- **Default Value**: `1`
- **Description**: Enables PlayerBot party-subgroup replies. (Default 1.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicesrepliesraid"></a>`AzerothVoices.Replies.Raid`

- **Data Type**: Boolean (`0` or `1`)
- **Default Value**: `1`
- **Description**: Enables PlayerBot raid replies. (Default 1.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicesrepliesguild"></a>`AzerothVoices.Replies.Guild`

- **Data Type**: Boolean (`0` or `1`)
- **Default Value**: `1`
- **Description**: Enables guild replies, guild ambient chatter, and guild-scoped event chatter. (Default 1.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicesrepliesofficer"></a>`AzerothVoices.Replies.Officer`

- **Data Type**: Boolean (`0` or `1`)
- **Default Value**: `0`
- **Description**: Enables officer-channel replies for suitably ranked bots. (Default 0.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicesrepliesworld"></a>`AzerothVoices.Replies.World`

- **Data Type**: Boolean (`0` or `1`)
- **Default Value**: `1`
- **Description**: Enables cross-faction replies and ambient lines in WorldChannelName when the core's two-side channel interaction is enabled. Bot-originated and random World chatter requires one online real player anywhere on the server; it does not require channel-membership proof or proximity. (Default 1.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicesrepliescustomchannels"></a>`AzerothVoices.Replies.CustomChannels`

- **Data Type**: Boolean (`0` or `1`)
- **Default Value**: `0`
- **Description**: Enables cross-faction replies in channels other than WorldChannelName when the core's two-side channel interaction is enabled. When set to 1, bot-originated replies retain the conservative public-count gate because exact individual channel membership is private in this core. (Default 0.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicesworldchannelname"></a>`AzerothVoices.WorldChannelName`

- **Data Type**: String
- **Default Value**: `World`
- **Description**: Used to classify and deliver world-chat messages. (Channel name; default World.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicesdisablerepliesincombat"></a>`AzerothVoices.DisableRepliesInCombat`

- **Data Type**: Boolean (`0` or `1`)
- **Default Value**: `0`
- **Description**: Prevents actors currently in combat from being selected when set to 1. Combat replies, including the dedicated combat-start reaction, are enabled by default. (Default 0.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicesmaxresponders"></a>`AzerothVoices.MaxResponders`

- **Data Type**: Integer
- **Default Value**: `2`
- **Description**: Maximum responders to one incoming message. (Actors, minimum 1; default 2.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicesresponderfalloffenable"></a>`AzerothVoices.ResponderFalloff.Enable`

- **Data Type**: Boolean (`0` or `1`)
- **Default Value**: `1`
- **Description**: Enables progressive falloff only for responders to one normal non-whisper incoming chat message; 1 always permits the first successfully queued responder, then rolls each later responder slot, while 0 uses only AzerothVoices.MaxResponders. Generated follow-ups and event responders keep their separate existing settings. Direct selection/name targeting keeps its existing priority. (Accepted 0/1; default 1.)
- **Operator Guidance & Impact**: Keep enabled (`1`) for standard functionality. Set to `0` to completely bypass this feature and eliminate its processing overhead.

### <a id="azerothvoicesresponderfalloffsecondchance"></a>`AzerothVoices.ResponderFalloff.SecondChance`

- **Data Type**: Percentage (`0` - `100`)
- **Default Value**: `50`
- **Description**: Chance to open the second responder slot after one responder was successfully queued; if the roll passes, candidates are tried until one queues successfully or none remain. Existing scope, direct-address, name-mention, NPC, audience, cooldown, and eligibility gates still apply. (Percentage 0-100; default 50.)
- **Operator Guidance & Impact**: Adjust this probability up for chattier, more responsive bots, or down to conserve LLM API tokens on high-population servers.

### <a id="azerothvoicesresponderfalloffdelta"></a>`AzerothVoices.ResponderFalloff.Delta`

- **Data Type**: Percentage (`0` - `100`)
- **Default Value**: `20`
- **Description**: Percentage points removed from each later successfully queued responder slot: third = SecondChance - Delta, fourth = SecondChance - 2*Delta, floored at 0. (Percentage points 0-100; default 20.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicessaydistance"></a>`AzerothVoices.SayDistance`

- **Data Type**: Decimal Yards (Decimal yards, minimum 1; default 25.)
- **Default Value**: `25`
- **Description**: Local Say listener/responder and real-human observer distance for PlayerBots and NPCs. PlayerBots never count as observers. Combat-start reactions require the opening real player to be within this distance. (Decimal yards, minimum 1; default 25.)
- **Operator Guidance & Impact**: Defines the spatial radius for candidate discovery and line-of-sight checks. Keep aligned with visible WoW client chat bubbles.

### <a id="azerothvoicesyelldistance"></a>`AzerothVoices.YellDistance`

- **Data Type**: Decimal Yards (Decimal yards, minimum SayDistance; default 100.)
- **Default Value**: `100`
- **Description**: Local yell listener/responder distance. Bot-originated yell can trigger AI replies only when a real player is also within this distance. (Decimal yards, minimum SayDistance; default 100.)
- **Operator Guidance & Impact**: Defines the spatial radius for candidate discovery and line-of-sight checks. Keep aligned with visible WoW client chat bubbles.

### <a id="azerothvoicesnamementionexclusiveresponder"></a>`AzerothVoices.NameMention.ExclusiveResponder`

- **Data Type**: Boolean (`0` or `1`)
- **Default Value**: `1`
- **Description**: When 1, any full online PlayerBot name explicitly mentioned in Say, Yell, Party, Raid, Guild, Officer, World, or custom-channel chat makes only explicitly named PlayerBots eligible; unmentioned PlayerBots and NPCs are suppressed, while the existing NameMention chance still decides whether a named bot replies. Whisper is already single-target. (Accepted 0/1; default 1.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicescandidatescoredirecttarget"></a>`AzerothVoices.CandidateScore.DirectTarget`

- **Data Type**: Integer (`0` - `200`)
- **Default Value**: `100`
- **Description**: Candidate priority base score for an actor directly targeted by the speaking player or directly whispered. (Score, 0-200; default 100.)
- **Operator Guidance & Impact**: Sets the scoring baseline for direct-addressed candidates. Keeping this at `100` ensures targeted actors reliably take priority over name-mentioned (`80`) and untargeted (`25`) candidates.

### <a id="azerothvoicescandidatescorenamemention"></a>`AzerothVoices.CandidateScore.NameMention`

- **Data Type**: Integer (`0` - `100`)
- **Default Value**: `80`
- **Description**: Candidate priority base score for an actor whose name is explicitly mentioned in the message. (Score, 0-100; default 80.)
- **Operator Guidance & Impact**: Sets the scoring baseline for name-addressed bots below direct-target (100) but well above untargeted companions. Lowering this value diminishes the priority advantage of name-mentioned bots when multiple candidates compete.

### <a id="azerothvoicescandidatescoreuntargeted"></a>`AzerothVoices.CandidateScore.Untargeted`

- **Data Type**: Integer (`0` - `100`)
- **Default Value**: `25`
- **Description**: Candidate priority base score for an untargeted and unmentioned actor in group or local chat. (Score, 0-100; default 25.)
- **Operator Guidance & Impact**: Sets the scoring baseline for ambient companion bots when no specific entity is targeted or named. Keeping this at 25 preserves deterministic hierarchy (Direct Target 100 > Name-Mentioned 80 > Untargeted 25).

### <a id="azerothvoiceschancedirectaddress"></a>`AzerothVoices.Chance.DirectAddress`

- **Data Type**: Percentage (`0` - `100`)
- **Default Value**: `100`
- **Description**: Chance a selected or directly whispered actor answers. (Percentage; default 100.)
- **Operator Guidance & Impact**: Set to `0` to disable this trigger entirely, or `100` for deterministic triggering every time the event conditions are satisfied.

### <a id="azerothvoiceschancenamemention"></a>`AzerothVoices.Chance.NameMention`

- **Data Type**: Percentage (`0` - `100`)
- **Default Value**: `70`
- **Description**: Chance an actor named in the message answers. (Percentage; default 70.)
- **Operator Guidance & Impact**: Adjust this probability up for chattier, more responsive bots, or down to conserve LLM API tokens on high-population servers.

### <a id="azerothvoiceschanceoverhear"></a>`AzerothVoices.Chance.Overhear`

- **Data Type**: Percentage (`0` - `100`)
- **Default Value**: `8`
- **Description**: Chance an otherwise eligible nearby actor joins in. (Percentage; default 8.)
- **Operator Guidance & Impact**: Adjust this probability up for chattier, more responsive bots, or down to conserve LLM API tokens on high-population servers.

### <a id="azerothvoicessayconversationreplywindowseconds"></a>`AzerothVoices.Say.ConversationReplyWindowSeconds`

- **Data Type**: Integer (Seconds, minimum 5; default 30.)
- **Default Value**: `30`
- **Description**: Time window in seconds where an active /say conversation partner (NPC or PlayerBot) remains locked for subsequent turns when the player speaks again without naming another actor. (Seconds, minimum 5; default 30.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicessayconversationmaxturns"></a>`AzerothVoices.Say.ConversationMaxTurns`

- **Data Type**: Integer
- **Default Value**: `5`
- **Description**: Maximum consecutive reply turns an active /say conversation partner will take before requiring re-addressing. (Turns, minimum 1; default 5.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicespartyspeakerrotationpenalty"></a>`AzerothVoices.Party.SpeakerRotationPenalty`

- **Data Type**: Percentage (`0` - `100`)
- **Default Value**: `40`
- **Description**: Score penalty applied to the bot that spoke last in party or raid chat when multiple bots are eligible and none was explicitly named, ensuring diverse cooperative replies. (Percentage, 0-100; default 40.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicesplayerreplychancesay"></a>`AzerothVoices.PlayerReplyChance.Say`

- **Data Type**: Percentage (`0` - `100`)
- **Default Value**: `90`
- **Description**: Bot response chance to a real player's say/yell. (Percentage; default 90.)
- **Operator Guidance & Impact**: Adjust this probability up for chattier, more responsive bots, or down to conserve LLM API tokens on high-population servers.

### <a id="azerothvoicesbotreplychancesay"></a>`AzerothVoices.BotReplyChance.Say`

- **Data Type**: Percentage (`0` - `100`)
- **Default Value**: `10`
- **Description**: Bot response chance to another bot's say/yell. (Percentage; default 10.)
- **Operator Guidance & Impact**: Adjust this probability up for chattier, more responsive bots, or down to conserve LLM API tokens on high-population servers.

### <a id="azerothvoicesplayerreplychancechannel"></a>`AzerothVoices.PlayerReplyChance.Channel`

- **Data Type**: Percentage (`0` - `100`)
- **Default Value**: `60`
- **Description**: Bot response chance to a real player in world/custom chat. (Percentage; default 60.)
- **Operator Guidance & Impact**: Adjust this probability up for chattier, more responsive bots, or down to conserve LLM API tokens on high-population servers.

### <a id="azerothvoicesbotreplychancechannel"></a>`AzerothVoices.BotReplyChance.Channel`

- **Data Type**: Percentage (`0` - `100`)
- **Default Value**: `3`
- **Description**: Bot response chance to another bot in world/custom chat. Bot-originated channel replies also require the module to prove from public core counts that the channel has a real member. (Percentage; default 3.)
- **Operator Guidance & Impact**: Adjust this probability up for chattier, more responsive bots, or down to conserve LLM API tokens on high-population servers.

### <a id="azerothvoicesplayerreplychanceparty"></a>`AzerothVoices.PlayerReplyChance.Party`

- **Data Type**: Percentage (`0` - `100`)
- **Default Value**: `90`
- **Description**: Bot response chance to a real player in party/raid. (Percentage; default 90.)
- **Operator Guidance & Impact**: Adjust this probability up for chattier, more responsive bots, or down to conserve LLM API tokens on high-population servers.

### <a id="azerothvoicesplayerreplychancetargetactiveenable"></a>`AzerothVoices.PlayerReplyChance.TargetActive.Enable`

- **Data Type**: Boolean (`0` or `1`)
- **Default Value**: `1`
- **Description**: When enabled, if a human player has an active target in Say, Yell, Party, or Raid chat, untargeted companion bots suppress their reply probability to `TargetActive.Chance` instead of full ambient channel rates. Global channels (Guild, Officer, World) skip target suppression. (Default 1.)
- **Operator Guidance & Impact**: Keep enabled (`1`) to allow the speaking player to focus conversations on their selected target while still giving companion bots a moderate chance (e.g. 60%) to participate. Set to `0` to always evaluate untargeted bots at normal channel reply rates.

### <a id="azerothvoicesplayerreplychancetargetactivechance"></a>`AzerothVoices.PlayerReplyChance.TargetActive.Chance`

- **Data Type**: Percentage (`0` - `100`)
- **Default Value**: `60`
- **Description**: Bot response chance when the speaking player has an active target (applies to untargeted bots in Say, Yell, Party, Raid). (Percentage, 0-100; default 60.)
- **Operator Guidance & Impact**: Calibrates untargeted bot chatter when an active target exists. Set lower (e.g. `20` - `40`) if you prefer companion bots to mostly stay quiet when you have a target, or higher (e.g. `80` - `90`) for more proactive party commentary.

### <a id="azerothvoicesbotreplychanceparty"></a>`AzerothVoices.BotReplyChance.Party`

- **Data Type**: Percentage (`0` - `100`)
- **Default Value**: `10`
- **Description**: Bot response chance to another bot in party/raid. Bot-originated replies also require a real member in the party subgroup or raid. (Percentage; default 10.)
- **Operator Guidance & Impact**: Adjust this probability up for chattier, more responsive bots, or down to conserve LLM API tokens on high-population servers.

### <a id="azerothvoicesplayerreplychanceguild"></a>`AzerothVoices.PlayerReplyChance.Guild`

- **Data Type**: Percentage (`0` - `100`)
- **Default Value**: `70`
- **Description**: Bot response chance to a real player in guild/officer chat. (Percentage; default 70.)
- **Operator Guidance & Impact**: Adjust this probability up for chattier, more responsive bots, or down to conserve LLM API tokens on high-population servers.

### <a id="azerothvoicesbotreplychanceguild"></a>`AzerothVoices.BotReplyChance.Guild`

- **Data Type**: Percentage (`0` - `100`)
- **Default Value**: `5`
- **Description**: Bot response chance to another bot in guild/officer chat. Bot-originated replies also require a real member with listening rights. (Percentage; default 5.)
- **Operator Guidance & Impact**: Adjust this probability up for chattier, more responsive bots, or down to conserve LLM API tokens on high-population servers.

### <a id="aiplayerbotllmbottobotchatchance"></a>`AiPlayerbot.LLMBotToBotChatChance`

- **Data Type**: Percentage (`0` - `100`)
- **Default Value**: `10`
- **Description**: Global upper cap for PlayerBot-to-PlayerBot replies. Bot-to-bot whisper is disabled; every other scope requires a real audience. (Percentage; default 10.)
- **Operator Guidance & Impact**: Adjust this probability up for chattier, more responsive bots, or down to conserve LLM API tokens on high-population servers.

### <a id="aiplayerbotllmrpgaichatchance"></a>`AiPlayerbot.LLMRpgAIChatChance`

- **Data Type**: Percentage (`0` - `100`)
- **Default Value**: `30`
- **Description**: Upper cap for NPC reactions to PlayerBot Say and for generated follow-ups where either participant is an NPC. These interactions remain in Say and require a real human within SayDistance. (Percentage; default 30.)
- **Operator Guidance & Impact**: Adjust this probability up for chattier, more responsive bots, or down to conserve LLM API tokens on high-population servers.

### <a id="azerothvoicescommandblacklist"></a>`AzerothVoices.CommandBlacklist`

- **Data Type**: String (Comma-separated list)
- **Default Value**: `.,!,/,#,$,autogear,talents,summon,release,revive,attack,follow,stay,cast,quest,trainer,teleport,addon,DBM,Recount,Questie`
- **Description**: Messages beginning with any listed PlayerBots command or addon prefix are never sent to the provider; add local prefixes as needed. (Comma-separated case-insensitive prefixes.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicescommandignorelist"></a>`AzerothVoices.CommandIgnoreList`

- **Data Type**: String (Comma-separated list)
- **Default Value**: *(Empty)*
- **Description**: Additional list of ignored command tokens, strategy commands, or prefixes. (Comma-separated case-insensitive prefixes; default empty.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

---

## 8. NPC dialogue: reactions, combat-start, and the ordered /say reply chain

| Setting Name | Type / Range | Default | Summary |
| :--- | :--- | :--- | :--- |
| [`AzerothVoices.NPC.Enable`](#azerothvoicesnpcenable) | Boolean (`0` or `1`) | `1` | Allows eligible static NPCs to react to human or PlayerBot Say, to nearby e... |
| [`AzerothVoices.NPC.Distance`](#azerothvoicesnpcdistance) | Decimal Yards (Decimal yards, minimum 1; default 10.) | `10` | NPC candidate search radius around the local event subject, human/PlayerBot... |
| [`AzerothVoices.NPC.AllowedTypes`](#azerothvoicesnpcallowedtypes) | String (Template with placeholders) | `2,3,4,5,6,7,9` | Creature types eligible for generated NPC speech. Defaults to Dragonkin, De... |
| [`AzerothVoices.NPC.AllowNeutralAndHostile`](#azerothvoicesnpcallowneutralandhostile) | Boolean (`0` or `1`) | `1` | Allows both neutral and hostile NPC categories through the faction gate. Th... |
| [`AzerothVoices.NPC.ReplyChance.Friendly`](#azerothvoicesnpcreplychancefriendly) | Percentage (`0` - `100`) | `100` | The three disposition chances below are an additional probability gate alon... |
| [`AzerothVoices.NPC.ReplyChance.Neutral`](#azerothvoicesnpcreplychanceneutral) | Percentage (`0` - `100`) | `50` | Chance that an otherwise selected NPC neutral toward the current conversati... |
| [`AzerothVoices.NPC.ReplyChance.Hostile`](#azerothvoicesnpcreplychancehostile) | Percentage (`0` - `100`) | `25` | Chance that an otherwise selected NPC hostile or unfriendly toward the curr... |
| [`AzerothVoices.NPC.CombatStart.Enable`](#azerothvoicesnpccombatstartenable) | Boolean (`0` or `1`) | `1` | Enables one generated Say reaction from the exact creature a real player at... |
| [`AzerothVoices.NPC.CombatStart.Chance`](#azerothvoicesnpccombatstartchance) | Percentage (`0` - `100`) | `30` | Chance that the exact opening creature produces its one combat-start reacti... |
| [`AzerothVoices.NPC.CombatStart.CooldownSeconds`](#azerothvoicesnpccombatstartcooldownseconds) | Integer (Seconds, minimum 0; default 60.) | `60` | Minimum time between accepted combat-start creature reactions for the same ... |
| [`AzerothVoices.NPC.TargetedConversation.BotComments.Enable`](#azerothvoicesnpctargetedconversationbotcommentsenable) | Boolean (`0` or `1`) | `1` | Enables observer comments from nearby PlayerBots when a real player speaks ... |
| [`AzerothVoices.NPC.TargetedConversation.BotCommentChance`](#azerothvoicesnpctargetedconversationbotcommentchance) | Percentage (`0` - `100`) | `15` | Chance that nearby PlayerBots generate an observer comment when a real play... |
| [`AzerothVoices.NPC.TargetedConversation.MaxBotComments`](#azerothvoicesnpctargetedconversationmaxbotcomments) | Integer | `1` | Maximum number of PlayerBots allowed to make an observer comment per player... |
| [`AzerothVoices.NPC.MentionSuppressesDirectBotReply`](#azerothvoicesnpcmentionsuppressesdirectbotreply) | Boolean (`0` or `1`) | `1` | Suppresses direct PlayerBot replies in /say when a real player mentions or ... |
| [`AzerothVoices.NPC.MentionBotDirectReplyChance`](#azerothvoicesnpcmentionbotdirectreplychance) | Percentage (`0` - `100`) | `0` | If direct PlayerBot replies are not completely suppressed when speaking to ... |
| [`AzerothVoices.NPC.ReplyObserver.Enable`](#azerothvoicesnpcreplyobserverenable) | Boolean (`0` or `1`) | `1` | Enables observer comments from nearby PlayerBots when an NPC delivers a rep... |
| [`AzerothVoices.NPC.ReplyObserver.Chance`](#azerothvoicesnpcreplyobserverchance) | Percentage (`0` - `100`) | `25` | Chance that nearby PlayerBots generate an observer reaction to the NPC's an... |
| [`AzerothVoices.NPC.ReplyObserver.CooldownSeconds`](#azerothvoicesnpcreplyobservercooldownseconds) | Integer (Seconds, 0-86400; default 30.) | `30` | Cooldown between bot observer comments reacting to NPC interactions. (Secon... |
| [`AzerothVoices.TargetedBot.Observer.Enable`](#azerothvoicestargetedbotobserverenable) | Boolean (`0` or `1`) | `1` | Enables observer commentary from companion PlayerBots when a real player speaks to or interacts with a targeted companion PlayerBot. |
| [`AzerothVoices.TargetedBot.CommentOnPlayerChance`](#azerothvoicestargetedbotcommentonplayerchance) | Percentage (`0` - `100`) | `70` | Chance that an untargeted companion PlayerBot comments on what the real player said to the targeted bot. (Percentage, 0-100; default 70.) |
| [`AzerothVoices.TargetedBot.CommentOnReplyChance`](#azerothvoicestargetedbotcommentonreplychance) | Percentage (`0` - `100`) | `30` | Chance that an untargeted companion PlayerBot reacts to the reply delivered by the targeted bot. (Percentage, 0-100; default 30.) |
| [`AzerothVoices.TargetedBot.ObserverCooldownSeconds`](#azerothvoicestargetedbotobservercooldownseconds) | Integer (Seconds, 0-86400; default 30.) | `30` | Cooldown between companion bot observer comments reacting to targeted bot interactions. (Seconds, 0-86400; default 30.) |


### <a id="azerothvoicesnpcenable"></a>`AzerothVoices.NPC.Enable`

- **Data Type**: Boolean (`0` or `1`)
- **Default Value**: `1`
- **Description**: Allows eligible static NPCs to react to human or PlayerBot Say, to nearby events, and to a fresh combat opening. Ordinary ambient Say chatter is owned by the Proximity section below and classified dungeon/raid bosses are owned by the Boss Dialogue section, so this switch does not gate either of those. Neutral and hostile categories are enabled through the combined switch below. Pet, summon, guardian, totem, charmed, trigger, critter, and temporary speakers remain rejected. NPC output is Say only. (Default 1.)
- **Operator Guidance & Impact**: Keep enabled (`1`) for standard functionality. Set to `0` to completely bypass this feature and eliminate its processing overhead.

### <a id="azerothvoicesnpcdistance"></a>`AzerothVoices.NPC.Distance`

- **Data Type**: Decimal Yards (Decimal yards, minimum 1; default 10.)
- **Default Value**: `10`
- **Description**: NPC candidate search radius around the local event subject, human/PlayerBot speaker, or random real-player anchor. It is also the maximum actor-to-actor distance for every generated follow-up pair containing an NPC. Both follow-up actors must share one real-human observer within SayDistance. (Decimal yards, minimum 1; default 10.)
- **Operator Guidance & Impact**: Defines the spatial radius for candidate discovery and line-of-sight checks. Keep aligned with visible WoW client chat bubbles.

### <a id="azerothvoicesnpcallowedtypes"></a>`AzerothVoices.NPC.AllowedTypes`

- **Data Type**: String (Template with placeholders)
- **Default Value**: `2,3,4,5,6,7,9`
- **Description**: Creature types eligible for generated NPC speech. Defaults to Dragonkin, Demon, Elemental, Giant, Undead, Humanoid, and Mechanical; all NPC safety, faction, audience, distance, and combat checks still apply. The Proximity section below is stricter and accepts only humanoids unless an entry is listed in Proximity.Speaker.AllowNonHumanoidEntries. (Comma-separated creature_template.type values 0-11; default 2,3,4,5,6,7,9.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicesnpcallowneutralandhostile"></a>`AzerothVoices.NPC.AllowNeutralAndHostile`

- **Data Type**: Boolean (`0` or `1`)
- **Default Value**: `1`
- **Description**: Allows both neutral and hostile NPC categories through the faction gate. This does not bypass AllowedTypes or the existing ownership, temporary-creature, audience, distance, or combat rules. (Default 1.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicesnpcreplychancefriendly"></a>`AzerothVoices.NPC.ReplyChance.Friendly`

- **Data Type**: Percentage (`0` - `100`)
- **Default Value**: `100`
- **Description**: The three disposition chances below are an additional probability gate alongside the direct-address, name-mention, overhear, event, random, and follow-up chances. They apply to those normal NPC-speaking paths and never bypass NPC safety, faction, distance, audience, or combat checks. The dedicated combat-start path uses its own chance below instead. Chance that an otherwise selected NPC friendly toward the current conversational counterpart speaks. (Percentage; default 100.)
- **Operator Guidance & Impact**: Set to `0` to disable this trigger entirely, or `100` for deterministic triggering every time the event conditions are satisfied.

### <a id="azerothvoicesnpcreplychanceneutral"></a>`AzerothVoices.NPC.ReplyChance.Neutral`

- **Data Type**: Percentage (`0` - `100`)
- **Default Value**: `50`
- **Description**: Chance that an otherwise selected NPC neutral toward the current conversational counterpart speaks. (Percentage; default 50.)
- **Operator Guidance & Impact**: Adjust this probability up for chattier, more responsive bots, or down to conserve LLM API tokens on high-population servers.

### <a id="azerothvoicesnpcreplychancehostile"></a>`AzerothVoices.NPC.ReplyChance.Hostile`

- **Data Type**: Percentage (`0` - `100`)
- **Default Value**: `25`
- **Description**: Chance that an otherwise selected NPC hostile or unfriendly toward the current conversational counterpart speaks. (Percentage; default 25.)
- **Operator Guidance & Impact**: Adjust this probability up for chattier, more responsive bots, or down to conserve LLM API tokens on high-population servers.

### <a id="azerothvoicesnpccombatstartenable"></a>`AzerothVoices.NPC.CombatStart.Enable`

- **Data Type**: Boolean (`0` or `1`)
- **Default Value**: `1`
- **Description**: Enables one generated Say reaction from the exact creature a real player attacks when that attack opens a fresh combat. Attacking another creature while the player remains in the same combat never creates another combat-start reaction. Existing NPC safety/type/faction gates and DisableRepliesInCombat still apply. (Default 1.)
- **Operator Guidance & Impact**: Keep enabled (`1`) for standard functionality. Set to `0` to completely bypass this feature and eliminate its processing overhead.

### <a id="azerothvoicesnpccombatstartchance"></a>`AzerothVoices.NPC.CombatStart.Chance`

- **Data Type**: Percentage (`0` - `100`)
- **Default Value**: `30`
- **Description**: Chance that the exact opening creature produces its one combat-start reaction. The roll happens once per fresh combat. (Percentage; default 30.)
- **Operator Guidance & Impact**: Adjust this probability up for chattier, more responsive bots, or down to conserve LLM API tokens on high-population servers.

### <a id="azerothvoicesnpccombatstartcooldownseconds"></a>`AzerothVoices.NPC.CombatStart.CooldownSeconds`

- **Data Type**: Integer (Seconds, minimum 0; default 60.)
- **Default Value**: `60`
- **Description**: Minimum time between accepted combat-start creature reactions for the same real player; this is separate from the one-trigger-per-combat latch. (Seconds, minimum 0; default 60.)
- **Operator Guidance & Impact**: Controls rate-limiting. Increasing this value prevents repetitive triggers and reduces API call frequency; setting to `0` removes time-based gating for this specific event.

### <a id="azerothvoicesnpctargetedconversationbotcommentsenable"></a>`AzerothVoices.NPC.TargetedConversation.BotComments.Enable`

- **Data Type**: Boolean (`0` or `1`)
- **Default Value**: `1`
- **Description**: Enables observer comments from nearby PlayerBots when a real player speaks to a targeted NPC. (Default 1.)
- **Operator Guidance & Impact**: Keep enabled (`1`) for standard functionality. Set to `0` to completely bypass this feature and eliminate its processing overhead.

### <a id="azerothvoicesnpctargetedconversationbotcommentchance"></a>`AzerothVoices.NPC.TargetedConversation.BotCommentChance`

- **Data Type**: Percentage (`0` - `100`)
- **Default Value**: `15`
- **Description**: Chance that nearby PlayerBots generate an observer comment when a real player speaks to a targeted NPC. (Percentage; default 15.)
- **Operator Guidance & Impact**: Adjust this probability up for chattier, more responsive bots, or down to conserve LLM API tokens on high-population servers.

### <a id="azerothvoicesnpctargetedconversationmaxbotcomments"></a>`AzerothVoices.NPC.TargetedConversation.MaxBotComments`

- **Data Type**: Integer
- **Default Value**: `1`
- **Description**: Maximum number of PlayerBots allowed to make an observer comment per player-to-NPC interaction. (Integer 0-10; default 1.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicesnpcmentionsuppressesdirectbotreply"></a>`AzerothVoices.NPC.MentionSuppressesDirectBotReply`

- **Data Type**: Boolean (`0` or `1`)
- **Default Value**: `1`
- **Description**: Suppresses direct PlayerBot replies in /say when a real player mentions or speaks to an NPC. This prevents PlayerBots from answering questions intended for NPCs or impersonating vendors/questgivers. (Default 1.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicesnpcmentionbotdirectreplychance"></a>`AzerothVoices.NPC.MentionBotDirectReplyChance`

- **Data Type**: Percentage (`0` - `100`)
- **Default Value**: `0`
- **Description**: If direct PlayerBot replies are not completely suppressed when speaking to an NPC, this sets the chance (0-100%) that a bot is permitted to reply directly as addressee. (Percentage; default 0.)
- **Operator Guidance & Impact**: Set to `0` to disable this trigger entirely, or `100` for deterministic triggering every time the event conditions are satisfied.

### <a id="azerothvoicesnpcreplyobserverenable"></a>`AzerothVoices.NPC.ReplyObserver.Enable`

- **Data Type**: Boolean (`0` or `1`)
- **Default Value**: `1`
- **Description**: Enables observer comments from nearby PlayerBots when an NPC delivers a reply to a real player. Allows nearby bots to react in-character to what the NPC said. (Default 1.)
- **Operator Guidance & Impact**: Keep enabled (`1`) for standard functionality. Set to `0` to completely bypass this feature and eliminate its processing overhead.

### <a id="azerothvoicesnpcreplyobserverchance"></a>`AzerothVoices.NPC.ReplyObserver.Chance`

- **Data Type**: Percentage (`0` - `100`)
- **Default Value**: `25`
- **Description**: Chance that nearby PlayerBots generate an observer reaction to the NPC's answer. (Percentage; default 25.)
- **Operator Guidance & Impact**: Adjust this probability up for chattier, more responsive bots, or down to conserve LLM API tokens on high-population servers.

### <a id="azerothvoicesnpcreplyobservercooldownseconds"></a>`AzerothVoices.NPC.ReplyObserver.CooldownSeconds`

- **Data Type**: Integer (Seconds, 0-86400; default 30.)
- **Default Value**: `30`
- **Description**: Cooldown between bot observer comments reacting to NPC interactions. (Seconds, 0-86400; default 30.)
- **Operator Guidance & Impact**: Controls rate-limiting. Increasing this value prevents repetitive triggers and reduces API call frequency; setting to `0` removes time-based gating for this specific event.

### <a id="azerothvoicestargetedbotobserverenable"></a>`AzerothVoices.TargetedBot.Observer.Enable`

- **Data Type**: Boolean (`0` or `1`)
- **Default Value**: `1`
- **Description**: Enables observer commentary from companion PlayerBots when a real player speaks to or interacts with a targeted companion PlayerBot. Allows other party members to chime in or react. (Default 1.)
- **Operator Guidance & Impact**: Keep enabled (`1`) for lively party interactions where bots react to each other and player conversations. Set to `0` to disable companion bot side-commentary.

### <a id="azerothvoicestargetedbotcommentonplayerchance"></a>`AzerothVoices.TargetedBot.CommentOnPlayerChance`

- **Data Type**: Percentage (`0` - `100`)
- **Default Value**: `70`
- **Description**: Chance that an untargeted companion PlayerBot comments on what the real player said to the targeted bot. (Percentage, 0-100; default 70.)
- **Operator Guidance & Impact**: Sets the probability for an untargeted party bot to chime in immediately on the player's message directed at another bot. Adjust downwards to keep discussions strictly one-on-one.

### <a id="azerothvoicestargetedbotcommentonreplychance"></a>`AzerothVoices.TargetedBot.CommentOnReplyChance`

- **Data Type**: Percentage (`0` - `100`)
- **Default Value**: `30`
- **Description**: Chance that an untargeted companion PlayerBot reacts to the reply delivered by the targeted bot. (Percentage, 0-100; default 30.)
- **Operator Guidance & Impact**: Sets the probability for an untargeted bot to react after the targeted bot has finished answering the player. Creates natural multi-turn bot banter.

### <a id="azerothvoicestargetedbotobservercooldownseconds"></a>`AzerothVoices.TargetedBot.ObserverCooldownSeconds`

- **Data Type**: Integer (Seconds, 0-86400; default 30.)
- **Default Value**: `30`
- **Description**: Cooldown between companion bot observer comments reacting to targeted bot interactions. (Seconds, 0-86400; default 30.)
- **Operator Guidance & Impact**: Prevents companion bots from spamming commentary on every single exchange. Setting to `0` removes the per-bot observer cooldown.

---

## 9. Dedicated proximity chatter (ordinary Say)

| Setting Name | Type / Range | Default | Summary |
| :--- | :--- | :--- | :--- |
| [`AzerothVoices.Proximity.Enable`](#azerothvoicesproximityenable) | Boolean (`0` or `1`) | `1` | Enables the proximity subsystem, which owns ordinary nearby NPC Say chatter... |
| [`AzerothVoices.Proximity.ScanSecondsOutdoor`](#azerothvoicesproximityscansecondsoutdoor) | Integer (Seconds, 5-3600; default 30.) | `30` | Delay between outdoor proximity scans. (Seconds, 5-3600; default 30.) |
| [`AzerothVoices.Proximity.ScanSecondsInstance`](#azerothvoicesproximityscansecondsinstance) | Integer (Seconds, 5-3600; default 30.) | `30` | Delay between dungeon/raid proximity scans. (Seconds, 5-3600; default 30.) |
| [`AzerothVoices.Proximity.OutdoorChance`](#azerothvoicesproximityoutdoorchance) | Percentage (`0` - `100`) | `15` | Trigger chance per outdoor scan, before zone fatigue. (Percentage; default ... |
| [`AzerothVoices.Proximity.InstanceChance`](#azerothvoicesproximityinstancechance) | Percentage (`0` - `100`) | `75` | Trigger chance per dungeon/raid scan, before zone fatigue. (Percentage; def... |
| [`AzerothVoices.Proximity.IncludeInstances`](#azerothvoicesproximityincludeinstances) | Boolean (`0` or `1`) | `1` | Includes dungeons and raids; they are never required for outdoor chatter. (... |
| [`AzerothVoices.Proximity.EntityCooldownSeconds`](#azerothvoicesproximityentitycooldownseconds) | Integer (Seconds, 0-86400; default 60.) | `60` | Per-speaker cooldown after a delivered proximity line. (Seconds, 0-86400; d... |
| [`AzerothVoices.Proximity.ZoneFatigueScenes`](#azerothvoicesproximityzonefatiguescenes) | Integer | `3` | Scenes a zone may produce before its trigger chance starts decaying. (Scene... |
| [`AzerothVoices.Proximity.ZoneFatigueDecayPercent`](#azerothvoicesproximityzonefatiguedecaypercent) | Percentage (`0` - `100`) | `20` | Decay applied per scene beyond ZoneFatigueScenes and per scene-free scan, f... |
| [`AzerothVoices.Proximity.MinimumSpeakers`](#azerothvoicesproximityminimumspeakers) | Integer | `1` | Minimum speakers selected per scene. (Speakers, 1-8; default 1.) |
| [`AzerothVoices.Proximity.MaximumSpeakers`](#azerothvoicesproximitymaximumspeakers) | Integer | `4` | Maximum speakers selected per scene; raised to MinimumSpeakers when set low... |
| [`AzerothVoices.Proximity.ConversationChance`](#azerothvoicesproximityconversationchance) | Percentage (`0` - `100`) | `40` | Chance a scene becomes a multi-line conversation instead of one line. (Perc... |
| [`AzerothVoices.Proximity.MaximumLines`](#azerothvoicesproximitymaximumlines) | Integer | `4` | Lines a single scene may deliver. (Lines, 1-12; default 4.) |
| [`AzerothVoices.Proximity.TurnGapSeconds`](#azerothvoicesproximityturngapseconds) | Integer (Seconds, 0-600; default 6.) | `6` | Minimum gap between delivered lines of one conversation. (Seconds, 0-600; d... |
| [`AzerothVoices.Proximity.ReplyWindowSeconds`](#azerothvoicesproximityreplywindowseconds) | Integer (Seconds, 1-600; default 30.) | `30` | Window in which a conversation may continue after its first line. (Seconds,... |
| [`AzerothVoices.Proximity.MaximumReplyTurns`](#azerothvoicesproximitymaximumreplyturns) | Integer | `5` | Generated reply turns allowed inside one scene. (Turns, 1-20; default 5.) |
| [`AzerothVoices.Proximity.Speaker.AllowEntries`](#azerothvoicesproximityspeakerallowentries) | String (Comma-separated list) | *(Empty)* | Creature entries allowed to speak in proximity scenes; empty means no entry... |
| [`AzerothVoices.Proximity.Speaker.DenyEntries`](#azerothvoicesproximityspeakerdenyentries) | String (Comma-separated list) | *(Empty)* | Creature entries that may never speak in proximity scenes, whatever else qu... |
| [`AzerothVoices.Proximity.Speaker.AllowNonHumanoidEntries`](#azerothvoicesproximityspeakerallownonhumanoidentries) | String (Comma-separated list) | *(Empty)* | Non-humanoid creature entries explicitly allowed to speak; humanoids never ... |


### <a id="azerothvoicesproximityenable"></a>`AzerothVoices.Proximity.Enable`

- **Data Type**: Boolean (`0` or `1`)
- **Default Value**: `1`
- **Description**: Enables the proximity subsystem, which owns ordinary nearby NPC Say chatter. Battlegrounds and arenas are always excluded. (Default 1.)
- **Operator Guidance & Impact**: Keep enabled (`1`) for standard functionality. Set to `0` to completely bypass this feature and eliminate its processing overhead.

### <a id="azerothvoicesproximityscansecondsoutdoor"></a>`AzerothVoices.Proximity.ScanSecondsOutdoor`

- **Data Type**: Integer (Seconds, 5-3600; default 30.)
- **Default Value**: `30`
- **Description**: Delay between outdoor proximity scans. (Seconds, 5-3600; default 30.)
- **Operator Guidance & Impact**: Controls background evaluation frequency. Shorter intervals increase responsiveness at the cost of more frequent world-thread scans; longer intervals conserve server CPU.

### <a id="azerothvoicesproximityscansecondsinstance"></a>`AzerothVoices.Proximity.ScanSecondsInstance`

- **Data Type**: Integer (Seconds, 5-3600; default 30.)
- **Default Value**: `30`
- **Description**: Delay between dungeon/raid proximity scans. (Seconds, 5-3600; default 30.)
- **Operator Guidance & Impact**: Controls background evaluation frequency. Shorter intervals increase responsiveness at the cost of more frequent world-thread scans; longer intervals conserve server CPU.

### <a id="azerothvoicesproximityoutdoorchance"></a>`AzerothVoices.Proximity.OutdoorChance`

- **Data Type**: Percentage (`0` - `100`)
- **Default Value**: `15`
- **Description**: Trigger chance per outdoor scan, before zone fatigue. (Percentage; default 15.)
- **Operator Guidance & Impact**: Adjust this probability up for chattier, more responsive bots, or down to conserve LLM API tokens on high-population servers.

### <a id="azerothvoicesproximityinstancechance"></a>`AzerothVoices.Proximity.InstanceChance`

- **Data Type**: Percentage (`0` - `100`)
- **Default Value**: `75`
- **Description**: Trigger chance per dungeon/raid scan, before zone fatigue. (Percentage; default 75.)
- **Operator Guidance & Impact**: Adjust this probability up for chattier, more responsive bots, or down to conserve LLM API tokens on high-population servers.

### <a id="azerothvoicesproximityincludeinstances"></a>`AzerothVoices.Proximity.IncludeInstances`

- **Data Type**: Boolean (`0` or `1`)
- **Default Value**: `1`
- **Description**: Includes dungeons and raids; they are never required for outdoor chatter. (Default 1.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicesproximityentitycooldownseconds"></a>`AzerothVoices.Proximity.EntityCooldownSeconds`

- **Data Type**: Integer (Seconds, 0-86400; default 60.)
- **Default Value**: `60`
- **Description**: Per-speaker cooldown after a delivered proximity line. (Seconds, 0-86400; default 60.)
- **Operator Guidance & Impact**: Controls rate-limiting. Increasing this value prevents repetitive triggers and reduces API call frequency; setting to `0` removes time-based gating for this specific event.

### <a id="azerothvoicesproximityzonefatiguescenes"></a>`AzerothVoices.Proximity.ZoneFatigueScenes`

- **Data Type**: Integer
- **Default Value**: `3`
- **Description**: Scenes a zone may produce before its trigger chance starts decaying. (Scenes, 1-100; default 3.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicesproximityzonefatiguedecaypercent"></a>`AzerothVoices.Proximity.ZoneFatigueDecayPercent`

- **Data Type**: Percentage (`0` - `100`)
- **Default Value**: `20`
- **Description**: Decay applied per scene beyond ZoneFatigueScenes and per scene-free scan, floored at 1 percent. (Percentage; default 20.)
- **Operator Guidance & Impact**: Adjust this probability up for chattier, more responsive bots, or down to conserve LLM API tokens on high-population servers.

### <a id="azerothvoicesproximityminimumspeakers"></a>`AzerothVoices.Proximity.MinimumSpeakers`

- **Data Type**: Integer
- **Default Value**: `1`
- **Description**: Minimum speakers selected per scene. (Speakers, 1-8; default 1.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicesproximitymaximumspeakers"></a>`AzerothVoices.Proximity.MaximumSpeakers`

- **Data Type**: Integer
- **Default Value**: `4`
- **Description**: Maximum speakers selected per scene; raised to MinimumSpeakers when set lower. (Speakers, 1-8; default 4.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicesproximityconversationchance"></a>`AzerothVoices.Proximity.ConversationChance`

- **Data Type**: Percentage (`0` - `100`)
- **Default Value**: `40`
- **Description**: Chance a scene becomes a multi-line conversation instead of one line. (Percentage; default 40.)
- **Operator Guidance & Impact**: Adjust this probability up for chattier, more responsive bots, or down to conserve LLM API tokens on high-population servers.

### <a id="azerothvoicesproximitymaximumlines"></a>`AzerothVoices.Proximity.MaximumLines`

- **Data Type**: Integer
- **Default Value**: `4`
- **Description**: Lines a single scene may deliver. (Lines, 1-12; default 4.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicesproximityturngapseconds"></a>`AzerothVoices.Proximity.TurnGapSeconds`

- **Data Type**: Integer (Seconds, 0-600; default 6.)
- **Default Value**: `6`
- **Description**: Minimum gap between delivered lines of one conversation. (Seconds, 0-600; default 6.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicesproximityreplywindowseconds"></a>`AzerothVoices.Proximity.ReplyWindowSeconds`

- **Data Type**: Integer (Seconds, 1-600; default 30.)
- **Default Value**: `30`
- **Description**: Window in which a conversation may continue after its first line. (Seconds, 1-600; default 30.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicesproximitymaximumreplyturns"></a>`AzerothVoices.Proximity.MaximumReplyTurns`

- **Data Type**: Integer
- **Default Value**: `5`
- **Description**: Generated reply turns allowed inside one scene. (Turns, 1-20; default 5.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicesproximityspeakerallowentries"></a>`AzerothVoices.Proximity.Speaker.AllowEntries`

- **Data Type**: String (Comma-separated list)
- **Default Value**: *(Empty)*
- **Description**: Creature entries allowed to speak in proximity scenes; empty means no entry restriction. The denylist below always wins. (Comma-separated creature entries; default empty.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicesproximityspeakerdenyentries"></a>`AzerothVoices.Proximity.Speaker.DenyEntries`

- **Data Type**: String (Comma-separated list)
- **Default Value**: *(Empty)*
- **Description**: Creature entries that may never speak in proximity scenes, whatever else qualifies them. (Comma-separated creature entries; default empty.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicesproximityspeakerallownonhumanoidentries"></a>`AzerothVoices.Proximity.Speaker.AllowNonHumanoidEntries`

- **Data Type**: String (Comma-separated list)
- **Default Value**: *(Empty)*
- **Description**: Non-humanoid creature entries explicitly allowed to speak; humanoids never need to be listed. (Comma-separated creature entries; default empty.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

---

## 10. Boss and miniboss pre-aggro dialogue (dungeons and raids only)

| Setting Name | Type / Range | Default | Summary |
| :--- | :--- | :--- | :--- |
| [`AzerothVoices.BossDialogue.Enable`](#azerothvoicesbossdialogueenable) | Boolean (`0` or `1`) | `1` | Enables pre-aggro boss dialogue. Bosses are classified from the built-in cu... |
| [`AzerothVoices.BossDialogue.ScanSeconds`](#azerothvoicesbossdialoguescanseconds) | Integer (Seconds, 1-300; default 2.) | `2` | Delay between boss-presence scans. (Seconds, 1-300; default 2.) |
| [`AzerothVoices.BossDialogue.MaximumDistance`](#azerothvoicesbossdialoguemaximumdistance) | Decimal Yards (Yards, 5-200; default 80.) | `80` | Maximum distance at which a boss may notice the approaching player. (Yards,... |
| [`AzerothVoices.BossDialogue.AggroMargin`](#azerothvoicesbossdialogueaggromargin) | Decimal Yards (Yards, 0-50; default 5.) | `5` | Extra safety margin added to the boss's own attack distance; a boss never s... |
| [`AzerothVoices.BossDialogue.InitialDelayMinimumSeconds`](#azerothvoicesbossdialogueinitialdelayminimumseconds) | Integer (Seconds, 0-600; default 2.) | `2` | Lower bound of the initial delay after a boss presence is first observed. (... |
| [`AzerothVoices.BossDialogue.InitialDelayMaximumSeconds`](#azerothvoicesbossdialogueinitialdelaymaximumseconds) | Integer (Seconds, 0-600; default 6.) | `6` | Upper bound of the initial delay; raised to the minimum when set lower. (Se... |
| [`AzerothVoices.BossDialogue.RepeatDelayMinimumSeconds`](#azerothvoicesbossdialoguerepeatdelayminimumseconds) | Integer (Seconds, 1-3600; default 20.) | `20` | Lower bound of the delay before the next automatic line. (Seconds, 1-3600; ... |
| [`AzerothVoices.BossDialogue.RepeatDelayMaximumSeconds`](#azerothvoicesbossdialoguerepeatdelaymaximumseconds) | Integer (Seconds, 1-3600; default 60.) | `60` | Upper bound of that delay; raised to the minimum when set lower. (Seconds, ... |
| [`AzerothVoices.BossDialogue.RepeatChance`](#azerothvoicesbossdialoguerepeatchance) | Percentage (`0` - `100`) | `80` | Chance an automatic opportunity produces a line. (Percentage; default 80.) |
| [`AzerothVoices.BossDialogue.RepeatChanceDecayPercent`](#azerothvoicesbossdialoguerepeatchancedecaypercent) | Percentage (`0` - `100`) | `50` | Decay applied to the repeat chance after each delivered automatic line. (Pe... |
| [`AzerothVoices.BossDialogue.RepeatChanceFloor`](#azerothvoicesbossdialoguerepeatchancefloor) | Percentage (`0` - `100`) | `10` | Lowest repeat chance an ongoing presence decays to. (Percentage; default 10... |
| [`AzerothVoices.BossDialogue.PresenceResetSeconds`](#azerothvoicesbossdialoguepresenceresetseconds) | Integer (Seconds, 5-3600; default 90.) | `90` | Time without an observed eligible player after which a boss presence resets... |
| [`AzerothVoices.BossDialogue.DirectedReplyCooldownSeconds`](#azerothvoicesbossdialoguedirectedreplycooldownseconds) | Integer (Seconds, 0-3600; default 15.) | `15` | Cooldown before the same boss answers another directed player line. (Second... |
| [`AzerothVoices.BossDialogue.MinimumWords`](#azerothvoicesbossdialogueminimumwords) | Integer (Words, 1-100; default 5.) | `5` | Lower word bound for an automatically generated boss line. (Words, 1-100; d... |
| [`AzerothVoices.BossDialogue.MaximumWords`](#azerothvoicesbossdialoguemaximumwords) | Integer (Words, 1-200; default 22.) | `22` | Upper word bound; a longer line is cut back to a word boundary and then rej... |
| [`AzerothVoices.BossDialogue.MaximumCharacters`](#azerothvoicesbossdialoguemaximumcharacters) | Integer (Characters, 20-1000; default 180.) | `180` | Hard character cap for one boss yell. (Characters, 20-1000; default 180.) |
| [`AzerothVoices.BossDialogue.AllowEntries`](#azerothvoicesbossdialogueallowentries) | String (Comma-separated list) | *(Empty)* | Extra creature entries treated as bosses; empty means registry and fallback... |
| [`AzerothVoices.BossDialogue.DenyEntries`](#azerothvoicesbossdialoguedenyentries) | String (Comma-separated list) | *(Empty)* | Creature entries never treated as bosses; the denylist wins over registry, ... |


### <a id="azerothvoicesbossdialogueenable"></a>`AzerothVoices.BossDialogue.Enable`

- **Data Type**: Boolean (`0` or `1`)
- **Default Value**: `1`
- **Description**: Enables pre-aggro boss dialogue. Bosses are classified from the built-in curated registry plus the world-boss rank and instance-bind fallbacks, and are excluded from ordinary proximity, event, and combat-start NPC generation so boss speech has exactly one owner. (Default 1.)
- **Operator Guidance & Impact**: Keep enabled (`1`) for standard functionality. Set to `0` to completely bypass this feature and eliminate its processing overhead.

### <a id="azerothvoicesbossdialoguescanseconds"></a>`AzerothVoices.BossDialogue.ScanSeconds`

- **Data Type**: Integer (Seconds, 1-300; default 2.)
- **Default Value**: `2`
- **Description**: Delay between boss-presence scans. (Seconds, 1-300; default 2.)
- **Operator Guidance & Impact**: Controls background evaluation frequency. Shorter intervals increase responsiveness at the cost of more frequent world-thread scans; longer intervals conserve server CPU.

### <a id="azerothvoicesbossdialoguemaximumdistance"></a>`AzerothVoices.BossDialogue.MaximumDistance`

- **Data Type**: Decimal Yards (Yards, 5-200; default 80.)
- **Default Value**: `80`
- **Description**: Maximum distance at which a boss may notice the approaching player. (Yards, 5-200; default 80.)
- **Operator Guidance & Impact**: Defines the spatial radius for candidate discovery and line-of-sight checks. Keep aligned with visible WoW client chat bubbles.

### <a id="azerothvoicesbossdialogueaggromargin"></a>`AzerothVoices.BossDialogue.AggroMargin`

- **Data Type**: Decimal Yards (Yards, 0-50; default 5.)
- **Default Value**: `5`
- **Description**: Extra safety margin added to the boss's own attack distance; a boss never speaks inside this radius. Nothing here alters threat, aggro, facing, or movement. (Yards, 0-50; default 5.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicesbossdialogueinitialdelayminimumseconds"></a>`AzerothVoices.BossDialogue.InitialDelayMinimumSeconds`

- **Data Type**: Integer (Seconds, 0-600; default 2.)
- **Default Value**: `2`
- **Description**: Lower bound of the initial delay after a boss presence is first observed. (Seconds, 0-600; default 2.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicesbossdialogueinitialdelaymaximumseconds"></a>`AzerothVoices.BossDialogue.InitialDelayMaximumSeconds`

- **Data Type**: Integer (Seconds, 0-600; default 6.)
- **Default Value**: `6`
- **Description**: Upper bound of the initial delay; raised to the minimum when set lower. (Seconds, 0-600; default 6.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicesbossdialoguerepeatdelayminimumseconds"></a>`AzerothVoices.BossDialogue.RepeatDelayMinimumSeconds`

- **Data Type**: Integer (Seconds, 1-3600; default 20.)
- **Default Value**: `20`
- **Description**: Lower bound of the delay before the next automatic line. (Seconds, 1-3600; default 20.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicesbossdialoguerepeatdelaymaximumseconds"></a>`AzerothVoices.BossDialogue.RepeatDelayMaximumSeconds`

- **Data Type**: Integer (Seconds, 1-3600; default 60.)
- **Default Value**: `60`
- **Description**: Upper bound of that delay; raised to the minimum when set lower. (Seconds, 1-3600; default 60.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicesbossdialoguerepeatchance"></a>`AzerothVoices.BossDialogue.RepeatChance`

- **Data Type**: Percentage (`0` - `100`)
- **Default Value**: `80`
- **Description**: Chance an automatic opportunity produces a line. (Percentage; default 80.)
- **Operator Guidance & Impact**: Adjust this probability up for chattier, more responsive bots, or down to conserve LLM API tokens on high-population servers.

### <a id="azerothvoicesbossdialoguerepeatchancedecaypercent"></a>`AzerothVoices.BossDialogue.RepeatChanceDecayPercent`

- **Data Type**: Percentage (`0` - `100`)
- **Default Value**: `50`
- **Description**: Decay applied to the repeat chance after each delivered automatic line. (Percentage; default 50.)
- **Operator Guidance & Impact**: Adjust this probability up for chattier, more responsive bots, or down to conserve LLM API tokens on high-population servers.

### <a id="azerothvoicesbossdialoguerepeatchancefloor"></a>`AzerothVoices.BossDialogue.RepeatChanceFloor`

- **Data Type**: Percentage (`0` - `100`)
- **Default Value**: `10`
- **Description**: Lowest repeat chance an ongoing presence decays to. (Percentage; default 10.)
- **Operator Guidance & Impact**: Adjust this probability up for chattier, more responsive bots, or down to conserve LLM API tokens on high-population servers.

### <a id="azerothvoicesbossdialoguepresenceresetseconds"></a>`AzerothVoices.BossDialogue.PresenceResetSeconds`

- **Data Type**: Integer (Seconds, 5-3600; default 90.)
- **Default Value**: `90`
- **Description**: Time without an observed eligible player after which a boss presence resets. (Seconds, 5-3600; default 90.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicesbossdialoguedirectedreplycooldownseconds"></a>`AzerothVoices.BossDialogue.DirectedReplyCooldownSeconds`

- **Data Type**: Integer (Seconds, 0-3600; default 15.)
- **Default Value**: `15`
- **Description**: Cooldown before the same boss answers another directed player line. (Seconds, 0-3600; default 15.)
- **Operator Guidance & Impact**: Controls rate-limiting. Increasing this value prevents repetitive triggers and reduces API call frequency; setting to `0` removes time-based gating for this specific event.

### <a id="azerothvoicesbossdialogueminimumwords"></a>`AzerothVoices.BossDialogue.MinimumWords`

- **Data Type**: Integer (Words, 1-100; default 5.)
- **Default Value**: `5`
- **Description**: Lower word bound for an automatically generated boss line. (Words, 1-100; default 5.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicesbossdialoguemaximumwords"></a>`AzerothVoices.BossDialogue.MaximumWords`

- **Data Type**: Integer (Words, 1-200; default 22.)
- **Default Value**: `22`
- **Description**: Upper word bound; a longer line is cut back to a word boundary and then rejected if it still exceeds the bound. Raised to MinimumWords when set lower. (Words, 1-200; default 22.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicesbossdialoguemaximumcharacters"></a>`AzerothVoices.BossDialogue.MaximumCharacters`

- **Data Type**: Integer (Characters, 20-1000; default 180.)
- **Default Value**: `180`
- **Description**: Hard character cap for one boss yell. (Characters, 20-1000; default 180.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicesbossdialogueallowentries"></a>`AzerothVoices.BossDialogue.AllowEntries`

- **Data Type**: String (Comma-separated list)
- **Default Value**: *(Empty)*
- **Description**: Extra creature entries treated as bosses; empty means registry and fallbacks only. (Comma-separated creature entries; default empty.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicesbossdialoguedenyentries"></a>`AzerothVoices.BossDialogue.DenyEntries`

- **Data Type**: String (Comma-separated list)
- **Default Value**: *(Empty)*
- **Description**: Creature entries never treated as bosses; the denylist wins over registry, allow-list, and both fallbacks. (Comma-separated creature entries; default empty.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

---

## 11. Party and raid chatter

> Group reactions run on top of the existing reply system: they need a real
player in the same party subgroup (or raid), obey the normal audience,
cooldown, queue and rate limits, and are exempt from DisableRepliesInCombat
because in-combat callouts are the point. Every trigger ships enabled with the
conservative defaults below; raise only the ones you want.


| Setting Name | Type / Range | Default | Summary |
| :--- | :--- | :--- | :--- |
| [`AzerothVoices.GroupChatter.Enable`](#azerothvoicesgroupchatterenable) | Boolean (`0` or `1`) | `1` | Enables party chatter: idle, dungeon entry, zone change, kills, boss moment... |
| [`AzerothVoices.GroupChatter.ScanSeconds`](#azerothvoicesgroupchatterscanseconds) | Integer (Seconds, 5-3600; default 120.) | `120` | Delay between group scans that evaluate the periodic triggers. (Seconds, 5-... |
| [`AzerothVoices.GroupChatter.IdleChance`](#azerothvoicesgroupchatteridlechance) | Percentage (`0` - `100`) | `2` | Chance a scan produces an ordinary casual group remark. (Percentage; defaul... |
| [`AzerothVoices.GroupChatter.IdleCooldownSeconds`](#azerothvoicesgroupchatteridlecooldownseconds) | Integer (Seconds, 0-86400; default 300.) | `300` | Cooldown after an idle remark. (Seconds, 0-86400; default 300.) |
| [`AzerothVoices.GroupChatter.DungeonEntryChance`](#azerothvoicesgroupchatterdungeonentrychance) | Percentage (`0` - `100`) | `60` | Chance the group comments when a real player enters a dungeon or raid. (Per... |
| [`AzerothVoices.GroupChatter.DungeonEntryCooldownSeconds`](#azerothvoicesgroupchatterdungeonentrycooldownseconds) | Integer (Seconds, 0-86400; default 300.) | `300` | Cooldown after a dungeon-entry line. (Seconds, 0-86400; default 300.) |
| [`AzerothVoices.GroupChatter.ZoneChangeChance`](#azerothvoicesgroupchatterzonechangechance) | Percentage (`0` - `100`) | `15` | Chance the group comments on an outdoor zone change. (Percentage; default 1... |
| [`AzerothVoices.GroupChatter.ZoneChangeCooldownSeconds`](#azerothvoicesgroupchatterzonechangecooldownseconds) | Integer (Seconds, 0-86400; default 180.) | `180` | Cooldown after a zone-change line. (Seconds, 0-86400; default 180.) |
| [`AzerothVoices.GroupChatter.KillChance`](#azerothvoicesgroupchatterkillchance) | Percentage (`0` - `100`) | `4` | Chance the group reacts to an ordinary creature kill. (Percentage; default ... |
| [`AzerothVoices.GroupChatter.KillCooldownSeconds`](#azerothvoicesgroupchatterkillcooldownseconds) | Integer (Seconds, 0-86400; default 300.) | `300` | Cooldown after a trash-kill line. (Seconds, 0-86400; default 300.) |
| [`AzerothVoices.GroupChatter.BossPullChance`](#azerothvoicesgroupchatterbosspullchance) | Percentage (`0` - `100`) | `50` | Chance the group reacts when a classified dungeon/raid boss is engaged. (Pe... |
| [`AzerothVoices.GroupChatter.BossPullCooldownSeconds`](#azerothvoicesgroupchatterbosspullcooldownseconds) | Integer (Seconds, 0-86400; default 300.) | `300` | Cooldown after a boss-pull line. (Seconds, 0-86400; default 300.) |
| [`AzerothVoices.GroupChatter.BossKillChance`](#azerothvoicesgroupchatterbosskillchance) | Percentage (`0` - `100`) | `80` | Chance the group reacts to a classified boss kill. (Percentage; default 80.... |
| [`AzerothVoices.GroupChatter.BossKillCooldownSeconds`](#azerothvoicesgroupchatterbosskillcooldownseconds) | Integer (Seconds, 0-86400; default 300.) | `300` | Cooldown after a boss-kill line. (Seconds, 0-86400; default 300.) |
| [`AzerothVoices.GroupChatter.WipeChance`](#azerothvoicesgroupchatterwipechance) | Percentage (`0` - `100`) | `80` | Chance the group comments on a wipe, detected once per wipe when every memb... |
| [`AzerothVoices.GroupChatter.WipeCooldownSeconds`](#azerothvoicesgroupchatterwipecooldownseconds) | Integer (Seconds, 0-86400; default 300.) | `300` | Cooldown after a wipe line. (Seconds, 0-86400; default 300.) |
| [`AzerothVoices.GroupChatter.DeathChance`](#azerothvoicesgroupchatterdeathchance) | Percentage (`0` - `100`) | `25` | Chance the group reacts to a member's death. (Percentage; default 25.) |
| [`AzerothVoices.GroupChatter.DeathCooldownSeconds`](#azerothvoicesgroupchatterdeathcooldownseconds) | Integer (Seconds, 0-86400; default 60.) | `60` | Cooldown after a death line. (Seconds, 0-86400; default 60.) |
| [`AzerothVoices.GroupChatter.CorpseRunChance`](#azerothvoicesgroupchattercorpserunchance) | Percentage (`0` - `100`) | `40` | Chance the group comments when a member releases their spirit. (Percentage;... |
| [`AzerothVoices.GroupChatter.CorpseRunCooldownSeconds`](#azerothvoicesgroupchattercorpseruncooldownseconds) | Integer (Seconds, 0-86400; default 300.) | `300` | Cooldown after a corpse-run line. (Seconds, 0-86400; default 300.) |
| [`AzerothVoices.GroupChatter.ResurrectChance`](#azerothvoicesgroupchatterresurrectchance) | Percentage (`0` - `100`) | `50` | Chance the group welcomes a member back after an observed resurrection. (Pe... |
| [`AzerothVoices.GroupChatter.ResurrectCooldownSeconds`](#azerothvoicesgroupchatterresurrectcooldownseconds) | Integer (Seconds, 0-86400; default 120.) | `120` | Cooldown after a resurrect line. (Seconds, 0-86400; default 120.) |
| [`AzerothVoices.GroupChatter.LootChanceGreen`](#azerothvoicesgroupchatterlootchancegreen) | Percentage (`0` - `100`) | `10` | Loot chatter chance by item quality; poor and common loot never triggers. (... |
| [`AzerothVoices.GroupChatter.LootChanceBlue`](#azerothvoicesgroupchatterlootchanceblue) | Percentage (`0` - `100`) | `35` | Rare (blue) loot chatter chance. (Percentage; default 35.) |
| [`AzerothVoices.GroupChatter.LootChancePurple`](#azerothvoicesgroupchatterlootchancepurple) | Percentage (`0` - `100`) | `70` | Epic (purple) loot chatter chance. (Percentage; default 70.) |
| [`AzerothVoices.GroupChatter.LootChanceOrange`](#azerothvoicesgroupchatterlootchanceorange) | Percentage (`0` - `100`) | `100` | Legendary (orange) loot chatter chance. (Percentage; default 100.) |
| [`AzerothVoices.GroupChatter.LootCooldownSeconds`](#azerothvoicesgroupchatterlootcooldownseconds) | Integer (Seconds, 0-86400; default 120.) | `120` | Cooldown after a loot line. (Seconds, 0-86400; default 120.) |
| [`AzerothVoices.GroupChatter.QuestAcceptChance`](#azerothvoicesgroupchatterquestacceptchance) | Percentage (`0` - `100`) | `25` | Chance the group comments when a member picks up a quest. (Percentage; defa... |
| [`AzerothVoices.GroupChatter.QuestObjectiveChance`](#azerothvoicesgroupchatterquestobjectivechance) | Percentage (`0` - `100`) | `20` | Chance the group comments when a quest objective advances. (Percentage; def... |
| [`AzerothVoices.GroupChatter.QuestCompleteChance`](#azerothvoicesgroupchatterquestcompletechance) | Percentage (`0` - `100`) | `25` | Chance the group comments when a member completes a quest. (Percentage; def... |
| [`AzerothVoices.GroupChatter.QuestCooldownSeconds`](#azerothvoicesgroupchatterquestcooldownseconds) | Integer (Seconds, 0-86400; default 60.) | `60` | Cooldown for quest accept/complete lines. (Seconds, 0-86400; default 60.) |
| [`AzerothVoices.GroupChatter.QuestObjectiveDebounceSeconds`](#azerothvoicesgroupchatterquestobjectivedebounceseconds) | Integer (Seconds, 0-3600; default 60.) | `60` | Minimum gap between objective-progress lines, which are otherwise the chatt... |
| [`AzerothVoices.GroupChatter.SpellCastChance`](#azerothvoicesgroupchatterspellcastchance) | Percentage (`0` - `100`) | `2` | Chance the group comments on a member's spell cast. This hook fires very of... |
| [`AzerothVoices.GroupChatter.SpellCastCooldownSeconds`](#azerothvoicesgroupchatterspellcastcooldownseconds) | Integer (Seconds, 0-86400; default 180.) | `180` | Cooldown after a spell-cast line. (Seconds, 0-86400; default 180.) |
| [`AzerothVoices.GroupChatter.LowHealthChance`](#azerothvoicesgroupchatterlowhealthchance) | Percentage (`0` - `100`) | `40` | Chance a member calls out low health, once per drop below the threshold. (P... |
| [`AzerothVoices.GroupChatter.LowHealthThresholdPercent`](#azerothvoicesgroupchatterlowhealththresholdpercent) | Percentage (`0` - `100`) | `35` | Health percentage that counts as low for the callout. (Percentage, 1-99; de... |
| [`AzerothVoices.GroupChatter.LowHealthCooldownSeconds`](#azerothvoicesgroupchatterlowhealthcooldownseconds) | Integer (Seconds, 0-86400; default 180.) | `180` | Cooldown after a low-health callout. (Seconds, 0-86400; default 180.) |
| [`AzerothVoices.GroupChatter.LowManaChance`](#azerothvoicesgroupchatterlowmanachance) | Percentage (`0` - `100`) | `30` | Chance a member calls out low mana, once per drop below the threshold. (Per... |
| [`AzerothVoices.GroupChatter.LowManaThresholdPercent`](#azerothvoicesgroupchatterlowmanathresholdpercent) | Percentage (`0` - `100`) | `20` | Mana percentage that counts as low for the callout. (Percentage, 1-99; defa... |
| [`AzerothVoices.GroupChatter.LowManaCooldownSeconds`](#azerothvoicesgroupchatterlowmanacooldownseconds) | Integer (Seconds, 0-86400; default 180.) | `180` | Cooldown after a low-mana callout. (Seconds, 0-86400; default 180.) |
| [`AzerothVoices.GroupChatter.NearbyObjectChance`](#azerothvoicesgroupchatternearbyobjectchance) | Percentage (`0` - `100`) | `3` | Chance the group comments on a nearby visible game object. (Percentage; def... |
| [`AzerothVoices.GroupChatter.NearbyObjectScanSeconds`](#azerothvoicesgroupchatternearbyobjectscanseconds) | Integer (Seconds, 5-3600; default 180.) | `180` | Delay between nearby-object checks. (Seconds, 5-3600; default 180.) |
| [`AzerothVoices.GroupChatter.NearbyObjectCooldownSeconds`](#azerothvoicesgroupchatternearbyobjectcooldownseconds) | Integer (Seconds, 0-86400; default 600.) | `600` | Cooldown after a nearby-object line. (Seconds, 0-86400; default 600.) |
| [`AzerothVoices.GroupChatter.BotQuestionChance`](#azerothvoicesgroupchatterbotquestionchance) | Percentage (`0` - `100`) | `1` | Chance a grouped bot asks the group a short practical question. (Percentage... |
| [`AzerothVoices.GroupChatter.BotQuestionScanSeconds`](#azerothvoicesgroupchatterbotquestionscanseconds) | Integer (Seconds, 5-3600; default 60.) | `60` | Delay between bot-question checks. (Seconds, 5-3600; default 60.) |
| [`AzerothVoices.GroupChatter.BotQuestionCooldownSeconds`](#azerothvoicesgroupchatterbotquestioncooldownseconds) | Integer (Seconds, 0-86400; default 300.) | `300` | Cooldown after a bot question. (Seconds, 0-86400; default 300.) |
| [`AzerothVoices.GroupChatter.PlayerFollowupChance`](#azerothvoicesgroupchatterplayerfollowupchance) | Percentage (`0` - `100`) | `5` | Chance one other grouped bot adds a second line after a bot's reply to a re... |
| [`AzerothVoices.GroupChatter.PlayerFollowupCooldownSeconds`](#azerothvoicesgroupchatterplayerfollowupcooldownseconds) | Integer (Seconds, 0-86400; default 120.) | `120` | Cooldown after such a second line. (Seconds, 0-86400; default 120.) |
| [`AzerothVoices.GroupChatter.ConversationChance`](#azerothvoicesgroupchatterconversationchance) | Percentage (`0` - `100`) | `10` | Chance a group line becomes a short multi-line conversation instead of one ... |
| [`AzerothVoices.GroupChatter.ConversationMaximumLines`](#azerothvoicesgroupchatterconversationmaximumlines) | Integer | `3` | Lines a group conversation may deliver. (Lines, 1-12; default 3.) |
| [`AzerothVoices.GroupChatter.ConversationMaximumParticipants`](#azerothvoicesgroupchatterconversationmaximumparticipants) | Integer | `2` | Participants a group conversation may use, capped by the number of grouped ... |
| [`AzerothVoices.GroupChatter.ConversationTurnGapSeconds`](#azerothvoicesgroupchatterconversationturngapseconds) | Integer (Seconds, 0-600; default 12.) | `12` | Minimum gap between delivered lines of one group conversation. (Seconds, 0-... |
| [`AzerothVoices.GroupChatter.ConversationReplyWindowSeconds`](#azerothvoicesgroupchatterconversationreplywindowseconds) | Integer (Seconds, 1-600; default 30.) | `30` | Window in which a group conversation may continue after its first line. (Se... |
| [`AzerothVoices.GroupChatter.ConversationMaximumReplyTurns`](#azerothvoicesgroupchatterconversationmaximumreplyturns) | Integer | `2` | Generated reply turns allowed inside one group conversation. (Turns, 1-20; ... |
| [`AzerothVoices.RaidChatter.Enable`](#azerothvoicesraidchatterenable) | Boolean (`0` or `1`) | `1` | Enables the raid-only extras below plus raid delivery of the shared boss mo... |
| [`AzerothVoices.RaidChatter.BattleCryChance`](#azerothvoicesraidchatterbattlecrychance) | Percentage (`0` - `100`) | `40` | Chance the raid shouts a battle cry when a classified boss is engaged. (Per... |
| [`AzerothVoices.RaidChatter.BattleCryCooldownSeconds`](#azerothvoicesraidchatterbattlecrycooldownseconds) | Integer (Seconds, 0-86400; default 300.) | `300` | Cooldown after a battle cry. (Seconds, 0-86400; default 300.) |
| [`AzerothVoices.RaidChatter.MoraleChance`](#azerothvoicesraidchattermoralechance) | Percentage (`0` - `100`) | `15` | Chance a raid line keeps morale up between pulls. (Percentage; default 15.) |
| [`AzerothVoices.RaidChatter.MoraleScanSeconds`](#azerothvoicesraidchattermoralescanseconds) | Integer (Seconds, 5-3600; default 120.) | `120` | Delay between morale checks. (Seconds, 5-3600; default 120.) |
| [`AzerothVoices.RaidChatter.MoraleCooldownSeconds`](#azerothvoicesraidchattermoralecooldownseconds) | Integer (Seconds, 0-86400; default 300.) | `300` | Cooldown after a morale line. (Seconds, 0-86400; default 300.) |
| [`AzerothVoices.RaidChatter.IdleChance`](#azerothvoicesraidchatteridlechance) | Percentage (`0` - `100`) | `10` | Chance a raid scan produces an ordinary casual remark, replacing the party ... |
| [`AzerothVoices.RaidChatter.IdleScanSeconds`](#azerothvoicesraidchatteridlescanseconds) | Integer (Seconds, 5-3600; default 60.) | `60` | Delay between raid idle checks. (Seconds, 5-3600; default 60.) |
| [`AzerothVoices.RaidChatter.IdleCooldownSeconds`](#azerothvoicesraidchatteridlecooldownseconds) | Integer (Seconds, 0-86400; default 180.) | `180` | Cooldown after a raid idle line. (Seconds, 0-86400; default 180.) |


### <a id="azerothvoicesgroupchatterenable"></a>`AzerothVoices.GroupChatter.Enable`

- **Data Type**: Boolean (`0` or `1`)
- **Default Value**: `1`
- **Description**: Enables party chatter: idle, dungeon entry, zone change, kills, boss moments, wipes, deaths, corpse runs, resurrects, loot, quests, spell casts, low health/mana, nearby objects, bot questions and player-message follow-ups. (Default 1.)
- **Operator Guidance & Impact**: Keep enabled (`1`) for standard functionality. Set to `0` to completely bypass this feature and eliminate its processing overhead.

### <a id="azerothvoicesgroupchatterscanseconds"></a>`AzerothVoices.GroupChatter.ScanSeconds`

- **Data Type**: Integer (Seconds, 5-3600; default 120.)
- **Default Value**: `120`
- **Description**: Delay between group scans that evaluate the periodic triggers. (Seconds, 5-3600; default 120.)
- **Operator Guidance & Impact**: Controls background evaluation frequency. Shorter intervals increase responsiveness at the cost of more frequent world-thread scans; longer intervals conserve server CPU.

### <a id="azerothvoicesgroupchatteridlechance"></a>`AzerothVoices.GroupChatter.IdleChance`

- **Data Type**: Percentage (`0` - `100`)
- **Default Value**: `2`
- **Description**: Chance a scan produces an ordinary casual group remark. (Percentage; default 2.)
- **Operator Guidance & Impact**: Adjust this probability up for chattier, more responsive bots, or down to conserve LLM API tokens on high-population servers.

### <a id="azerothvoicesgroupchatteridlecooldownseconds"></a>`AzerothVoices.GroupChatter.IdleCooldownSeconds`

- **Data Type**: Integer (Seconds, 0-86400; default 300.)
- **Default Value**: `300`
- **Description**: Cooldown after an idle remark. (Seconds, 0-86400; default 300.)
- **Operator Guidance & Impact**: Controls rate-limiting. Increasing this value prevents repetitive triggers and reduces API call frequency; setting to `0` removes time-based gating for this specific event.

### <a id="azerothvoicesgroupchatterdungeonentrychance"></a>`AzerothVoices.GroupChatter.DungeonEntryChance`

- **Data Type**: Percentage (`0` - `100`)
- **Default Value**: `60`
- **Description**: Chance the group comments when a real player enters a dungeon or raid. (Percentage; default 60.)
- **Operator Guidance & Impact**: Adjust this probability up for chattier, more responsive bots, or down to conserve LLM API tokens on high-population servers.

### <a id="azerothvoicesgroupchatterdungeonentrycooldownseconds"></a>`AzerothVoices.GroupChatter.DungeonEntryCooldownSeconds`

- **Data Type**: Integer (Seconds, 0-86400; default 300.)
- **Default Value**: `300`
- **Description**: Cooldown after a dungeon-entry line. (Seconds, 0-86400; default 300.)
- **Operator Guidance & Impact**: Controls rate-limiting. Increasing this value prevents repetitive triggers and reduces API call frequency; setting to `0` removes time-based gating for this specific event.

### <a id="azerothvoicesgroupchatterzonechangechance"></a>`AzerothVoices.GroupChatter.ZoneChangeChance`

- **Data Type**: Percentage (`0` - `100`)
- **Default Value**: `15`
- **Description**: Chance the group comments on an outdoor zone change. (Percentage; default 15.)
- **Operator Guidance & Impact**: Adjust this probability up for chattier, more responsive bots, or down to conserve LLM API tokens on high-population servers.

### <a id="azerothvoicesgroupchatterzonechangecooldownseconds"></a>`AzerothVoices.GroupChatter.ZoneChangeCooldownSeconds`

- **Data Type**: Integer (Seconds, 0-86400; default 180.)
- **Default Value**: `180`
- **Description**: Cooldown after a zone-change line. (Seconds, 0-86400; default 180.)
- **Operator Guidance & Impact**: Controls rate-limiting. Increasing this value prevents repetitive triggers and reduces API call frequency; setting to `0` removes time-based gating for this specific event.

### <a id="azerothvoicesgroupchatterkillchance"></a>`AzerothVoices.GroupChatter.KillChance`

- **Data Type**: Percentage (`0` - `100`)
- **Default Value**: `4`
- **Description**: Chance the group reacts to an ordinary creature kill. (Percentage; default 4.)
- **Operator Guidance & Impact**: Adjust this probability up for chattier, more responsive bots, or down to conserve LLM API tokens on high-population servers.

### <a id="azerothvoicesgroupchatterkillcooldownseconds"></a>`AzerothVoices.GroupChatter.KillCooldownSeconds`

- **Data Type**: Integer (Seconds, 0-86400; default 300.)
- **Default Value**: `300`
- **Description**: Cooldown after a trash-kill line. (Seconds, 0-86400; default 300.)
- **Operator Guidance & Impact**: Controls rate-limiting. Increasing this value prevents repetitive triggers and reduces API call frequency; setting to `0` removes time-based gating for this specific event.

### <a id="azerothvoicesgroupchatterbosspullchance"></a>`AzerothVoices.GroupChatter.BossPullChance`

- **Data Type**: Percentage (`0` - `100`)
- **Default Value**: `50`
- **Description**: Chance the group reacts when a classified dungeon/raid boss is engaged. (Percentage; default 50.)
- **Operator Guidance & Impact**: Adjust this probability up for chattier, more responsive bots, or down to conserve LLM API tokens on high-population servers.

### <a id="azerothvoicesgroupchatterbosspullcooldownseconds"></a>`AzerothVoices.GroupChatter.BossPullCooldownSeconds`

- **Data Type**: Integer (Seconds, 0-86400; default 300.)
- **Default Value**: `300`
- **Description**: Cooldown after a boss-pull line. (Seconds, 0-86400; default 300.)
- **Operator Guidance & Impact**: Controls rate-limiting. Increasing this value prevents repetitive triggers and reduces API call frequency; setting to `0` removes time-based gating for this specific event.

### <a id="azerothvoicesgroupchatterbosskillchance"></a>`AzerothVoices.GroupChatter.BossKillChance`

- **Data Type**: Percentage (`0` - `100`)
- **Default Value**: `80`
- **Description**: Chance the group reacts to a classified boss kill. (Percentage; default 80.)
- **Operator Guidance & Impact**: Adjust this probability up for chattier, more responsive bots, or down to conserve LLM API tokens on high-population servers.

### <a id="azerothvoicesgroupchatterbosskillcooldownseconds"></a>`AzerothVoices.GroupChatter.BossKillCooldownSeconds`

- **Data Type**: Integer (Seconds, 0-86400; default 300.)
- **Default Value**: `300`
- **Description**: Cooldown after a boss-kill line. (Seconds, 0-86400; default 300.)
- **Operator Guidance & Impact**: Controls rate-limiting. Increasing this value prevents repetitive triggers and reduces API call frequency; setting to `0` removes time-based gating for this specific event.

### <a id="azerothvoicesgroupchatterwipechance"></a>`AzerothVoices.GroupChatter.WipeChance`

- **Data Type**: Percentage (`0` - `100`)
- **Default Value**: `80`
- **Description**: Chance the group comments on a wipe, detected once per wipe when every member is dead and out of combat. (Percentage; default 80.)
- **Operator Guidance & Impact**: Adjust this probability up for chattier, more responsive bots, or down to conserve LLM API tokens on high-population servers.

### <a id="azerothvoicesgroupchatterwipecooldownseconds"></a>`AzerothVoices.GroupChatter.WipeCooldownSeconds`

- **Data Type**: Integer (Seconds, 0-86400; default 300.)
- **Default Value**: `300`
- **Description**: Cooldown after a wipe line. (Seconds, 0-86400; default 300.)
- **Operator Guidance & Impact**: Controls rate-limiting. Increasing this value prevents repetitive triggers and reduces API call frequency; setting to `0` removes time-based gating for this specific event.

### <a id="azerothvoicesgroupchatterdeathchance"></a>`AzerothVoices.GroupChatter.DeathChance`

- **Data Type**: Percentage (`0` - `100`)
- **Default Value**: `25`
- **Description**: Chance the group reacts to a member's death. (Percentage; default 25.)
- **Operator Guidance & Impact**: Adjust this probability up for chattier, more responsive bots, or down to conserve LLM API tokens on high-population servers.

### <a id="azerothvoicesgroupchatterdeathcooldownseconds"></a>`AzerothVoices.GroupChatter.DeathCooldownSeconds`

- **Data Type**: Integer (Seconds, 0-86400; default 60.)
- **Default Value**: `60`
- **Description**: Cooldown after a death line. (Seconds, 0-86400; default 60.)
- **Operator Guidance & Impact**: Controls rate-limiting. Increasing this value prevents repetitive triggers and reduces API call frequency; setting to `0` removes time-based gating for this specific event.

### <a id="azerothvoicesgroupchattercorpserunchance"></a>`AzerothVoices.GroupChatter.CorpseRunChance`

- **Data Type**: Percentage (`0` - `100`)
- **Default Value**: `40`
- **Description**: Chance the group comments when a member releases their spirit. (Percentage; default 40.)
- **Operator Guidance & Impact**: Adjust this probability up for chattier, more responsive bots, or down to conserve LLM API tokens on high-population servers.

### <a id="azerothvoicesgroupchattercorpseruncooldownseconds"></a>`AzerothVoices.GroupChatter.CorpseRunCooldownSeconds`

- **Data Type**: Integer (Seconds, 0-86400; default 300.)
- **Default Value**: `300`
- **Description**: Cooldown after a corpse-run line. (Seconds, 0-86400; default 300.)
- **Operator Guidance & Impact**: Controls rate-limiting. Increasing this value prevents repetitive triggers and reduces API call frequency; setting to `0` removes time-based gating for this specific event.

### <a id="azerothvoicesgroupchatterresurrectchance"></a>`AzerothVoices.GroupChatter.ResurrectChance`

- **Data Type**: Percentage (`0` - `100`)
- **Default Value**: `50`
- **Description**: Chance the group welcomes a member back after an observed resurrection. (Percentage; default 50.)
- **Operator Guidance & Impact**: Adjust this probability up for chattier, more responsive bots, or down to conserve LLM API tokens on high-population servers.

### <a id="azerothvoicesgroupchatterresurrectcooldownseconds"></a>`AzerothVoices.GroupChatter.ResurrectCooldownSeconds`

- **Data Type**: Integer (Seconds, 0-86400; default 120.)
- **Default Value**: `120`
- **Description**: Cooldown after a resurrect line. (Seconds, 0-86400; default 120.)
- **Operator Guidance & Impact**: Controls rate-limiting. Increasing this value prevents repetitive triggers and reduces API call frequency; setting to `0` removes time-based gating for this specific event.

### <a id="azerothvoicesgroupchatterlootchancegreen"></a>`AzerothVoices.GroupChatter.LootChanceGreen`

- **Data Type**: Percentage (`0` - `100`)
- **Default Value**: `10`
- **Description**: Loot chatter chance by item quality; poor and common loot never triggers. (Percentage; default 10.)
- **Operator Guidance & Impact**: Adjust this probability up for chattier, more responsive bots, or down to conserve LLM API tokens on high-population servers.

### <a id="azerothvoicesgroupchatterlootchanceblue"></a>`AzerothVoices.GroupChatter.LootChanceBlue`

- **Data Type**: Percentage (`0` - `100`)
- **Default Value**: `35`
- **Description**: Rare (blue) loot chatter chance. (Percentage; default 35.)
- **Operator Guidance & Impact**: Adjust this probability up for chattier, more responsive bots, or down to conserve LLM API tokens on high-population servers.

### <a id="azerothvoicesgroupchatterlootchancepurple"></a>`AzerothVoices.GroupChatter.LootChancePurple`

- **Data Type**: Percentage (`0` - `100`)
- **Default Value**: `70`
- **Description**: Epic (purple) loot chatter chance. (Percentage; default 70.)
- **Operator Guidance & Impact**: Adjust this probability up for chattier, more responsive bots, or down to conserve LLM API tokens on high-population servers.

### <a id="azerothvoicesgroupchatterlootchanceorange"></a>`AzerothVoices.GroupChatter.LootChanceOrange`

- **Data Type**: Percentage (`0` - `100`)
- **Default Value**: `100`
- **Description**: Legendary (orange) loot chatter chance. (Percentage; default 100.)
- **Operator Guidance & Impact**: Set to `0` to disable this trigger entirely, or `100` for deterministic triggering every time the event conditions are satisfied.

### <a id="azerothvoicesgroupchatterlootcooldownseconds"></a>`AzerothVoices.GroupChatter.LootCooldownSeconds`

- **Data Type**: Integer (Seconds, 0-86400; default 120.)
- **Default Value**: `120`
- **Description**: Cooldown after a loot line. (Seconds, 0-86400; default 120.)
- **Operator Guidance & Impact**: Controls rate-limiting. Increasing this value prevents repetitive triggers and reduces API call frequency; setting to `0` removes time-based gating for this specific event.

### <a id="azerothvoicesgroupchatterquestacceptchance"></a>`AzerothVoices.GroupChatter.QuestAcceptChance`

- **Data Type**: Percentage (`0` - `100`)
- **Default Value**: `25`
- **Description**: Chance the group comments when a member picks up a quest. (Percentage; default 25.)
- **Operator Guidance & Impact**: Adjust this probability up for chattier, more responsive bots, or down to conserve LLM API tokens on high-population servers.

### <a id="azerothvoicesgroupchatterquestobjectivechance"></a>`AzerothVoices.GroupChatter.QuestObjectiveChance`

- **Data Type**: Percentage (`0` - `100`)
- **Default Value**: `20`
- **Description**: Chance the group comments when a quest objective advances. (Percentage; default 20.)
- **Operator Guidance & Impact**: Adjust this probability up for chattier, more responsive bots, or down to conserve LLM API tokens on high-population servers.

### <a id="azerothvoicesgroupchatterquestcompletechance"></a>`AzerothVoices.GroupChatter.QuestCompleteChance`

- **Data Type**: Percentage (`0` - `100`)
- **Default Value**: `25`
- **Description**: Chance the group comments when a member completes a quest. (Percentage; default 25.)
- **Operator Guidance & Impact**: Adjust this probability up for chattier, more responsive bots, or down to conserve LLM API tokens on high-population servers.

### <a id="azerothvoicesgroupchatterquestcooldownseconds"></a>`AzerothVoices.GroupChatter.QuestCooldownSeconds`

- **Data Type**: Integer (Seconds, 0-86400; default 60.)
- **Default Value**: `60`
- **Description**: Cooldown for quest accept/complete lines. (Seconds, 0-86400; default 60.)
- **Operator Guidance & Impact**: Controls rate-limiting. Increasing this value prevents repetitive triggers and reduces API call frequency; setting to `0` removes time-based gating for this specific event.

### <a id="azerothvoicesgroupchatterquestobjectivedebounceseconds"></a>`AzerothVoices.GroupChatter.QuestObjectiveDebounceSeconds`

- **Data Type**: Integer (Seconds, 0-3600; default 60.)
- **Default Value**: `60`
- **Description**: Minimum gap between objective-progress lines, which are otherwise the chattiest trigger. (Seconds, 0-3600; default 60.)
- **Operator Guidance & Impact**: Batches rapid incoming actions into a single consolidated response, preventing bots from spamming back-to-back reactions.

### <a id="azerothvoicesgroupchatterspellcastchance"></a>`AzerothVoices.GroupChatter.SpellCastChance`

- **Data Type**: Percentage (`0` - `100`)
- **Default Value**: `2`
- **Description**: Chance the group comments on a member's spell cast. This hook fires very often, so keep the value low. (Percentage; default 2.)
- **Operator Guidance & Impact**: Adjust this probability up for chattier, more responsive bots, or down to conserve LLM API tokens on high-population servers.

### <a id="azerothvoicesgroupchatterspellcastcooldownseconds"></a>`AzerothVoices.GroupChatter.SpellCastCooldownSeconds`

- **Data Type**: Integer (Seconds, 0-86400; default 180.)
- **Default Value**: `180`
- **Description**: Cooldown after a spell-cast line. (Seconds, 0-86400; default 180.)
- **Operator Guidance & Impact**: Controls rate-limiting. Increasing this value prevents repetitive triggers and reduces API call frequency; setting to `0` removes time-based gating for this specific event.

### <a id="azerothvoicesgroupchatterlowhealthchance"></a>`AzerothVoices.GroupChatter.LowHealthChance`

- **Data Type**: Percentage (`0` - `100`)
- **Default Value**: `40`
- **Description**: Chance a member calls out low health, once per drop below the threshold. (Percentage; default 40.)
- **Operator Guidance & Impact**: Adjust this probability up for chattier, more responsive bots, or down to conserve LLM API tokens on high-population servers.

### <a id="azerothvoicesgroupchatterlowhealththresholdpercent"></a>`AzerothVoices.GroupChatter.LowHealthThresholdPercent`

- **Data Type**: Percentage (`0` - `100`)
- **Default Value**: `35`
- **Description**: Health percentage that counts as low for the callout. (Percentage, 1-99; default 35.)
- **Operator Guidance & Impact**: Adjust this probability up for chattier, more responsive bots, or down to conserve LLM API tokens on high-population servers.

### <a id="azerothvoicesgroupchatterlowhealthcooldownseconds"></a>`AzerothVoices.GroupChatter.LowHealthCooldownSeconds`

- **Data Type**: Integer (Seconds, 0-86400; default 180.)
- **Default Value**: `180`
- **Description**: Cooldown after a low-health callout. (Seconds, 0-86400; default 180.)
- **Operator Guidance & Impact**: Controls rate-limiting. Increasing this value prevents repetitive triggers and reduces API call frequency; setting to `0` removes time-based gating for this specific event.

### <a id="azerothvoicesgroupchatterlowmanachance"></a>`AzerothVoices.GroupChatter.LowManaChance`

- **Data Type**: Percentage (`0` - `100`)
- **Default Value**: `30`
- **Description**: Chance a member calls out low mana, once per drop below the threshold. (Percentage; default 30.)
- **Operator Guidance & Impact**: Adjust this probability up for chattier, more responsive bots, or down to conserve LLM API tokens on high-population servers.

### <a id="azerothvoicesgroupchatterlowmanathresholdpercent"></a>`AzerothVoices.GroupChatter.LowManaThresholdPercent`

- **Data Type**: Percentage (`0` - `100`)
- **Default Value**: `20`
- **Description**: Mana percentage that counts as low for the callout. (Percentage, 1-99; default 20.)
- **Operator Guidance & Impact**: Adjust this probability up for chattier, more responsive bots, or down to conserve LLM API tokens on high-population servers.

### <a id="azerothvoicesgroupchatterlowmanacooldownseconds"></a>`AzerothVoices.GroupChatter.LowManaCooldownSeconds`

- **Data Type**: Integer (Seconds, 0-86400; default 180.)
- **Default Value**: `180`
- **Description**: Cooldown after a low-mana callout. (Seconds, 0-86400; default 180.)
- **Operator Guidance & Impact**: Controls rate-limiting. Increasing this value prevents repetitive triggers and reduces API call frequency; setting to `0` removes time-based gating for this specific event.

### <a id="azerothvoicesgroupchatternearbyobjectchance"></a>`AzerothVoices.GroupChatter.NearbyObjectChance`

- **Data Type**: Percentage (`0` - `100`)
- **Default Value**: `3`
- **Description**: Chance the group comments on a nearby visible game object. (Percentage; default 3.)
- **Operator Guidance & Impact**: Adjust this probability up for chattier, more responsive bots, or down to conserve LLM API tokens on high-population servers.

### <a id="azerothvoicesgroupchatternearbyobjectscanseconds"></a>`AzerothVoices.GroupChatter.NearbyObjectScanSeconds`

- **Data Type**: Integer (Seconds, 5-3600; default 180.)
- **Default Value**: `180`
- **Description**: Delay between nearby-object checks. (Seconds, 5-3600; default 180.)
- **Operator Guidance & Impact**: Controls background evaluation frequency. Shorter intervals increase responsiveness at the cost of more frequent world-thread scans; longer intervals conserve server CPU.

### <a id="azerothvoicesgroupchatternearbyobjectcooldownseconds"></a>`AzerothVoices.GroupChatter.NearbyObjectCooldownSeconds`

- **Data Type**: Integer (Seconds, 0-86400; default 600.)
- **Default Value**: `600`
- **Description**: Cooldown after a nearby-object line. (Seconds, 0-86400; default 600.)
- **Operator Guidance & Impact**: Controls rate-limiting. Increasing this value prevents repetitive triggers and reduces API call frequency; setting to `0` removes time-based gating for this specific event.

### <a id="azerothvoicesgroupchatterbotquestionchance"></a>`AzerothVoices.GroupChatter.BotQuestionChance`

- **Data Type**: Percentage (`0` - `100`)
- **Default Value**: `1`
- **Description**: Chance a grouped bot asks the group a short practical question. (Percentage; default 1.)
- **Operator Guidance & Impact**: Adjust this probability up for chattier, more responsive bots, or down to conserve LLM API tokens on high-population servers.

### <a id="azerothvoicesgroupchatterbotquestionscanseconds"></a>`AzerothVoices.GroupChatter.BotQuestionScanSeconds`

- **Data Type**: Integer (Seconds, 5-3600; default 60.)
- **Default Value**: `60`
- **Description**: Delay between bot-question checks. (Seconds, 5-3600; default 60.)
- **Operator Guidance & Impact**: Controls background evaluation frequency. Shorter intervals increase responsiveness at the cost of more frequent world-thread scans; longer intervals conserve server CPU.

### <a id="azerothvoicesgroupchatterbotquestioncooldownseconds"></a>`AzerothVoices.GroupChatter.BotQuestionCooldownSeconds`

- **Data Type**: Integer (Seconds, 0-86400; default 300.)
- **Default Value**: `300`
- **Description**: Cooldown after a bot question. (Seconds, 0-86400; default 300.)
- **Operator Guidance & Impact**: Controls rate-limiting. Increasing this value prevents repetitive triggers and reduces API call frequency; setting to `0` removes time-based gating for this specific event.

### <a id="azerothvoicesgroupchatterplayerfollowupchance"></a>`AzerothVoices.GroupChatter.PlayerFollowupChance`

- **Data Type**: Percentage (`0` - `100`)
- **Default Value**: `5`
- **Description**: Chance one other grouped bot adds a second line after a bot's reply to a real player's group message. (Percentage; default 5.)
- **Operator Guidance & Impact**: Adjust this probability up for chattier, more responsive bots, or down to conserve LLM API tokens on high-population servers.

### <a id="azerothvoicesgroupchatterplayerfollowupcooldownseconds"></a>`AzerothVoices.GroupChatter.PlayerFollowupCooldownSeconds`

- **Data Type**: Integer (Seconds, 0-86400; default 120.)
- **Default Value**: `120`
- **Description**: Cooldown after such a second line. (Seconds, 0-86400; default 120.)
- **Operator Guidance & Impact**: Controls rate-limiting. Increasing this value prevents repetitive triggers and reduces API call frequency; setting to `0` removes time-based gating for this specific event.

### <a id="azerothvoicesgroupchatterconversationchance"></a>`AzerothVoices.GroupChatter.ConversationChance`

- **Data Type**: Percentage (`0` - `100`)
- **Default Value**: `10`
- **Description**: Chance a group line becomes a short multi-line conversation instead of one line. (Percentage; default 10.)
- **Operator Guidance & Impact**: Adjust this probability up for chattier, more responsive bots, or down to conserve LLM API tokens on high-population servers.

### <a id="azerothvoicesgroupchatterconversationmaximumlines"></a>`AzerothVoices.GroupChatter.ConversationMaximumLines`

- **Data Type**: Integer
- **Default Value**: `3`
- **Description**: Lines a group conversation may deliver. (Lines, 1-12; default 3.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicesgroupchatterconversationmaximumparticipants"></a>`AzerothVoices.GroupChatter.ConversationMaximumParticipants`

- **Data Type**: Integer
- **Default Value**: `2`
- **Description**: Participants a group conversation may use, capped by the number of grouped bots. (Participants, 1-8; default 2.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicesgroupchatterconversationturngapseconds"></a>`AzerothVoices.GroupChatter.ConversationTurnGapSeconds`

- **Data Type**: Integer (Seconds, 0-600; default 12.)
- **Default Value**: `12`
- **Description**: Minimum gap between delivered lines of one group conversation. (Seconds, 0-600; default 12.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicesgroupchatterconversationreplywindowseconds"></a>`AzerothVoices.GroupChatter.ConversationReplyWindowSeconds`

- **Data Type**: Integer (Seconds, 1-600; default 30.)
- **Default Value**: `30`
- **Description**: Window in which a group conversation may continue after its first line. (Seconds, 1-600; default 30.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicesgroupchatterconversationmaximumreplyturns"></a>`AzerothVoices.GroupChatter.ConversationMaximumReplyTurns`

- **Data Type**: Integer
- **Default Value**: `2`
- **Description**: Generated reply turns allowed inside one group conversation. (Turns, 1-20; default 2.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicesraidchatterenable"></a>`AzerothVoices.RaidChatter.Enable`

- **Data Type**: Boolean (`0` or `1`)
- **Default Value**: `1`
- **Description**: Enables the raid-only extras below plus raid delivery of the shared boss moments. (Default 1.)
- **Operator Guidance & Impact**: Keep enabled (`1`) for standard functionality. Set to `0` to completely bypass this feature and eliminate its processing overhead.

### <a id="azerothvoicesraidchatterbattlecrychance"></a>`AzerothVoices.RaidChatter.BattleCryChance`

- **Data Type**: Percentage (`0` - `100`)
- **Default Value**: `40`
- **Description**: Chance the raid shouts a battle cry when a classified boss is engaged. (Percentage; default 40.)
- **Operator Guidance & Impact**: Adjust this probability up for chattier, more responsive bots, or down to conserve LLM API tokens on high-population servers.

### <a id="azerothvoicesraidchatterbattlecrycooldownseconds"></a>`AzerothVoices.RaidChatter.BattleCryCooldownSeconds`

- **Data Type**: Integer (Seconds, 0-86400; default 300.)
- **Default Value**: `300`
- **Description**: Cooldown after a battle cry. (Seconds, 0-86400; default 300.)
- **Operator Guidance & Impact**: Controls rate-limiting. Increasing this value prevents repetitive triggers and reduces API call frequency; setting to `0` removes time-based gating for this specific event.

### <a id="azerothvoicesraidchattermoralechance"></a>`AzerothVoices.RaidChatter.MoraleChance`

- **Data Type**: Percentage (`0` - `100`)
- **Default Value**: `15`
- **Description**: Chance a raid line keeps morale up between pulls. (Percentage; default 15.)
- **Operator Guidance & Impact**: Adjust this probability up for chattier, more responsive bots, or down to conserve LLM API tokens on high-population servers.

### <a id="azerothvoicesraidchattermoralescanseconds"></a>`AzerothVoices.RaidChatter.MoraleScanSeconds`

- **Data Type**: Integer (Seconds, 5-3600; default 120.)
- **Default Value**: `120`
- **Description**: Delay between morale checks. (Seconds, 5-3600; default 120.)
- **Operator Guidance & Impact**: Controls background evaluation frequency. Shorter intervals increase responsiveness at the cost of more frequent world-thread scans; longer intervals conserve server CPU.

### <a id="azerothvoicesraidchattermoralecooldownseconds"></a>`AzerothVoices.RaidChatter.MoraleCooldownSeconds`

- **Data Type**: Integer (Seconds, 0-86400; default 300.)
- **Default Value**: `300`
- **Description**: Cooldown after a morale line. (Seconds, 0-86400; default 300.)
- **Operator Guidance & Impact**: Controls rate-limiting. Increasing this value prevents repetitive triggers and reduces API call frequency; setting to `0` removes time-based gating for this specific event.

### <a id="azerothvoicesraidchatteridlechance"></a>`AzerothVoices.RaidChatter.IdleChance`

- **Data Type**: Percentage (`0` - `100`)
- **Default Value**: `10`
- **Description**: Chance a raid scan produces an ordinary casual remark, replacing the party idle value on raid maps. (Percentage; default 10.)
- **Operator Guidance & Impact**: Adjust this probability up for chattier, more responsive bots, or down to conserve LLM API tokens on high-population servers.

### <a id="azerothvoicesraidchatteridlescanseconds"></a>`AzerothVoices.RaidChatter.IdleScanSeconds`

- **Data Type**: Integer (Seconds, 5-3600; default 60.)
- **Default Value**: `60`
- **Description**: Delay between raid idle checks. (Seconds, 5-3600; default 60.)
- **Operator Guidance & Impact**: Controls background evaluation frequency. Shorter intervals increase responsiveness at the cost of more frequent world-thread scans; longer intervals conserve server CPU.

### <a id="azerothvoicesraidchatteridlecooldownseconds"></a>`AzerothVoices.RaidChatter.IdleCooldownSeconds`

- **Data Type**: Integer (Seconds, 0-86400; default 180.)
- **Default Value**: `180`
- **Description**: Cooldown after a raid idle line. (Seconds, 0-86400; default 180.)
- **Operator Guidance & Impact**: Controls rate-limiting. Increasing this value prevents repetitive triggers and reduces API call frequency; setting to `0` removes time-based gating for this specific event.

---

## 12. Guild chatter

| Setting Name | Type / Range | Default | Summary |
| :--- | :--- | :--- | :--- |
| [`AzerothVoices.GuildChatter.Enable`](#azerothvoicesguildchatterenable) | Boolean (`0` or `1`) | `1` | Enables guild ambient conversation, login greetings and reply gating. Guild... |
| [`AzerothVoices.GuildChatter.ScanSeconds`](#azerothvoicesguildchatterscanseconds) | Integer (Seconds, 5-3600; default 60.) | `60` | Delay between guild ambient scans. (Seconds, 5-3600; default 60.) |
| [`AzerothVoices.GuildChatter.AmbientChance`](#azerothvoicesguildchatterambientchance) | Percentage (`0` - `100`) | `8` | Chance a scan starts a guild ambient line. (Percentage; default 8.) |
| [`AzerothVoices.GuildChatter.AmbientCooldownSeconds`](#azerothvoicesguildchatterambientcooldownseconds) | Integer (Seconds, 0-86400; default 600.) | `600` | Cooldown between guild ambient lines. (Seconds, 0-86400; default 600.) |
| [`AzerothVoices.GuildChatter.ConversationChance`](#azerothvoicesguildchatterconversationchance) | Percentage (`0` - `100`) | `35` | Chance a guild ambient line becomes a short multi-line conversation. (Perce... |
| [`AzerothVoices.GuildChatter.MaximumParticipants`](#azerothvoicesguildchattermaximumparticipants) | Integer | `2` | Participants a guild conversation may use, capped by the number of online g... |
| [`AzerothVoices.GuildChatter.MaximumLines`](#azerothvoicesguildchattermaximumlines) | Integer | `3` | Lines a guild conversation may deliver. (Lines, 1-12; default 3.) |
| [`AzerothVoices.GuildChatter.ParticipantReferenceChance`](#azerothvoicesguildchatterparticipantreferencechance) | Percentage (`0` - `100`) | `20` | Chance the topic instruction mentions another online guild member. (Percent... |
| [`AzerothVoices.GuildChatter.ZoneNameChance`](#azerothvoicesguildchatterzonenamechance) | Percentage (`0` - `100`) | `15` | Chance the topic instruction refers to the current zone. (Percentage; defau... |
| [`AzerothVoices.GuildChatter.HistoryContextChance`](#azerothvoicesguildchatterhistorycontextchance) | Percentage (`0` - `100`) | `25` | Chance the topic instruction asks for something the guild said recently. (P... |
| [`AzerothVoices.GuildChatter.ReplyDebounceSeconds`](#azerothvoicesguildchatterreplydebounceseconds) | Integer (Seconds, 0-600; default 5.) | `5` | Minimum gap between accepted replies to one real guild speaker. (Seconds, 0... |
| [`AzerothVoices.GuildChatter.RecentSpeakerSuppressionSeconds`](#azerothvoicesguildchatterrecentspeakersuppressionseconds) | Integer (Seconds, 0-3600; default 30.) | `30` | Window in which the bot that spoke most recently in a guild is skipped in f... |
| [`AzerothVoices.GuildChatter.LoginGreeting.Enable`](#azerothvoicesguildchatterlogingreetingenable) | Boolean (`0` or `1`) | `1` | Enables the bounded one-line login greeting when a real guild member logs i... |
| [`AzerothVoices.GuildChatter.LoginGreeting.Chance`](#azerothvoicesguildchatterlogingreetingchance) | Percentage (`0` - `100`) | `50` | Chance the greeting is sent. (Percentage; default 50.) |
| [`AzerothVoices.GuildChatter.LoginGreeting.CooldownSeconds`](#azerothvoicesguildchatterlogingreetingcooldownseconds) | Integer (Seconds, 0-86400; default 1800.) | `1800` | Per-recipient greeting cooldown; the default is 30 minutes. (Seconds, 0-864... |
| [`AzerothVoices.GuildChatter.LoginGreeting.MaxCandidates`](#azerothvoicesguildchatterlogingreetingmaxcandidates) | Integer | `12` | Maximum candidate bots to evaluate for guild login greetings. (Candidates, ... |
| [`AzerothVoices.GuildChatter.LoginGreeting.MultiReplyChance`](#azerothvoicesguildchatterlogingreetingmultireplychance) | Percentage (`0` - `100`) | `20` | Chance that multiple bots greet the logging in player. (Percentage; default... |
| [`AzerothVoices.GuildChatter.LoginGreeting.MaxResponders`](#azerothvoicesguildchatterlogingreetingmaxresponders) | Integer | `3` | Maximum bots that greet the logging in player. (Responders, 1-8; default 3.... |
| [`AzerothVoices.GuildChatter.LoginGreeting.QuickChance`](#azerothvoicesguildchatterlogingreetingquickchance) | Percentage (`0` - `100`) | `20` | Chance for Quick (2-5s) timing band. (Percentage; default 20.) |
| [`AzerothVoices.GuildChatter.LoginGreeting.BusyChance`](#azerothvoicesguildchatterlogingreetingbusychance) | Percentage (`0` - `100`) | `25` | Chance for Busy (25-45s) timing band. (Percentage; default 25.) |
| [`AzerothVoices.GuildChatter.LoginGreeting.RetryIntervalSeconds`](#azerothvoicesguildchatterlogingreetingretryintervalseconds) | Integer (Seconds, 1-60; default 5.) | `5` | Interval between candidate readiness checks for pending greetings. (Seconds... |
| [`AzerothVoices.GuildChatter.LoginGreeting.ReadinessTimeoutSeconds`](#azerothvoicesguildchatterlogingreetingreadinesstimeoutseconds) | Integer (Seconds, 5-600; default 90.) | `90` | Maximum timeout waiting for a candidate bot before discarding the greeting.... |
| [`AzerothVoices.GuildChatter.LoginGreeting.PlayerNameChance`](#azerothvoicesguildchatterlogingreetingplayernamechance) | Percentage (`0` - `100`) | `60` | Chance to address the logging-in player by name in the greeting. (Percentag... |
| [`AzerothVoices.GuildChatter.LoginGreeting.MaxCharacters`](#azerothvoicesguildchatterlogingreetingmaxcharacters) | Integer (Characters, 20-500; default 100.) | `100` | Character limit for the guild login greeting. (Characters, 20-500; default ... |
| [`AzerothVoices.GuildChatter.PlayerReplies.Enable`](#azerothvoicesguildchatterplayerrepliesenable) | Boolean (`0` or `1`) | `1` | Advanced Guild player-driven conversation controls Enables advanced Guild p... |
| [`AzerothVoices.GuildChatter.PlayerReplies.DebounceSeconds`](#azerothvoicesguildchatterplayerrepliesdebounceseconds) | Integer (Seconds, 0-600; default 5.) | `5` | Minimum gap between replies to the same player in guild chat; falls back to... |
| [`AzerothVoices.GuildChatter.PlayerReplies.IdleSuppressionSeconds`](#azerothvoicesguildchatterplayerrepliesidlesuppressionseconds) | Integer (Seconds, 0-3600; default 90.) | `90` | Duration of ambient guild chatter suppression while a human conversation is... |
| [`AzerothVoices.GuildChatter.PlayerReplies.MaxCandidates`](#azerothvoicesguildchatterplayerrepliesmaxcandidates) | Integer | `12` | Maximum candidate bots to evaluate for guild replies. (Candidates, 1-32; de... |
| [`AzerothVoices.GuildChatter.PlayerReplies.MultiReplyChance`](#azerothvoicesguildchatterplayerrepliesmultireplychance) | Percentage (`0` - `100`) | `35` | Chance that multiple bots reply to a player guild message. (Percentage; def... |
| [`AzerothVoices.GuildChatter.PlayerReplies.MultiAddressedBonus`](#azerothvoicesguildchatterplayerrepliesmultiaddressedbonus) | Percentage (`0` - `100`) | `50` | Bonus chance added to MultiReplyChance when multiple bots are explicitly ad... |
| [`AzerothVoices.GuildChatter.PlayerReplies.ConversationChance`](#azerothvoicesguildchatterplayerrepliesconversationchance) | Percentage (`0` - `100`) | `20` | Chance that a player reply triggers a multi-bot guild conversation. (Percen... |
| [`AzerothVoices.GuildChatter.PlayerReplies.MaxResponders`](#azerothvoicesguildchatterplayerrepliesmaxresponders) | Integer | `3` | Maximum bot responders to a single player message in multi-reply mode. (Res... |
| [`AzerothVoices.GuildChatter.PlayerReplies.PlayerNameChance`](#azerothvoicesguildchatterplayerrepliesplayernamechance) | Percentage (`0` - `100`) | `45` | Chance to direct the responding bot to address the player by name. (Percent... |
| [`AzerothVoices.GuildChatter.PlayerReplies.CallbackChance`](#azerothvoicesguildchatterplayerrepliescallbackchance) | Percentage (`0` - `100`) | `25` | Chance to inject callback context from earlier guild conversation turns int... |
| [`AzerothVoices.GuildChatter.PlayerReplies.FollowupQuestionChance`](#azerothvoicesguildchatterplayerrepliesfollowupquestionchance) | Percentage (`0` - `100`) | `30` | Chance to direct the responding bot to include a relevant follow-up questio... |
| [`AzerothVoices.GuildChatter.PlayerReplies.RecentSpeakerPenalty`](#azerothvoicesguildchatterplayerrepliesrecentspeakerpenalty) | Percentage (`0` - `100`) | `60` | Soft penalty applied to recent speakers during candidate weighting. (Percen... |
| [`AzerothVoices.GuildChatter.PlayerReplies.FirstDelayMinSeconds`](#azerothvoicesguildchatterplayerrepliesfirstdelayminseconds) | Integer (Seconds, 0-60; default 2.) | `2` | Minimum delay before the first guild reply line is delivered. (Seconds, 0-6... |
| [`AzerothVoices.GuildChatter.PlayerReplies.FirstDelayMaxSeconds`](#azerothvoicesguildchatterplayerrepliesfirstdelaymaxseconds) | Integer (Seconds, 0-60; default 6.) | `6` | Maximum delay before the first guild reply line is delivered. (Seconds, 0-6... |


### <a id="azerothvoicesguildchatterenable"></a>`AzerothVoices.GuildChatter.Enable`

- **Data Type**: Boolean (`0` or `1`)
- **Default Value**: `1`
- **Description**: Enables guild ambient conversation, login greetings and reply gating. Guild replies still require an online real member with listening rights. (Default 1.)
- **Operator Guidance & Impact**: Keep enabled (`1`) for standard functionality. Set to `0` to completely bypass this feature and eliminate its processing overhead.

### <a id="azerothvoicesguildchatterscanseconds"></a>`AzerothVoices.GuildChatter.ScanSeconds`

- **Data Type**: Integer (Seconds, 5-3600; default 60.)
- **Default Value**: `60`
- **Description**: Delay between guild ambient scans. (Seconds, 5-3600; default 60.)
- **Operator Guidance & Impact**: Controls background evaluation frequency. Shorter intervals increase responsiveness at the cost of more frequent world-thread scans; longer intervals conserve server CPU.

### <a id="azerothvoicesguildchatterambientchance"></a>`AzerothVoices.GuildChatter.AmbientChance`

- **Data Type**: Percentage (`0` - `100`)
- **Default Value**: `8`
- **Description**: Chance a scan starts a guild ambient line. (Percentage; default 8.)
- **Operator Guidance & Impact**: Adjust this probability up for chattier, more responsive bots, or down to conserve LLM API tokens on high-population servers.

### <a id="azerothvoicesguildchatterambientcooldownseconds"></a>`AzerothVoices.GuildChatter.AmbientCooldownSeconds`

- **Data Type**: Integer (Seconds, 0-86400; default 600.)
- **Default Value**: `600`
- **Description**: Cooldown between guild ambient lines. (Seconds, 0-86400; default 600.)
- **Operator Guidance & Impact**: Controls rate-limiting. Increasing this value prevents repetitive triggers and reduces API call frequency; setting to `0` removes time-based gating for this specific event.

### <a id="azerothvoicesguildchatterconversationchance"></a>`AzerothVoices.GuildChatter.ConversationChance`

- **Data Type**: Percentage (`0` - `100`)
- **Default Value**: `35`
- **Description**: Chance a guild ambient line becomes a short multi-line conversation. (Percentage; default 35.)
- **Operator Guidance & Impact**: Adjust this probability up for chattier, more responsive bots, or down to conserve LLM API tokens on high-population servers.

### <a id="azerothvoicesguildchattermaximumparticipants"></a>`AzerothVoices.GuildChatter.MaximumParticipants`

- **Data Type**: Integer
- **Default Value**: `2`
- **Description**: Participants a guild conversation may use, capped by the number of online guilded bots. (Participants, 1-8; default 2.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicesguildchattermaximumlines"></a>`AzerothVoices.GuildChatter.MaximumLines`

- **Data Type**: Integer
- **Default Value**: `3`
- **Description**: Lines a guild conversation may deliver. (Lines, 1-12; default 3.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicesguildchatterparticipantreferencechance"></a>`AzerothVoices.GuildChatter.ParticipantReferenceChance`

- **Data Type**: Percentage (`0` - `100`)
- **Default Value**: `20`
- **Description**: Chance the topic instruction mentions another online guild member. (Percentage; default 20.)
- **Operator Guidance & Impact**: Adjust this probability up for chattier, more responsive bots, or down to conserve LLM API tokens on high-population servers.

### <a id="azerothvoicesguildchatterzonenamechance"></a>`AzerothVoices.GuildChatter.ZoneNameChance`

- **Data Type**: Percentage (`0` - `100`)
- **Default Value**: `15`
- **Description**: Chance the topic instruction refers to the current zone. (Percentage; default 15.)
- **Operator Guidance & Impact**: Adjust this probability up for chattier, more responsive bots, or down to conserve LLM API tokens on high-population servers.

### <a id="azerothvoicesguildchatterhistorycontextchance"></a>`AzerothVoices.GuildChatter.HistoryContextChance`

- **Data Type**: Percentage (`0` - `100`)
- **Default Value**: `25`
- **Description**: Chance the topic instruction asks for something the guild said recently. (Percentage; default 25.)
- **Operator Guidance & Impact**: Adjust this probability up for chattier, more responsive bots, or down to conserve LLM API tokens on high-population servers.

### <a id="azerothvoicesguildchatterreplydebounceseconds"></a>`AzerothVoices.GuildChatter.ReplyDebounceSeconds`

- **Data Type**: Integer (Seconds, 0-600; default 5.)
- **Default Value**: `5`
- **Description**: Minimum gap between accepted replies to one real guild speaker. (Seconds, 0-600; default 5.)
- **Operator Guidance & Impact**: Batches rapid incoming actions into a single consolidated response, preventing bots from spamming back-to-back reactions.

### <a id="azerothvoicesguildchatterrecentspeakersuppressionseconds"></a>`AzerothVoices.GuildChatter.RecentSpeakerSuppressionSeconds`

- **Data Type**: Integer (Seconds, 0-3600; default 30.)
- **Default Value**: `30`
- **Description**: Window in which the bot that spoke most recently in a guild is skipped in favour of another. (Seconds, 0-3600; default 30.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicesguildchatterlogingreetingenable"></a>`AzerothVoices.GuildChatter.LoginGreeting.Enable`

- **Data Type**: Boolean (`0` or `1`)
- **Default Value**: `1`
- **Description**: Enables the bounded one-line login greeting when a real guild member logs in and a guilded bot is online. (Default 1.)
- **Operator Guidance & Impact**: Keep enabled (`1`) for standard functionality. Set to `0` to completely bypass this feature and eliminate its processing overhead.

### <a id="azerothvoicesguildchatterlogingreetingchance"></a>`AzerothVoices.GuildChatter.LoginGreeting.Chance`

- **Data Type**: Percentage (`0` - `100`)
- **Default Value**: `50`
- **Description**: Chance the greeting is sent. (Percentage; default 50.)
- **Operator Guidance & Impact**: Adjust this probability up for chattier, more responsive bots, or down to conserve LLM API tokens on high-population servers.

### <a id="azerothvoicesguildchatterlogingreetingcooldownseconds"></a>`AzerothVoices.GuildChatter.LoginGreeting.CooldownSeconds`

- **Data Type**: Integer (Seconds, 0-86400; default 1800.)
- **Default Value**: `1800`
- **Description**: Per-recipient greeting cooldown; the default is 30 minutes. (Seconds, 0-86400; default 1800.)
- **Operator Guidance & Impact**: Controls rate-limiting. Increasing this value prevents repetitive triggers and reduces API call frequency; setting to `0` removes time-based gating for this specific event.

### <a id="azerothvoicesguildchatterlogingreetingmaxcandidates"></a>`AzerothVoices.GuildChatter.LoginGreeting.MaxCandidates`

- **Data Type**: Integer
- **Default Value**: `12`
- **Description**: Maximum candidate bots to evaluate for guild login greetings. (Candidates, 1-32; default 12.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicesguildchatterlogingreetingmultireplychance"></a>`AzerothVoices.GuildChatter.LoginGreeting.MultiReplyChance`

- **Data Type**: Percentage (`0` - `100`)
- **Default Value**: `20`
- **Description**: Chance that multiple bots greet the logging in player. (Percentage; default 20.)
- **Operator Guidance & Impact**: Adjust this probability up for chattier, more responsive bots, or down to conserve LLM API tokens on high-population servers.

### <a id="azerothvoicesguildchatterlogingreetingmaxresponders"></a>`AzerothVoices.GuildChatter.LoginGreeting.MaxResponders`

- **Data Type**: Integer
- **Default Value**: `3`
- **Description**: Maximum bots that greet the logging in player. (Responders, 1-8; default 3.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicesguildchatterlogingreetingquickchance"></a>`AzerothVoices.GuildChatter.LoginGreeting.QuickChance`

- **Data Type**: Percentage (`0` - `100`)
- **Default Value**: `20`
- **Description**: Chance for Quick (2-5s) timing band. (Percentage; default 20.)
- **Operator Guidance & Impact**: Adjust this probability up for chattier, more responsive bots, or down to conserve LLM API tokens on high-population servers.

### <a id="azerothvoicesguildchatterlogingreetingbusychance"></a>`AzerothVoices.GuildChatter.LoginGreeting.BusyChance`

- **Data Type**: Percentage (`0` - `100`)
- **Default Value**: `25`
- **Description**: Chance for Busy (25-45s) timing band. (Percentage; default 25.)
- **Operator Guidance & Impact**: Adjust this probability up for chattier, more responsive bots, or down to conserve LLM API tokens on high-population servers.

### <a id="azerothvoicesguildchatterlogingreetingretryintervalseconds"></a>`AzerothVoices.GuildChatter.LoginGreeting.RetryIntervalSeconds`

- **Data Type**: Integer (Seconds, 1-60; default 5.)
- **Default Value**: `5`
- **Description**: Interval between candidate readiness checks for pending greetings. (Seconds, 1-60; default 5.)
- **Operator Guidance & Impact**: Controls background evaluation frequency. Shorter intervals increase responsiveness at the cost of more frequent world-thread scans; longer intervals conserve server CPU.

### <a id="azerothvoicesguildchatterlogingreetingreadinesstimeoutseconds"></a>`AzerothVoices.GuildChatter.LoginGreeting.ReadinessTimeoutSeconds`

- **Data Type**: Integer (Seconds, 5-600; default 90.)
- **Default Value**: `90`
- **Description**: Maximum timeout waiting for a candidate bot before discarding the greeting. (Seconds, 5-600; default 90.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicesguildchatterlogingreetingplayernamechance"></a>`AzerothVoices.GuildChatter.LoginGreeting.PlayerNameChance`

- **Data Type**: Percentage (`0` - `100`)
- **Default Value**: `60`
- **Description**: Chance to address the logging-in player by name in the greeting. (Percentage; default 60.)
- **Operator Guidance & Impact**: Adjust this probability up for chattier, more responsive bots, or down to conserve LLM API tokens on high-population servers.

### <a id="azerothvoicesguildchatterlogingreetingmaxcharacters"></a>`AzerothVoices.GuildChatter.LoginGreeting.MaxCharacters`

- **Data Type**: Integer (Characters, 20-500; default 100.)
- **Default Value**: `100`
- **Description**: Character limit for the guild login greeting. (Characters, 20-500; default 100.)
- **Operator Guidance & Impact**: Directly limits output length. Lower values keep chat punchy and reduce API completion token costs; higher values allow more descriptive storytelling.

### <a id="azerothvoicesguildchatterplayerrepliesenable"></a>`AzerothVoices.GuildChatter.PlayerReplies.Enable`

- **Data Type**: Boolean (`0` or `1`)
- **Default Value**: `1`
- **Description**: Advanced Guild player-driven conversation controls Enables advanced Guild player-driven conversation controls. (Default 1.)
- **Operator Guidance & Impact**: Keep enabled (`1`) for standard functionality. Set to `0` to completely bypass this feature and eliminate its processing overhead.

### <a id="azerothvoicesguildchatterplayerrepliesdebounceseconds"></a>`AzerothVoices.GuildChatter.PlayerReplies.DebounceSeconds`

- **Data Type**: Integer (Seconds, 0-600; default 5.)
- **Default Value**: `5`
- **Description**: Minimum gap between replies to the same player in guild chat; falls back to ReplyDebounceSeconds if not set. (Seconds, 0-600; default 5.)
- **Operator Guidance & Impact**: Batches rapid incoming actions into a single consolidated response, preventing bots from spamming back-to-back reactions.

### <a id="azerothvoicesguildchatterplayerrepliesidlesuppressionseconds"></a>`AzerothVoices.GuildChatter.PlayerReplies.IdleSuppressionSeconds`

- **Data Type**: Integer (Seconds, 0-3600; default 90.)
- **Default Value**: `90`
- **Description**: Duration of ambient guild chatter suppression while a human conversation is active. (Seconds, 0-3600; default 90.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicesguildchatterplayerrepliesmaxcandidates"></a>`AzerothVoices.GuildChatter.PlayerReplies.MaxCandidates`

- **Data Type**: Integer
- **Default Value**: `12`
- **Description**: Maximum candidate bots to evaluate for guild replies. (Candidates, 1-32; default 12.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicesguildchatterplayerrepliesmultireplychance"></a>`AzerothVoices.GuildChatter.PlayerReplies.MultiReplyChance`

- **Data Type**: Percentage (`0` - `100`)
- **Default Value**: `35`
- **Description**: Chance that multiple bots reply to a player guild message. (Percentage; default 35.)
- **Operator Guidance & Impact**: Adjust this probability up for chattier, more responsive bots, or down to conserve LLM API tokens on high-population servers.

### <a id="azerothvoicesguildchatterplayerrepliesmultiaddressedbonus"></a>`AzerothVoices.GuildChatter.PlayerReplies.MultiAddressedBonus`

- **Data Type**: Percentage (`0` - `100`)
- **Default Value**: `50`
- **Description**: Bonus chance added to MultiReplyChance when multiple bots are explicitly addressed by name. (Percentage; default 50.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicesguildchatterplayerrepliesconversationchance"></a>`AzerothVoices.GuildChatter.PlayerReplies.ConversationChance`

- **Data Type**: Percentage (`0` - `100`)
- **Default Value**: `20`
- **Description**: Chance that a player reply triggers a multi-bot guild conversation. (Percentage; default 20.)
- **Operator Guidance & Impact**: Adjust this probability up for chattier, more responsive bots, or down to conserve LLM API tokens on high-population servers.

### <a id="azerothvoicesguildchatterplayerrepliesmaxresponders"></a>`AzerothVoices.GuildChatter.PlayerReplies.MaxResponders`

- **Data Type**: Integer
- **Default Value**: `3`
- **Description**: Maximum bot responders to a single player message in multi-reply mode. (Responders, 1-8; default 3.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicesguildchatterplayerrepliesplayernamechance"></a>`AzerothVoices.GuildChatter.PlayerReplies.PlayerNameChance`

- **Data Type**: Percentage (`0` - `100`)
- **Default Value**: `45`
- **Description**: Chance to direct the responding bot to address the player by name. (Percentage; default 45.)
- **Operator Guidance & Impact**: Adjust this probability up for chattier, more responsive bots, or down to conserve LLM API tokens on high-population servers.

### <a id="azerothvoicesguildchatterplayerrepliescallbackchance"></a>`AzerothVoices.GuildChatter.PlayerReplies.CallbackChance`

- **Data Type**: Percentage (`0` - `100`)
- **Default Value**: `25`
- **Description**: Chance to inject callback context from earlier guild conversation turns into the prompt. (Percentage; default 25.)
- **Operator Guidance & Impact**: Adjust this probability up for chattier, more responsive bots, or down to conserve LLM API tokens on high-population servers.

### <a id="azerothvoicesguildchatterplayerrepliesfollowupquestionchance"></a>`AzerothVoices.GuildChatter.PlayerReplies.FollowupQuestionChance`

- **Data Type**: Percentage (`0` - `100`)
- **Default Value**: `30`
- **Description**: Chance to direct the responding bot to include a relevant follow-up question. (Percentage; default 30.)
- **Operator Guidance & Impact**: Adjust this probability up for chattier, more responsive bots, or down to conserve LLM API tokens on high-population servers.

### <a id="azerothvoicesguildchatterplayerrepliesrecentspeakerpenalty"></a>`AzerothVoices.GuildChatter.PlayerReplies.RecentSpeakerPenalty`

- **Data Type**: Percentage (`0` - `100`)
- **Default Value**: `60`
- **Description**: Soft penalty applied to recent speakers during candidate weighting. (Percentage, 0-100; default 60.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicesguildchatterplayerrepliesfirstdelayminseconds"></a>`AzerothVoices.GuildChatter.PlayerReplies.FirstDelayMinSeconds`

- **Data Type**: Integer (Seconds, 0-60; default 2.)
- **Default Value**: `2`
- **Description**: Minimum delay before the first guild reply line is delivered. (Seconds, 0-60; default 2.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicesguildchatterplayerrepliesfirstdelaymaxseconds"></a>`AzerothVoices.GuildChatter.PlayerReplies.FirstDelayMaxSeconds`

- **Data Type**: Integer (Seconds, 0-60; default 6.)
- **Default Value**: `6`
- **Description**: Maximum delay before the first guild reply line is delivered. (Seconds, 0-60; default 6.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

---

## 13. Deterministic bot memory ledger

> C++ writes short fixed-template memories from verified facts only: no provider
call is made and no player-written text is ever stored. Memories are per
bot/player pair, they feed a bounded prompt block, and both the Chatter addon's
Forget button and `.av memory forget` delete them.


| Setting Name | Type / Range | Default | Summary |
| :--- | :--- | :--- | :--- |
| [`AzerothVoices.Memory.Enable`](#azerothvoicesmemoryenable) | Boolean (`0` or `1`) | `1` | Enables the memory ledger. (Default 1.) |
| [`AzerothVoices.Memory.CacheMaximumPairs`](#azerothvoicesmemorycachemaximumpairs) | Integer | `4096` | Hot-cache bound for loaded bot/player memory pairs. (Pairs, 1-100000; defau... |
| [`AzerothVoices.Memory.MaximumPerPair`](#azerothvoicesmemorymaximumperpair) | Integer | `30` | Newest rows kept per bot/player pair; older rows are pruned on write. (Rows... |
| [`AzerothVoices.Memory.RetentionDays`](#azerothvoicesmemoryretentiondays) | Integer | `180` | Age after which untouched rows are deleted by the hourly cleanup. (Days, 1-... |
| [`AzerothVoices.Memory.MaximumPromptItems`](#azerothvoicesmemorymaximumpromptitems) | Integer | `3` | Memories injected into one prompt. (Items, 1-20; default 3.) |
| [`AzerothVoices.Memory.MaximumPromptCharacters`](#azerothvoicesmemorymaximumpromptcharacters) | Integer (Characters, 64-4000; default 600.) | `600` | Character cap for the injected memory block, charged to the context budget ... |
| [`AzerothVoices.Memory.RecallChance`](#azerothvoicesmemoryrecallchance) | Percentage (`0` - `100`) | `15` | Chance an ambient or group line draws on a memory at all; direct conversati... |
| [`AzerothVoices.Memory.FirstMetChance`](#azerothvoicesmemoryfirstmetchance) | Percentage (`0` - `100`) | `20` | Chance a first meeting in the same group becomes a durable memory. (Percent... |
| [`AzerothVoices.Memory.PartyMemberChance`](#azerothvoicesmemorypartymemberchance) | Percentage (`0` - `100`) | `25` | Chance joining a real player's group becomes a memory. (Percentage; default... |
| [`AzerothVoices.Memory.QuestCompletedChance`](#azerothvoicesmemoryquestcompletedchance) | Percentage (`0` - `100`) | `25` | Chance completing a quest while grouped becomes a memory. (Percentage; defa... |
| [`AzerothVoices.Memory.BossKillChance`](#azerothvoicesmemorybosskillchance) | Percentage (`0` - `100`) | `50` | Chance a classified boss kill while grouped becomes a memory. (Percentage; ... |
| [`AzerothVoices.Memory.PvpKillChance`](#azerothvoicesmemorypvpkillchance) | Percentage (`0` - `100`) | `20` | Chance a PvP kill while grouped becomes a memory. (Percentage; default 20.) |
| [`AzerothVoices.Memory.WipeChance`](#azerothvoicesmemorywipechance) | Percentage (`0` - `100`) | `50` | Chance a wipe becomes a memory for every grouped bot. (Percentage; default ... |
| [`AzerothVoices.Memory.LevelUpChance`](#azerothvoicesmemorylevelupchance) | Percentage (`0` - `100`) | `40` | Chance a level-up while grouped becomes a memory. (Percentage; default 40.) |
| [`AzerothVoices.Memory.DungeonCompletedChance`](#azerothvoicesmemorydungeoncompletedchance) | Percentage (`0` - `100`) | `30` | Chance leaving an instance after at least one boss kill becomes a "we clear... |
| [`AzerothVoices.Memory.DatabaseFlushSeconds`](#azerothvoicesmemorydatabaseflushseconds) | Integer (Seconds, 1-3600; default 5.) | `5` | Coalesced memory write interval. (Seconds, 1-3600; default 5.) |
| [`AzerothVoices.Memory.DatabaseFlushBatchSize`](#azerothvoicesmemorydatabaseflushbatchsize) | Integer | `50` | Rows per normal memory flush batch. (Rows, 1-1000; default 50.) |


### <a id="azerothvoicesmemoryenable"></a>`AzerothVoices.Memory.Enable`

- **Data Type**: Boolean (`0` or `1`)
- **Default Value**: `1`
- **Description**: Enables the memory ledger. (Default 1.)
- **Operator Guidance & Impact**: Keep enabled (`1`) for standard functionality. Set to `0` to completely bypass this feature and eliminate its processing overhead.

### <a id="azerothvoicesmemorycachemaximumpairs"></a>`AzerothVoices.Memory.CacheMaximumPairs`

- **Data Type**: Integer
- **Default Value**: `4096`
- **Description**: Hot-cache bound for loaded bot/player memory pairs. (Pairs, 1-100000; default 4096.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicesmemorymaximumperpair"></a>`AzerothVoices.Memory.MaximumPerPair`

- **Data Type**: Integer
- **Default Value**: `30`
- **Description**: Newest rows kept per bot/player pair; older rows are pruned on write. (Rows, 1-200; default 30.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicesmemoryretentiondays"></a>`AzerothVoices.Memory.RetentionDays`

- **Data Type**: Integer
- **Default Value**: `180`
- **Description**: Age after which untouched rows are deleted by the hourly cleanup. (Days, 1-3650; default 180.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicesmemorymaximumpromptitems"></a>`AzerothVoices.Memory.MaximumPromptItems`

- **Data Type**: Integer
- **Default Value**: `3`
- **Description**: Memories injected into one prompt. (Items, 1-20; default 3.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicesmemorymaximumpromptcharacters"></a>`AzerothVoices.Memory.MaximumPromptCharacters`

- **Data Type**: Integer (Characters, 64-4000; default 600.)
- **Default Value**: `600`
- **Description**: Character cap for the injected memory block, charged to the context budget below personality. (Characters, 64-4000; default 600.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicesmemoryrecallchance"></a>`AzerothVoices.Memory.RecallChance`

- **Data Type**: Percentage (`0` - `100`)
- **Default Value**: `15`
- **Description**: Chance an ambient or group line draws on a memory at all; direct conversations may always use one. (Percentage; default 15.)
- **Operator Guidance & Impact**: Adjust this probability up for chattier, more responsive bots, or down to conserve LLM API tokens on high-population servers.

### <a id="azerothvoicesmemoryfirstmetchance"></a>`AzerothVoices.Memory.FirstMetChance`

- **Data Type**: Percentage (`0` - `100`)
- **Default Value**: `20`
- **Description**: Chance a first meeting in the same group becomes a durable memory. (Percentage; default 20.)
- **Operator Guidance & Impact**: Adjust this probability up for chattier, more responsive bots, or down to conserve LLM API tokens on high-population servers.

### <a id="azerothvoicesmemorypartymemberchance"></a>`AzerothVoices.Memory.PartyMemberChance`

- **Data Type**: Percentage (`0` - `100`)
- **Default Value**: `25`
- **Description**: Chance joining a real player's group becomes a memory. (Percentage; default 25.)
- **Operator Guidance & Impact**: Adjust this probability up for chattier, more responsive bots, or down to conserve LLM API tokens on high-population servers.

### <a id="azerothvoicesmemoryquestcompletedchance"></a>`AzerothVoices.Memory.QuestCompletedChance`

- **Data Type**: Percentage (`0` - `100`)
- **Default Value**: `25`
- **Description**: Chance completing a quest while grouped becomes a memory. (Percentage; default 25.)
- **Operator Guidance & Impact**: Adjust this probability up for chattier, more responsive bots, or down to conserve LLM API tokens on high-population servers.

### <a id="azerothvoicesmemorybosskillchance"></a>`AzerothVoices.Memory.BossKillChance`

- **Data Type**: Percentage (`0` - `100`)
- **Default Value**: `50`
- **Description**: Chance a classified boss kill while grouped becomes a memory. (Percentage; default 50.)
- **Operator Guidance & Impact**: Adjust this probability up for chattier, more responsive bots, or down to conserve LLM API tokens on high-population servers.

### <a id="azerothvoicesmemorypvpkillchance"></a>`AzerothVoices.Memory.PvpKillChance`

- **Data Type**: Percentage (`0` - `100`)
- **Default Value**: `20`
- **Description**: Chance a PvP kill while grouped becomes a memory. (Percentage; default 20.)
- **Operator Guidance & Impact**: Adjust this probability up for chattier, more responsive bots, or down to conserve LLM API tokens on high-population servers.

### <a id="azerothvoicesmemorywipechance"></a>`AzerothVoices.Memory.WipeChance`

- **Data Type**: Percentage (`0` - `100`)
- **Default Value**: `50`
- **Description**: Chance a wipe becomes a memory for every grouped bot. (Percentage; default 50.)
- **Operator Guidance & Impact**: Adjust this probability up for chattier, more responsive bots, or down to conserve LLM API tokens on high-population servers.

### <a id="azerothvoicesmemorylevelupchance"></a>`AzerothVoices.Memory.LevelUpChance`

- **Data Type**: Percentage (`0` - `100`)
- **Default Value**: `40`
- **Description**: Chance a level-up while grouped becomes a memory. (Percentage; default 40.)
- **Operator Guidance & Impact**: Adjust this probability up for chattier, more responsive bots, or down to conserve LLM API tokens on high-population servers.

### <a id="azerothvoicesmemorydungeoncompletedchance"></a>`AzerothVoices.Memory.DungeonCompletedChance`

- **Data Type**: Percentage (`0` - `100`)
- **Default Value**: `30`
- **Description**: Chance leaving an instance after at least one boss kill becomes a "we cleared this" memory. (Percentage; default 30.)
- **Operator Guidance & Impact**: Adjust this probability up for chattier, more responsive bots, or down to conserve LLM API tokens on high-population servers.

### <a id="azerothvoicesmemorydatabaseflushseconds"></a>`AzerothVoices.Memory.DatabaseFlushSeconds`

- **Data Type**: Integer (Seconds, 1-3600; default 5.)
- **Default Value**: `5`
- **Description**: Coalesced memory write interval. (Seconds, 1-3600; default 5.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicesmemorydatabaseflushbatchsize"></a>`AzerothVoices.Memory.DatabaseFlushBatchSize`

- **Data Type**: Integer
- **Default Value**: `50`
- **Description**: Rows per normal memory flush batch. (Rows, 1-1000; default 50.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

---

## 14. Random nearby, guild, and world chatter

| Setting Name | Type / Range | Default | Summary |
| :--- | :--- | :--- | :--- |
| [`AzerothVoices.Random.Enable`](#azerothvoicesrandomenable) | Boolean (`0` or `1`) | `1` | Enables scheduled ambient chatter around online real players. (Default 1.) |
| [`AzerothVoices.Random.MinimumIntervalSeconds`](#azerothvoicesrandomminimumintervalseconds) | Integer (Seconds, minimum 5; default 90.) | `90` | Lower bound before the next ambient attempt. (Seconds, minimum 5; default 9... |
| [`AzerothVoices.Random.MaximumIntervalSeconds`](#azerothvoicesrandommaximumintervalseconds) | Integer (Seconds, at least MinimumIntervalSeconds; default 240.) | `240` | Upper interval bound. (Seconds, at least MinimumIntervalSeconds; default 24... |
| [`AzerothVoices.Random.FollowupChance`](#azerothvoicesrandomfollowupchance) | Percentage (`0` - `100`) | `15` | Chance another actor replies after ambient chatter, a bot-originated Say/Ye... |
| [`AzerothVoices.Random.MaximumActors`](#azerothvoicesrandommaximumactors) | Integer | `2` | Generated conversation chain cap; 1 disables follow-ups. (Generated turns, ... |
| [`AzerothVoices.Random.Scopes`](#azerothvoicesrandomscopes) | String (Comma-separated list) | `guild,world,party` | Chooses ambient-chat delivery scopes; a disabled or unavailable group scope... |
| [`AzerothVoices.Random.Prompts`](#azerothvoicesrandomprompts) | String (Pipe-separated choices) | `Make a casual observation about the current zone.|Complain briefly about leveling or grinding.|Comment on loot, repairs, travel, professions, quests, dungeons, or battlegrounds.|Make a short joke that fits World of Warcraft.|Mention what you want to do next in the game.` | General ambient topic instructions. (Pipe-separated text choices.) |
| [`AzerothVoices.Random.Questions`](#azerothvoicesrandomquestions) | String (Pipe-separated choices) | `Ask whether anyone wants to group.|Ask for an opinion about the current zone.|Ask which dungeon people like.|Ask what people are farming.|Ask about professions, gear, or PvP.` | Question-style ambient instructions. (Pipe-separated text choices.) |
| [`AzerothVoices.Random.EnvironmentPrompts`](#azerothvoicesrandomenvironmentprompts) | String (Pipe-separated choices) | `React to something nearby.|Comment on the weather or surroundings.|Mention a creature, object, quest, spell, item, vendor, dungeon, or unfinished task that fits the location.` | Instructions that use live surroundings. (Pipe-separated text choices.) |
| [`AzerothVoices.Random.GuildPrompts`](#azerothvoicesrandomguildprompts) | String (Pipe-separated choices) | `Ask whether guild members want a dungeon group.|Congratulate the guild on recent progress.|Talk about raids, PvP, professions, loot, the guild bank, or helping another member.` | Topics used for guild ambient chatter. (Pipe-separated text choices.) |
| [`AzerothVoices.Random.WorldPrompts`](#azerothvoicesrandomworldprompts) | String (Pipe-separated choices) | `Start a short world-chat topic about zones, dungeons, loot, professions, PvP, quests, travel, or server life.|Ask world chat a brief useful question.|Make a short public observation that could start a conversation.` | Topics used for world/channel ambient chatter. (Pipe-separated text choices... |


### <a id="azerothvoicesrandomenable"></a>`AzerothVoices.Random.Enable`

- **Data Type**: Boolean (`0` or `1`)
- **Default Value**: `1`
- **Description**: Enables scheduled ambient chatter around online real players. (Default 1.)
- **Operator Guidance & Impact**: Keep enabled (`1`) for standard functionality. Set to `0` to completely bypass this feature and eliminate its processing overhead.

### <a id="azerothvoicesrandomminimumintervalseconds"></a>`AzerothVoices.Random.MinimumIntervalSeconds`

- **Data Type**: Integer (Seconds, minimum 5; default 90.)
- **Default Value**: `90`
- **Description**: Lower bound before the next ambient attempt. (Seconds, minimum 5; default 90.)
- **Operator Guidance & Impact**: Controls background evaluation frequency. Shorter intervals increase responsiveness at the cost of more frequent world-thread scans; longer intervals conserve server CPU.

### <a id="azerothvoicesrandommaximumintervalseconds"></a>`AzerothVoices.Random.MaximumIntervalSeconds`

- **Data Type**: Integer (Seconds, at least MinimumIntervalSeconds; default 240.)
- **Default Value**: `240`
- **Description**: Upper interval bound. (Seconds, at least MinimumIntervalSeconds; default 240.)
- **Operator Guidance & Impact**: Controls background evaluation frequency. Shorter intervals increase responsiveness at the cost of more frequent world-thread scans; longer intervals conserve server CPU.

### <a id="azerothvoicesrandomfollowupchance"></a>`AzerothVoices.Random.FollowupChance`

- **Data Type**: Percentage (`0` - `100`)
- **Default Value**: `15`
- **Description**: Chance another actor replies after ambient chatter, a bot-originated Say/Yell/group/guild/world/custom reaction, or an event line; the corresponding bot-to-bot or RPG interaction cap is then applied. Every follow-up repeats the selected scope's real-audience rule. (Percentage; default 15.)
- **Operator Guidance & Impact**: Adjust this probability up for chattier, more responsive bots, or down to conserve LLM API tokens on high-population servers.

### <a id="azerothvoicesrandommaximumactors"></a>`AzerothVoices.Random.MaximumActors`

- **Data Type**: Integer
- **Default Value**: `2`
- **Description**: Generated conversation chain cap; 1 disables follow-ups. (Generated turns, minimum 1; default 2.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicesrandomscopes"></a>`AzerothVoices.Random.Scopes`

- **Data Type**: String (Comma-separated list)
- **Default Value**: `guild,world,party`
- **Description**: Chooses ambient-chat delivery scopes; a disabled or unavailable group scope safely falls back to Say. Party is used only when Random selects Party and requires the selected PlayerBot to share a subgroup with an online real player. Ordinary Say chatter is owned by the proximity subsystem (AzerothVoices.Proximity.*); a legacy `say` entry here is routed through that proximity gate instead of the generic NPC path so one Random roll cannot make the same NPC speak twice. Yell uses YellDistance, raid and guild/officer use real membership, World requires one online real player, and channel uses the custom-channel audience gate. (Comma-separated say/yell/party/raid/guild/officer/world/channel; default guild,world,party.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicesrandomprompts"></a>`AzerothVoices.Random.Prompts`

- **Data Type**: String (Pipe-separated choices)
- **Default Value**: `Make a casual observation about the current zone.|Complain briefly about leveling or grinding.|Comment on loot, repairs, travel, professions, quests, dungeons, or battlegrounds.|Make a short joke that fits World of Warcraft.|Mention what you want to do next in the game.`
- **Description**: General ambient topic instructions. (Pipe-separated text choices.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicesrandomquestions"></a>`AzerothVoices.Random.Questions`

- **Data Type**: String (Pipe-separated choices)
- **Default Value**: `Ask whether anyone wants to group.|Ask for an opinion about the current zone.|Ask which dungeon people like.|Ask what people are farming.|Ask about professions, gear, or PvP.`
- **Description**: Question-style ambient instructions. (Pipe-separated text choices.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicesrandomenvironmentprompts"></a>`AzerothVoices.Random.EnvironmentPrompts`

- **Data Type**: String (Pipe-separated choices)
- **Default Value**: `React to something nearby.|Comment on the weather or surroundings.|Mention a creature, object, quest, spell, item, vendor, dungeon, or unfinished task that fits the location.`
- **Description**: Instructions that use live surroundings. (Pipe-separated text choices.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicesrandomguildprompts"></a>`AzerothVoices.Random.GuildPrompts`

- **Data Type**: String (Pipe-separated choices)
- **Default Value**: `Ask whether guild members want a dungeon group.|Congratulate the guild on recent progress.|Talk about raids, PvP, professions, loot, the guild bank, or helping another member.`
- **Description**: Topics used for guild ambient chatter. (Pipe-separated text choices.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicesrandomworldprompts"></a>`AzerothVoices.Random.WorldPrompts`

- **Data Type**: String (Pipe-separated choices)
- **Default Value**: `Start a short world-chat topic about zones, dungeons, loot, professions, PvP, quests, travel, or server life.|Ask world chat a brief useful question.|Make a short public observation that could start a conversation.`
- **Description**: Topics used for world/channel ambient chatter. (Pipe-separated text choices.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

---

## 15. Automated World/custom-channel pacing

| Setting Name | Type / Range | Default | Summary |
| :--- | :--- | :--- | :--- |
| [`AzerothVoices.GeneralChat.Pacing.Enable`](#azerothvoicesgeneralchatpacingenable) | Boolean (`0` or `1`) | `1` | Paces automated World and custom-channel chatter so concurrent exchanges ca... |
| [`AzerothVoices.GeneralChat.MinimumGapSeconds`](#azerothvoicesgeneralchatminimumgapseconds) | Integer (Seconds, 0-86400; default 15.) | `15` | Minimum gap after a reserved multi-line exchange on the same normalized cha... |
| [`AzerothVoices.General.Enable`](#azerothvoicesgeneralenable) | Boolean (`0` or `1`) | `1` | Dedicated General/World ambient chatter system ported from mod-llm-chatter.... |
| [`AzerothVoices.General.TriggerIntervalSeconds`](#azerothvoicesgeneraltriggerintervalseconds) | Integer (Seconds, minimum 5; default 30.) | `30` | Interval between General chatter trigger evaluation passes across zones wit... |
| [`AzerothVoices.General.TriggerChance`](#azerothvoicesgeneraltriggerchance) | Integer | `15` | Base percent chance per zone to trigger a chatter event during an evaluatio... |
| [`AzerothVoices.General.CityMultiplier`](#azerothvoicesgeneralcitymultiplier) | Integer | `2` | Multiplier applied to trigger chance in major capital cities (Stormwind, Ir... |
| [`AzerothVoices.General.ConversationChance`](#azerothvoicesgeneralconversationchance) | Integer | `40` | Chance that a triggered chatter event becomes a two-bot conversation instea... |
| [`AzerothVoices.General.NpcGossipChance`](#azerothvoicesgeneralnpcgossipchance) | Integer | `5` | Chance that the chosen topic gossips about a nearby alive NPC in the zone. ... |
| [`AzerothVoices.General.BotGossipChance`](#azerothvoicesgeneralbotgossipchance) | Integer | `5` | Chance that the chosen topic gossips about another bot in the same zone. (0... |
| [`AzerothVoices.General.GossipTargetCooldownSeconds`](#azerothvoicesgeneralgossiptargetcooldownseconds) | Integer (Seconds; default 1800.) | `1800` | Cooldown before the same NPC or bot can be picked as a gossip subject again... |
| [`AzerothVoices.General.BotSpeakerCooldownSeconds`](#azerothvoicesgeneralbotspeakercooldownseconds) | Integer (Seconds; default 900.) | `900` | Cooldown before the same bot can speak or participate in General channel am... |


### <a id="azerothvoicesgeneralchatpacingenable"></a>`AzerothVoices.GeneralChat.Pacing.Enable`

- **Data Type**: Boolean (`0` or `1`)
- **Default Value**: `1`
- **Description**: Paces automated World and custom-channel chatter so concurrent exchanges cannot arrive as one wall of text. Only this module's scheduled ambient World/custom-channel lines are paced; direct player-directed replies stay immediate, and combat-start, event, boss, and dungeon/raid reactions are never delayed by this timeline. (Default 1.)
- **Operator Guidance & Impact**: Keep enabled (`1`) for standard functionality. Set to `0` to completely bypass this feature and eliminate its processing overhead.

### <a id="azerothvoicesgeneralchatminimumgapseconds"></a>`AzerothVoices.GeneralChat.MinimumGapSeconds`

- **Data Type**: Integer (Seconds, 0-86400; default 15.)
- **Default Value**: `15`
- **Description**: Minimum gap after a reserved multi-line exchange on the same normalized channel plus zone or map/instance identity. (Seconds, 0-86400; default 15.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicesgeneralenable"></a>`AzerothVoices.General.Enable`

- **Data Type**: Boolean (`0` or `1`)
- **Default Value**: `1`
- **Description**: Dedicated General/World ambient chatter system ported from mod-llm-chatter. Enables periodic ambient chatter in General/World channels where real players are present. (Default 1.)
- **Operator Guidance & Impact**: Keep enabled (`1`) for standard functionality. Set to `0` to completely bypass this feature and eliminate its processing overhead.

### <a id="azerothvoicesgeneraltriggerintervalseconds"></a>`AzerothVoices.General.TriggerIntervalSeconds`

- **Data Type**: Integer (Seconds, minimum 5; default 30.)
- **Default Value**: `30`
- **Description**: Interval between General chatter trigger evaluation passes across zones with players. (Seconds, minimum 5; default 30.)
- **Operator Guidance & Impact**: Controls background evaluation frequency. Shorter intervals increase responsiveness at the cost of more frequent world-thread scans; longer intervals conserve server CPU.

### <a id="azerothvoicesgeneraltriggerchance"></a>`AzerothVoices.General.TriggerChance`

- **Data Type**: Integer
- **Default Value**: `15`
- **Description**: Base percent chance per zone to trigger a chatter event during an evaluation pass. (0-100; default 15.)
- **Operator Guidance & Impact**: Adjust this probability up for chattier, more responsive bots, or down to conserve LLM API tokens on high-population servers.

### <a id="azerothvoicesgeneralcitymultiplier"></a>`AzerothVoices.General.CityMultiplier`

- **Data Type**: Integer
- **Default Value**: `2`
- **Description**: Multiplier applied to trigger chance in major capital cities (Stormwind, Ironforge, Orgrimmar, etc.). (1-10; default 2.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicesgeneralconversationchance"></a>`AzerothVoices.General.ConversationChance`

- **Data Type**: Integer
- **Default Value**: `40`
- **Description**: Chance that a triggered chatter event becomes a two-bot conversation instead of a solo observation. (0-100; default 40.)
- **Operator Guidance & Impact**: Adjust this probability up for chattier, more responsive bots, or down to conserve LLM API tokens on high-population servers.

### <a id="azerothvoicesgeneralnpcgossipchance"></a>`AzerothVoices.General.NpcGossipChance`

- **Data Type**: Integer
- **Default Value**: `5`
- **Description**: Chance that the chosen topic gossips about a nearby alive NPC in the zone. (0-100; default 5.)
- **Operator Guidance & Impact**: Adjust this probability up for chattier, more responsive bots, or down to conserve LLM API tokens on high-population servers.

### <a id="azerothvoicesgeneralbotgossipchance"></a>`AzerothVoices.General.BotGossipChance`

- **Data Type**: Integer
- **Default Value**: `5`
- **Description**: Chance that the chosen topic gossips about another bot in the same zone. (0-100; default 5.)
- **Operator Guidance & Impact**: Adjust this probability up for chattier, more responsive bots, or down to conserve LLM API tokens on high-population servers.

### <a id="azerothvoicesgeneralgossiptargetcooldownseconds"></a>`AzerothVoices.General.GossipTargetCooldownSeconds`

- **Data Type**: Integer (Seconds; default 1800.)
- **Default Value**: `1800`
- **Description**: Cooldown before the same NPC or bot can be picked as a gossip subject again. (Seconds; default 1800.)
- **Operator Guidance & Impact**: Controls rate-limiting. Increasing this value prevents repetitive triggers and reduces API call frequency; setting to `0` removes time-based gating for this specific event.

### <a id="azerothvoicesgeneralbotspeakercooldownseconds"></a>`AzerothVoices.General.BotSpeakerCooldownSeconds`

- **Data Type**: Integer (Seconds; default 900.)
- **Default Value**: `900`
- **Description**: Cooldown before the same bot can speak or participate in General channel ambient chatter again. (Seconds; default 900.)
- **Operator Guidance & Impact**: Controls rate-limiting. Increasing this value prevents repetitive triggers and reduces API call frequency; setting to `0` removes time-based gating for this specific event.

---

## 16. PartyGate per-party delivery pacing

| Setting Name | Type / Range | Default | Summary |
| :--- | :--- | :--- | :--- |
| [`AzerothVoices.PartyGate.Enable`](#azerothvoicespartygateenable) | Boolean (`0` or `1`) | `1` | Enables per-party delivery timeline pacing and trigger-based policy gating.... |
| [`AzerothVoices.PartyGate.FillerMinGapSeconds`](#azerothvoicespartygatefillermingapseconds) | Integer (Seconds, 0-300; default 30.) | `30` | Minimum gap between filler chatter messages in the same party. (Seconds, 0-... |
| [`AzerothVoices.PartyGate.ContextualMinGapSeconds`](#azerothvoicespartygatecontextualmingapseconds) | Integer (Seconds, 0-300; default 20.) | `20` | Minimum gap between contextual event chatter messages in the same party. (S... |
| [`AzerothVoices.PartyGate.ResponsiveMinGapSeconds`](#azerothvoicespartygateresponsivemingapseconds) | Integer (Seconds, 0-300; default 4.) | `4` | Minimum gap between direct/responsive chatter messages in the same party. (... |
| [`AzerothVoices.PartyGate.UrgentMinGapSeconds`](#azerothvoicespartygateurgentmingapseconds) | Integer (Seconds, 0-300; default 0.) | `0` | Minimum gap between urgent callout messages in the same party. (Seconds, 0-... |
| [`AzerothVoices.PartyGate.PreLLMDeferThresholdSeconds`](#azerothvoicespartygateprellmdeferthresholdseconds) | Integer (Seconds, 0-60; default 4.) | `4` | Threshold in seconds beyond which pre-LLM filler requests are deferred or d... |
| [`AzerothVoices.PartyGate.MaxFillerDelaySeconds`](#azerothvoicespartygatemaxfillerdelayseconds) | Integer (Seconds, 5-300; default 45.) | `45` | Maximum post-generation delay cap for filler chatter. (Seconds, 5-300; defa... |
| [`AzerothVoices.PartyGate.DebugLog`](#azerothvoicespartygatedebuglog) | Boolean (`0` or `1`) | `0` | Enables debug logging for PartyGate policy decisions and pacing reservation... |


### <a id="azerothvoicespartygateenable"></a>`AzerothVoices.PartyGate.Enable`

- **Data Type**: Boolean (`0` or `1`)
- **Default Value**: `1`
- **Description**: Enables per-party delivery timeline pacing and trigger-based policy gating. (Default 1.)
- **Operator Guidance & Impact**: Keep enabled (`1`) for standard functionality. Set to `0` to completely bypass this feature and eliminate its processing overhead.

### <a id="azerothvoicespartygatefillermingapseconds"></a>`AzerothVoices.PartyGate.FillerMinGapSeconds`

- **Data Type**: Integer (Seconds, 0-300; default 30.)
- **Default Value**: `30`
- **Description**: Minimum gap between filler chatter messages in the same party. (Seconds, 0-300; default 30.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicespartygatecontextualmingapseconds"></a>`AzerothVoices.PartyGate.ContextualMinGapSeconds`

- **Data Type**: Integer (Seconds, 0-300; default 20.)
- **Default Value**: `20`
- **Description**: Minimum gap between contextual event chatter messages in the same party. (Seconds, 0-300; default 20.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicespartygateresponsivemingapseconds"></a>`AzerothVoices.PartyGate.ResponsiveMinGapSeconds`

- **Data Type**: Integer (Seconds, 0-300; default 4.)
- **Default Value**: `4`
- **Description**: Minimum gap between direct/responsive chatter messages in the same party. (Seconds, 0-300; default 4.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicespartygateurgentmingapseconds"></a>`AzerothVoices.PartyGate.UrgentMinGapSeconds`

- **Data Type**: Integer (Seconds, 0-300; default 0.)
- **Default Value**: `0`
- **Description**: Minimum gap between urgent callout messages in the same party. (Seconds, 0-300; default 0.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicespartygateprellmdeferthresholdseconds"></a>`AzerothVoices.PartyGate.PreLLMDeferThresholdSeconds`

- **Data Type**: Integer (Seconds, 0-60; default 4.)
- **Default Value**: `4`
- **Description**: Threshold in seconds beyond which pre-LLM filler requests are deferred or dropped. (Seconds, 0-60; default 4.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicespartygatemaxfillerdelayseconds"></a>`AzerothVoices.PartyGate.MaxFillerDelaySeconds`

- **Data Type**: Integer (Seconds, 5-300; default 45.)
- **Default Value**: `45`
- **Description**: Maximum post-generation delay cap for filler chatter. (Seconds, 5-300; default 45.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicespartygatedebuglog"></a>`AzerothVoices.PartyGate.DebugLog`

- **Data Type**: Boolean (`0` or `1`)
- **Default Value**: `0`
- **Description**: Enables debug logging for PartyGate policy decisions and pacing reservations. (Default 0.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

---

## 17. Lightweight live environment context

| Setting Name | Type / Range | Default | Summary |
| :--- | :--- | :--- | :--- |
| [`AzerothVoices.Environment.Enable`](#azerothvoicesenvironmentenable) | Boolean (`0` or `1`) | `1` | Adds map/zone/area/dungeon/combat/group/guild and bounded context. (Default... |
| [`AzerothVoices.Environment.Distance`](#azerothvoicesenvironmentdistance) | Decimal Yards (Decimal yards, clamped 1-100; default 25.) | `25` | Radius for bounded nearby alive creature names, including friendly, neutral... |
| [`AzerothVoices.Environment.MaximumCreatures`](#azerothvoicesenvironmentmaximumcreatures) | Integer | `5` | Maximum nearby creature names after bounded collection; 0 disables them. (E... |
| [`AzerothVoices.Environment.MaximumItems`](#azerothvoicesenvironmentmaximumitems) | Integer | `8` | Combined equipment/backpack item-name cap; 0 disables item names. (Entries,... |
| [`AzerothVoices.Environment.IncludeEquipment`](#azerothvoicesenvironmentincludeequipment) | Boolean (`0` or `1`) | `1` | Includes bounded PlayerBot equipment names in context. (Default 1.) |
| [`AzerothVoices.Environment.IncludeBackpack`](#azerothvoicesenvironmentincludebackpack) | Boolean (`0` or `1`) | `0` | Includes bounded backpack names; off keeps prompts lightweight. (Default 0.... |


### <a id="azerothvoicesenvironmentenable"></a>`AzerothVoices.Environment.Enable`

- **Data Type**: Boolean (`0` or `1`)
- **Default Value**: `1`
- **Description**: Adds map/zone/area/dungeon/combat/group/guild and bounded context. (Default 1.)
- **Operator Guidance & Impact**: Keep enabled (`1`) for standard functionality. Set to `0` to completely bypass this feature and eliminate its processing overhead.

### <a id="azerothvoicesenvironmentdistance"></a>`AzerothVoices.Environment.Distance`

- **Data Type**: Decimal Yards (Decimal yards, clamped 1-100; default 25.)
- **Default Value**: `25`
- **Description**: Radius for bounded nearby alive creature names, including friendly, neutral, or hostile creatures. (Decimal yards, clamped 1-100; default 25.)
- **Operator Guidance & Impact**: Defines the spatial radius for candidate discovery and line-of-sight checks. Keep aligned with visible WoW client chat bubbles.

### <a id="azerothvoicesenvironmentmaximumcreatures"></a>`AzerothVoices.Environment.MaximumCreatures`

- **Data Type**: Integer
- **Default Value**: `5`
- **Description**: Maximum nearby creature names after bounded collection; 0 disables them. (Entries, clamped 0-50; default 5.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicesenvironmentmaximumitems"></a>`AzerothVoices.Environment.MaximumItems`

- **Data Type**: Integer
- **Default Value**: `8`
- **Description**: Combined equipment/backpack item-name cap; 0 disables item names. (Entries, clamped 0-50; default 8.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicesenvironmentincludeequipment"></a>`AzerothVoices.Environment.IncludeEquipment`

- **Data Type**: Boolean (`0` or `1`)
- **Default Value**: `1`
- **Description**: Includes bounded PlayerBot equipment names in context. (Default 1.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicesenvironmentincludebackpack"></a>`AzerothVoices.Environment.IncludeBackpack`

- **Data Type**: Boolean (`0` or `1`)
- **Default Value**: `0`
- **Description**: Includes bounded backpack names; off keeps prompts lightweight. (Default 0.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

---

## 18. Event chatter

> Automatic vMaNGOS hooks: creature/player kill, death, loot, quest complete, spell learned,
duel request/start/win, level-up, login, guild join, and guild leave.
Automatic & core hooks: pet_defeated, used_object, guild promotion/demotion,
dungeon_completed, and game_event_started/stopped. Hooked natively into engine scripts
and commands, and also triggerable externally or via .av event.
Guild join/leave/login/promotion/demotion and guilded level-up events use Guild and require an
online real guild listener. Other events prioritize Party for PlayerBots in the subject's party
subgroup, then use Say for remaining nearby PlayerBot/NPC responders. Say actors require a real
human within SayDistance; Party actors require a real human in the same party subgroup.


| Setting Name | Type / Range | Default | Summary |
| :--- | :--- | :--- | :--- |
| [`AzerothVoices.Events.Enable`](#azerothvoiceseventsenable) | Boolean (`0` or `1`) | `1` | Master switch for automatic and adapter event reactions. (Default 1.) |
| [`AzerothVoices.Events.ResponderChance`](#azerothvoiceseventsresponderchance) | Percentage (`0` - `100`) | `25` | Second-stage chance that other actors react after the selected scope's real... |
| [`AzerothVoices.Events.SelfCommentChance`](#azerothvoiceseventsselfcommentchance) | Percentage (`0` - `100`) | `5` | Chance an AI PlayerBot comments on its own event through Guild, preferred P... |
| [`AzerothVoices.Events.MaximumResponders`](#azerothvoiceseventsmaximumresponders) | Integer | `2` | Maximum other responders to one event. (Actors, minimum 1; default 2.) |
| [`AzerothVoices.Events.CooldownSeconds`](#azerothvoiceseventscooldownseconds) | Integer (Seconds, minimum 0; default 60.) | `60` | Per-subject/per-event repeat cooldown. (Seconds, minimum 0; default 60.) |
| [`AzerothVoices.Events.Chance.CreatureDefeated`](#azerothvoiceseventschancecreaturedefeated) | Percentage (`0` - `100`) | `8` | Initial chance for an automatic creature-kill event. (Percentage; default 8... |
| [`AzerothVoices.Events.Chance.PlayerDefeated`](#azerothvoiceseventschanceplayerdefeated) | Percentage (`0` - `100`) | `40` | Initial chance for an automatic PvP-kill event. (Percentage; default 40.) |
| [`AzerothVoices.Events.Chance.Died`](#azerothvoiceseventschancedied) | Percentage (`0` - `100`) | `25` | Initial chance for automatic player-death event. (Percentage; default 25.) |
| [`AzerothVoices.Events.Chance.ItemLooted`](#azerothvoiceseventschanceitemlooted) | Percentage (`0` - `100`) | `8` | Initial chance for automatic non-rare loot chatter. (Percentage; default 8.... |
| [`AzerothVoices.Events.Chance.QuestCompleted`](#azerothvoiceseventschancequestcompleted) | Percentage (`0` - `100`) | `25` | Initial chance for automatic quest-completion chatter. (Percentage; default... |
| [`AzerothVoices.Events.Chance.SpellLearned`](#azerothvoiceseventschancespelllearned) | Percentage (`0` - `100`) | `15` | Initial chance for automatic spell-learning chatter. (Percentage; default 1... |
| [`AzerothVoices.Events.Chance.DuelRequested`](#azerothvoiceseventschanceduelrequested) | Percentage (`0` - `100`) | `25` | Initial chance for automatic duel-request chatter. (Percentage; default 25.... |
| [`AzerothVoices.Events.Chance.DuelStarted`](#azerothvoiceseventschanceduelstarted) | Percentage (`0` - `100`) | `20` | Initial chance for automatic duel-start chatter. (Percentage; default 20.) |
| [`AzerothVoices.Events.Chance.DuelWon`](#azerothvoiceseventschanceduelwon) | Percentage (`0` - `100`) | `35` | Initial chance for automatic duel-victory chatter. (Percentage; default 35.... |
| [`AzerothVoices.Events.Chance.LevelUp`](#azerothvoiceseventschancelevelup) | Percentage (`0` - `100`) | `50` | Initial chance for level-up chatter; a guilded subject requires a real guil... |
| [`AzerothVoices.Events.Chance.GuildJoin`](#azerothvoiceseventschanceguildjoin) | Percentage (`0` - `100`) | `60` | Initial chance for automatic Guild chatter on guild join; requires an onlin... |
| [`AzerothVoices.Events.Chance.GuildLeave`](#azerothvoiceseventschanceguildleave) | Percentage (`0` - `100`) | `40` | Initial chance for Guild chatter on guild leave; the old guild id is retain... |
| [`AzerothVoices.Events.Chance.GuildLogin`](#azerothvoiceseventschanceguildlogin) | Percentage (`0` - `100`) | `10` | Initial chance for automatic Guild chatter on guild login; requires an onli... |
| [`AzerothVoices.Events.Chance.GuildPromotion`](#azerothvoiceseventschanceguildpromotion) | Percentage (`0` - `100`) | `50` | Initial chance for adapter-supplied Guild chatter on promotion; requires an... |
| [`AzerothVoices.Events.Chance.GuildDemotion`](#azerothvoiceseventschanceguilddemotion) | Percentage (`0` - `100`) | `30` | Initial chance for adapter-supplied Guild chatter on demotion; requires an ... |
| [`AzerothVoices.Events.Chance.PetDefeated`](#azerothvoiceseventschancepetdefeated) | Percentage (`0` - `100`) | `20` | Initial chance for adapter-supplied pet-defeat chatter. (Percentage; defaul... |
| [`AzerothVoices.Events.Chance.UsedObject`](#azerothvoiceseventschanceusedobject) | Percentage (`0` - `100`) | `10` | Initial chance for adapter-supplied object-use chatter. (Percentage; defaul... |
| [`AzerothVoices.Events.Chance.RareItem`](#azerothvoiceseventschancerareitem) | Percentage (`0` - `100`) | `45` | Initial chance for automatic rare-loot chatter; Party is preferred before S... |
| [`AzerothVoices.Events.Chance.EpicItem`](#azerothvoiceseventschanceepicitem) | Percentage (`0` - `100`) | `75` | Initial chance for automatic epic-loot chatter; Party is preferred before S... |
| [`AzerothVoices.Events.Chance.DungeonCompleted`](#azerothvoiceseventschancedungeoncompleted) | Percentage (`0` - `100`) | `70` | Initial chance for adapter-supplied dungeon completion. (Percentage; defaul... |
| [`AzerothVoices.Events.Chance.GameEventStarted`](#azerothvoiceseventschancegameeventstarted) | Percentage (`0` - `100`) | `50` | Initial chance for adapter game-event start chatter; Party is preferred bef... |
| [`AzerothVoices.Events.Chance.GameEventStopped`](#azerothvoiceseventschancegameeventstopped) | Percentage (`0` - `100`) | `30` | Initial chance for adapter game-event stop chatter; Party is preferred befo... |


### <a id="azerothvoiceseventsenable"></a>`AzerothVoices.Events.Enable`

- **Data Type**: Boolean (`0` or `1`)
- **Default Value**: `1`
- **Description**: Master switch for automatic and adapter event reactions. (Default 1.)
- **Operator Guidance & Impact**: Keep enabled (`1`) for standard functionality. Set to `0` to completely bypass this feature and eliminate its processing overhead.

### <a id="azerothvoiceseventsresponderchance"></a>`AzerothVoices.Events.ResponderChance`

- **Data Type**: Percentage (`0` - `100`)
- **Default Value**: `25`
- **Description**: Second-stage chance that other actors react after the selected scope's real-audience check passes. (Percentage; default 25.)
- **Operator Guidance & Impact**: Adjust this probability up for chattier, more responsive bots, or down to conserve LLM API tokens on high-population servers.

### <a id="azerothvoiceseventsselfcommentchance"></a>`AzerothVoices.Events.SelfCommentChance`

- **Data Type**: Percentage (`0` - `100`)
- **Default Value**: `5`
- **Description**: Chance an AI PlayerBot comments on its own event through Guild, preferred Party, or fallback Say after the selected scope's audience check passes. (Percentage; default 5.)
- **Operator Guidance & Impact**: Adjust this probability up for chattier, more responsive bots, or down to conserve LLM API tokens on high-population servers.

### <a id="azerothvoiceseventsmaximumresponders"></a>`AzerothVoices.Events.MaximumResponders`

- **Data Type**: Integer
- **Default Value**: `2`
- **Description**: Maximum other responders to one event. (Actors, minimum 1; default 2.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoiceseventscooldownseconds"></a>`AzerothVoices.Events.CooldownSeconds`

- **Data Type**: Integer (Seconds, minimum 0; default 60.)
- **Default Value**: `60`
- **Description**: Per-subject/per-event repeat cooldown. (Seconds, minimum 0; default 60.)
- **Operator Guidance & Impact**: Controls rate-limiting. Increasing this value prevents repetitive triggers and reduces API call frequency; setting to `0` removes time-based gating for this specific event.

### <a id="azerothvoiceseventschancecreaturedefeated"></a>`AzerothVoices.Events.Chance.CreatureDefeated`

- **Data Type**: Percentage (`0` - `100`)
- **Default Value**: `8`
- **Description**: Initial chance for an automatic creature-kill event. (Percentage; default 8.)
- **Operator Guidance & Impact**: Adjust this probability up for chattier, more responsive bots, or down to conserve LLM API tokens on high-population servers.

### <a id="azerothvoiceseventschanceplayerdefeated"></a>`AzerothVoices.Events.Chance.PlayerDefeated`

- **Data Type**: Percentage (`0` - `100`)
- **Default Value**: `40`
- **Description**: Initial chance for an automatic PvP-kill event. (Percentage; default 40.)
- **Operator Guidance & Impact**: Adjust this probability up for chattier, more responsive bots, or down to conserve LLM API tokens on high-population servers.

### <a id="azerothvoiceseventschancedied"></a>`AzerothVoices.Events.Chance.Died`

- **Data Type**: Percentage (`0` - `100`)
- **Default Value**: `25`
- **Description**: Initial chance for automatic player-death event. (Percentage; default 25.)
- **Operator Guidance & Impact**: Adjust this probability up for chattier, more responsive bots, or down to conserve LLM API tokens on high-population servers.

### <a id="azerothvoiceseventschanceitemlooted"></a>`AzerothVoices.Events.Chance.ItemLooted`

- **Data Type**: Percentage (`0` - `100`)
- **Default Value**: `8`
- **Description**: Initial chance for automatic non-rare loot chatter. (Percentage; default 8.)
- **Operator Guidance & Impact**: Adjust this probability up for chattier, more responsive bots, or down to conserve LLM API tokens on high-population servers.

### <a id="azerothvoiceseventschancequestcompleted"></a>`AzerothVoices.Events.Chance.QuestCompleted`

- **Data Type**: Percentage (`0` - `100`)
- **Default Value**: `25`
- **Description**: Initial chance for automatic quest-completion chatter. (Percentage; default 25.)
- **Operator Guidance & Impact**: Adjust this probability up for chattier, more responsive bots, or down to conserve LLM API tokens on high-population servers.

### <a id="azerothvoiceseventschancespelllearned"></a>`AzerothVoices.Events.Chance.SpellLearned`

- **Data Type**: Percentage (`0` - `100`)
- **Default Value**: `15`
- **Description**: Initial chance for automatic spell-learning chatter. (Percentage; default 15.)
- **Operator Guidance & Impact**: Adjust this probability up for chattier, more responsive bots, or down to conserve LLM API tokens on high-population servers.

### <a id="azerothvoiceseventschanceduelrequested"></a>`AzerothVoices.Events.Chance.DuelRequested`

- **Data Type**: Percentage (`0` - `100`)
- **Default Value**: `25`
- **Description**: Initial chance for automatic duel-request chatter. (Percentage; default 25.)
- **Operator Guidance & Impact**: Adjust this probability up for chattier, more responsive bots, or down to conserve LLM API tokens on high-population servers.

### <a id="azerothvoiceseventschanceduelstarted"></a>`AzerothVoices.Events.Chance.DuelStarted`

- **Data Type**: Percentage (`0` - `100`)
- **Default Value**: `20`
- **Description**: Initial chance for automatic duel-start chatter. (Percentage; default 20.)
- **Operator Guidance & Impact**: Adjust this probability up for chattier, more responsive bots, or down to conserve LLM API tokens on high-population servers.

### <a id="azerothvoiceseventschanceduelwon"></a>`AzerothVoices.Events.Chance.DuelWon`

- **Data Type**: Percentage (`0` - `100`)
- **Default Value**: `35`
- **Description**: Initial chance for automatic duel-victory chatter. (Percentage; default 35.)
- **Operator Guidance & Impact**: Adjust this probability up for chattier, more responsive bots, or down to conserve LLM API tokens on high-population servers.

### <a id="azerothvoiceseventschancelevelup"></a>`AzerothVoices.Events.Chance.LevelUp`

- **Data Type**: Percentage (`0` - `100`)
- **Default Value**: `50`
- **Description**: Initial chance for level-up chatter; a guilded subject requires a real guild listener and uses Guild, otherwise Party is preferred before Say. (Percentage; default 50.)
- **Operator Guidance & Impact**: Adjust this probability up for chattier, more responsive bots, or down to conserve LLM API tokens on high-population servers.

### <a id="azerothvoiceseventschanceguildjoin"></a>`AzerothVoices.Events.Chance.GuildJoin`

- **Data Type**: Percentage (`0` - `100`)
- **Default Value**: `60`
- **Description**: Initial chance for automatic Guild chatter on guild join; requires an online real member in that guild. (Percentage; default 60.)
- **Operator Guidance & Impact**: Adjust this probability up for chattier, more responsive bots, or down to conserve LLM API tokens on high-population servers.

### <a id="azerothvoiceseventschanceguildleave"></a>`AzerothVoices.Events.Chance.GuildLeave`

- **Data Type**: Percentage (`0` - `100`)
- **Default Value**: `40`
- **Description**: Initial chance for Guild chatter on guild leave; the old guild id is retained and an online real member must remain in that guild. (Percentage; default 40.)
- **Operator Guidance & Impact**: Adjust this probability up for chattier, more responsive bots, or down to conserve LLM API tokens on high-population servers.

### <a id="azerothvoiceseventschanceguildlogin"></a>`AzerothVoices.Events.Chance.GuildLogin`

- **Data Type**: Percentage (`0` - `100`)
- **Default Value**: `10`
- **Description**: Initial chance for automatic Guild chatter on guild login; requires an online real member in that guild. (Percentage; default 10.)
- **Operator Guidance & Impact**: Adjust this probability up for chattier, more responsive bots, or down to conserve LLM API tokens on high-population servers.

### <a id="azerothvoiceseventschanceguildpromotion"></a>`AzerothVoices.Events.Chance.GuildPromotion`

- **Data Type**: Percentage (`0` - `100`)
- **Default Value**: `50`
- **Description**: Initial chance for adapter-supplied Guild chatter on promotion; requires an online real member in that guild. (Percentage; default 50.)
- **Operator Guidance & Impact**: Adjust this probability up for chattier, more responsive bots, or down to conserve LLM API tokens on high-population servers.

### <a id="azerothvoiceseventschanceguilddemotion"></a>`AzerothVoices.Events.Chance.GuildDemotion`

- **Data Type**: Percentage (`0` - `100`)
- **Default Value**: `30`
- **Description**: Initial chance for adapter-supplied Guild chatter on demotion; requires an online real member in that guild. (Percentage; default 30.)
- **Operator Guidance & Impact**: Adjust this probability up for chattier, more responsive bots, or down to conserve LLM API tokens on high-population servers.

### <a id="azerothvoiceseventschancepetdefeated"></a>`AzerothVoices.Events.Chance.PetDefeated`

- **Data Type**: Percentage (`0` - `100`)
- **Default Value**: `20`
- **Description**: Initial chance for adapter-supplied pet-defeat chatter. (Percentage; default 20.)
- **Operator Guidance & Impact**: Adjust this probability up for chattier, more responsive bots, or down to conserve LLM API tokens on high-population servers.

### <a id="azerothvoiceseventschanceusedobject"></a>`AzerothVoices.Events.Chance.UsedObject`

- **Data Type**: Percentage (`0` - `100`)
- **Default Value**: `10`
- **Description**: Initial chance for adapter-supplied object-use chatter. (Percentage; default 10.)
- **Operator Guidance & Impact**: Adjust this probability up for chattier, more responsive bots, or down to conserve LLM API tokens on high-population servers.

### <a id="azerothvoiceseventschancerareitem"></a>`AzerothVoices.Events.Chance.RareItem`

- **Data Type**: Percentage (`0` - `100`)
- **Default Value**: `45`
- **Description**: Initial chance for automatic rare-loot chatter; Party is preferred before Say. (Percentage; default 45.)
- **Operator Guidance & Impact**: Adjust this probability up for chattier, more responsive bots, or down to conserve LLM API tokens on high-population servers.

### <a id="azerothvoiceseventschanceepicitem"></a>`AzerothVoices.Events.Chance.EpicItem`

- **Data Type**: Percentage (`0` - `100`)
- **Default Value**: `75`
- **Description**: Initial chance for automatic epic-loot chatter; Party is preferred before Say. (Percentage; default 75.)
- **Operator Guidance & Impact**: Adjust this probability up for chattier, more responsive bots, or down to conserve LLM API tokens on high-population servers.

### <a id="azerothvoiceseventschancedungeoncompleted"></a>`AzerothVoices.Events.Chance.DungeonCompleted`

- **Data Type**: Percentage (`0` - `100`)
- **Default Value**: `70`
- **Description**: Initial chance for adapter-supplied dungeon completion. (Percentage; default 70.)
- **Operator Guidance & Impact**: Adjust this probability up for chattier, more responsive bots, or down to conserve LLM API tokens on high-population servers.

### <a id="azerothvoiceseventschancegameeventstarted"></a>`AzerothVoices.Events.Chance.GameEventStarted`

- **Data Type**: Percentage (`0` - `100`)
- **Default Value**: `50`
- **Description**: Initial chance for adapter game-event start chatter; Party is preferred before Say. (Percentage; default 50.)
- **Operator Guidance & Impact**: Adjust this probability up for chattier, more responsive bots, or down to conserve LLM API tokens on high-population servers.

### <a id="azerothvoiceseventschancegameeventstopped"></a>`AzerothVoices.Events.Chance.GameEventStopped`

- **Data Type**: Percentage (`0` - `100`)
- **Default Value**: `30`
- **Description**: Initial chance for adapter game-event stop chatter; Party is preferred before Say. (Percentage; default 30.)
- **Operator Guidance & Impact**: Adjust this probability up for chattier, more responsive bots, or down to conserve LLM API tokens on high-population servers.

---

## 19. Reply length and PlayerBots-compatible typing delay

| Setting Name | Type / Range | Default | Summary |
| :--- | :--- | :--- | :--- |
| [`AzerothVoices.Typing.Enable`](#azerothvoicestypingenable) | Boolean (`0` or `1`) | `1` | Delays delivery so generated chat appears naturally typed. (Default 1.) |
| [`AzerothVoices.Typing.BaseDelayMilliseconds`](#azerothvoicestypingbasedelaymilliseconds) | Integer (Milliseconds, minimum 0; default 0.) | `0` | Fixed delay added to each line. (Milliseconds, minimum 0; default 0.) |
| [`AzerothVoices.Typing.MinimumDelayPerCharacterMilliseconds`](#azerothvoicestypingminimumdelaypercharactermilliseconds) | Integer | `100` | Minimum natural typing delay; one speed is randomly selected between the mi... |
| [`AzerothVoices.Typing.MaximumDelayPerCharacterMilliseconds`](#azerothvoicestypingmaximumdelaypercharactermilliseconds) | Integer | `200` | Maximum natural typing delay; values below the minimum are swapped safely. ... |
| [`AzerothVoices.Typing.SubtractGenerationTime`](#azerothvoicestypingsubtractgenerationtime) | Boolean (`0` or `1`) | `1` | Legacy compatibility: if both range keys above are absent, AzerothVoices.Ty... |
| [`AzerothVoices.Reply.MaximumCharacters`](#azerothvoicesreplymaximumcharacters) | Integer (Characters, minimum 1; default 140.) | `140` | Cumulative total character budget across all delivered reply lines for a... |
| [`AzerothVoices.Reply.MaximumLines`](#azerothvoicesreplymaximumlines) | Integer | `2` | Maximum delivered lines/parts per request. Limits the number of separate... |
| [`AzerothVoices.Reply.SentenceAwareSplitting`](#azerothvoicesreplysentenceawaresplitting) | Boolean (`0` or `1`) | `1` | Enables structured, sentence-aware line breaking for delivered dialogue. Wh... |


### <a id="azerothvoicestypingenable"></a>`AzerothVoices.Typing.Enable`

- **Data Type**: Boolean (`0` or `1`)
- **Default Value**: `1`
- **Description**: Delays delivery so generated chat appears naturally typed. (Default 1.)
- **Operator Guidance & Impact**: Keep enabled (`1`) for standard functionality. Set to `0` to completely bypass this feature and eliminate its processing overhead.

### <a id="azerothvoicestypingbasedelaymilliseconds"></a>`AzerothVoices.Typing.BaseDelayMilliseconds`

- **Data Type**: Integer (Milliseconds, minimum 0; default 0.)
- **Default Value**: `0`
- **Description**: Fixed delay added to each line. (Milliseconds, minimum 0; default 0.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicestypingminimumdelaypercharactermilliseconds"></a>`AzerothVoices.Typing.MinimumDelayPerCharacterMilliseconds`

- **Data Type**: Integer
- **Default Value**: `100`
- **Description**: Minimum natural typing delay; one speed is randomly selected between the minimum and maximum for each delivered reply line. (Ms/character, minimum 0; default 100.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicestypingmaximumdelaypercharactermilliseconds"></a>`AzerothVoices.Typing.MaximumDelayPerCharacterMilliseconds`

- **Data Type**: Integer
- **Default Value**: `200`
- **Description**: Maximum natural typing delay; values below the minimum are swapped safely. (Ms/character, minimum 0; default 200.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicestypingsubtractgenerationtime"></a>`AzerothVoices.Typing.SubtractGenerationTime`

- **Data Type**: Boolean (`0` or `1`)
- **Default Value**: `1`
- **Description**: Legacy compatibility: if both range keys above are absent, AzerothVoices.Typing.DelayPerCharacterMilliseconds is still accepted as one fixed per-character delay. Subtracts provider generation time from the simulated typing delay, preserving the restored PlayerBots-style delivery timing. (Default 1.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicesreplymaximumcharacters"></a>`AzerothVoices.Reply.MaximumCharacters`

- **Data Type**: Integer (Characters, minimum 1; default 140.)
- **Default Value**: `140`
- **Description**: Cumulative total character budget across all delivered reply lines for a single request. Messages are cleanly split on sentence and clause boundaries up to this cumulative character budget. (Characters, minimum 1; default 140.)
- **Operator Guidance & Impact**: Enforces the maximum combined character length of a bot's delivered response across all lines. Lower values keep bot replies short, punchy, and non-spammy; higher values allow longer multi-sentence thoughts.

### <a id="azerothvoicesreplymaximumlines"></a>`AzerothVoices.Reply.MaximumLines`

- **Data Type**: Integer
- **Default Value**: `2`
- **Description**: Maximum delivered lines/parts per request. Limits the number of separate chat messages a bot can send per response turn. (Lines, minimum 1; default 2.)
- **Operator Guidance & Impact**: Caps how many separate in-game chat messages/bubbles a bot can send for a single dialogue turn. Setting to 1 ensures single-line responses; setting to 2 allows short two-line responses when dialogue warrants it.

### <a id="azerothvoicesreplysentenceawaresplitting"></a>`AzerothVoices.Reply.SentenceAwareSplitting`

- **Data Type**: Boolean (`0` or `1`)
- **Default Value**: `1`
- **Description**: Enables structured, sentence-aware line breaking for delivered dialogue. When enabled, replies are cleanly split on natural sentence boundaries (.!?) and clause punctuation (,;-) instead of greedily packing characters and breaking in the middle of sentences across messages. (Default 1.)
- **Operator Guidance & Impact**: Keep enabled (`1`) to prevent fragmented replies where the start of the next sentence is chopped across messages. Set to `0` only if you need legacy character-greedy splitting.

---

## 20. Bounded conversation and surrounding-chat history

| Setting Name | Type / Range | Default | Summary |
| :--- | :--- | :--- | :--- |
| [`AzerothVoices.History.StorageMode`](#azerothvoiceshistorystoragemode) | Integer | `2` | 0 disables conversation history, 1 keeps bounded history only in RAM until ... |
| [`AzerothVoices.History.DatabaseMaximumTurns`](#azerothvoiceshistorydatabasemaximumturns) | Integer | `20` | --- SQL MODE SETTINGS (StorageMode = 2) --- Used only when StorageMode = 2 ... |
| [`AzerothVoices.History.DatabaseTTLMinutes`](#azerothvoiceshistorydatabasettlminutes) | Integer | `10080` | Used only when StorageMode = 2 (SQL); minutes, default 10080 (7 days). Olde... |
| [`AzerothVoices.History.DatabaseFlushSeconds`](#azerothvoiceshistorydatabaseflushseconds) | Integer (Seconds) | `5` | Used only when a StorageMode = 2 (SQL) feature has queued rows; seconds, de... |
| [`AzerothVoices.History.DatabaseFlushBatchSize`](#azerothvoiceshistorydatabaseflushbatchsize) | Integer | `20` | Used only when a StorageMode = 2 (SQL) feature has queued rows; rows, defau... |
| [`AzerothVoices.History.RamMaximumTurns`](#azerothvoiceshistoryrammaximumturns) | Integer | `6` | --- RAM MODE SETTINGS (StorageMode = 1) --- Used only when StorageMode = 1 ... |
| [`AzerothVoices.History.TTLMinutes`](#azerothvoiceshistoryttlminutes) | Integer | `30` | Prompt-age limit for conversation turns in RAM or the SQL hot cache. (Minut... |
| [`AzerothVoices.History.MaximumCharacters`](#azerothvoiceshistorymaximumcharacters) | Integer (Characters, minimum 128; default 2500.) | `2500` | Hard rendered conversation-history cap per provider request. (Characters, m... |
| [`AzerothVoices.History.MaximumConversations`](#azerothvoiceshistorymaximumconversations) | Integer | `2048` | Hard bound on in-memory conversation buckets in StorageMode 1 or 2. (Keys, ... |
| [`AzerothVoices.History.HeaderTemplate`](#azerothvoiceshistoryheadertemplate) | String (Template with placeholders) | `Recent conversation with <sender name>, use it only as context:` | Introduces previous conversation context. (Text template.) |
| [`AzerothVoices.History.LineTemplate`](#azerothvoiceshistorylinetemplate) | String (Template with placeholders) | `<sender name>: <sender message>\n<bot name>: <bot reply>\n` | Renders each stored exchange. (Text template with \n escapes.) |
| [`AzerothVoices.History.FooterTemplate`](#azerothvoiceshistoryfootertemplate) | String (Template with placeholders) | `New message from <sender name>: <initial message>` | Separates history from the new incoming message. (Text template.) |
| [`AzerothVoices.SurroundingChat.Enable`](#azerothvoicessurroundingchatenable) | Boolean (`0` or `1`) | `1` | Adds recent lines only from the same whisper, group, guild, channel, or loc... |
| [`AzerothVoices.SurroundingChat.MaximumLines`](#azerothvoicessurroundingchatmaximumlines) | Integer | `8` | Hard per-scope recent-line cap. (Lines, minimum 0; default 8.) |
| [`AzerothVoices.SurroundingChat.TTLMinutes`](#azerothvoicessurroundingchatttlminutes) | Integer | `5` | Discards stale surrounding lines. (Minutes, minimum 1; default 5.) |
| [`AzerothVoices.SurroundingChat.MaximumCharacters`](#azerothvoicessurroundingchatmaximumcharacters) | Integer (Characters, minimum 128; default 1200.) | `1200` | Rendered surrounding-context cap. (Characters, minimum 128; default 1200.) |
| [`AzerothVoices.SurroundingChat.MaximumScopes`](#azerothvoicessurroundingchatmaximumscopes) | Integer | `512` | Hard bound on RAM scope buckets. (Scopes, minimum 1; default 512.) |


### <a id="azerothvoiceshistorystoragemode"></a>`AzerothVoices.History.StorageMode`

- **Data Type**: Integer
- **Default Value**: `2`
- **Description**: 0 disables conversation history, 1 keeps bounded history only in RAM until restart, and 2 persists it in the character database with a bounded lazy-loaded RAM cache (falling back to RAM if the module tables are unavailable). (Mode 0/1/2; default 2.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoiceshistorydatabasemaximumturns"></a>`AzerothVoices.History.DatabaseMaximumTurns`

- **Data Type**: Integer
- **Default Value**: `20`
- **Description**: --- SQL MODE SETTINGS (StorageMode = 2) --- Used only when StorageMode = 2 (SQL); turns, default 20. Caps persistent rows and lazy-loaded hot-cache turns per conversation.
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoiceshistorydatabasettlminutes"></a>`AzerothVoices.History.DatabaseTTLMinutes`

- **Data Type**: Integer
- **Default Value**: `10080`
- **Description**: Used only when StorageMode = 2 (SQL); minutes, default 10080 (7 days). Older persistent conversation turns are not loaded and are deleted by cleanup.
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoiceshistorydatabaseflushseconds"></a>`AzerothVoices.History.DatabaseFlushSeconds`

- **Data Type**: Integer (Seconds)
- **Default Value**: `5`
- **Description**: Used only when a StorageMode = 2 (SQL) feature has queued rows; seconds, default 5. Sets the maximum delay before batched history/snapshot writes flush.
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoiceshistorydatabaseflushbatchsize"></a>`AzerothVoices.History.DatabaseFlushBatchSize`

- **Data Type**: Integer
- **Default Value**: `20`
- **Description**: Used only when a StorageMode = 2 (SQL) feature has queued rows; rows, default 20. Flushes the shared history/snapshot transaction when this many rows wait.
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoiceshistoryrammaximumturns"></a>`AzerothVoices.History.RamMaximumTurns`

- **Data Type**: Integer
- **Default Value**: `6`
- **Description**: --- RAM MODE SETTINGS (StorageMode = 1) --- Used only when StorageMode = 1 (RAM); turns, default 6. Caps retained turns per conversation until worldserver restarts.
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoiceshistoryttlminutes"></a>`AzerothVoices.History.TTLMinutes`

- **Data Type**: Integer
- **Default Value**: `30`
- **Description**: Prompt-age limit for conversation turns in RAM or the SQL hot cache. (Minutes, minimum 1; default 30.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoiceshistorymaximumcharacters"></a>`AzerothVoices.History.MaximumCharacters`

- **Data Type**: Integer (Characters, minimum 128; default 2500.)
- **Default Value**: `2500`
- **Description**: Hard rendered conversation-history cap per provider request. (Characters, minimum 128; default 2500.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoiceshistorymaximumconversations"></a>`AzerothVoices.History.MaximumConversations`

- **Data Type**: Integer
- **Default Value**: `2048`
- **Description**: Hard bound on in-memory conversation buckets in StorageMode 1 or 2. (Keys, minimum 1; default 2048.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoiceshistoryheadertemplate"></a>`AzerothVoices.History.HeaderTemplate`

- **Data Type**: String (Template with placeholders)
- **Default Value**: `Recent conversation with <sender name>, use it only as context:`
- **Description**: Introduces previous conversation context. (Text template.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoiceshistorylinetemplate"></a>`AzerothVoices.History.LineTemplate`

- **Data Type**: String (Template with placeholders)
- **Default Value**: `<sender name>: <sender message>\n<bot name>: <bot reply>\n`
- **Description**: Renders each stored exchange. (Text template with \n escapes.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoiceshistoryfootertemplate"></a>`AzerothVoices.History.FooterTemplate`

- **Data Type**: String (Template with placeholders)
- **Default Value**: `New message from <sender name>: <initial message>`
- **Description**: Separates history from the new incoming message. (Text template.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicessurroundingchatenable"></a>`AzerothVoices.SurroundingChat.Enable`

- **Data Type**: Boolean (`0` or `1`)
- **Default Value**: `1`
- **Description**: Adds recent lines only from the same whisper, group, guild, channel, or local map-area scope so references such as "him" or "that" retain context. (Default 1.)
- **Operator Guidance & Impact**: Keep enabled (`1`) for standard functionality. Set to `0` to completely bypass this feature and eliminate its processing overhead.

### <a id="azerothvoicessurroundingchatmaximumlines"></a>`AzerothVoices.SurroundingChat.MaximumLines`

- **Data Type**: Integer
- **Default Value**: `8`
- **Description**: Hard per-scope recent-line cap. (Lines, minimum 0; default 8.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicessurroundingchatttlminutes"></a>`AzerothVoices.SurroundingChat.TTLMinutes`

- **Data Type**: Integer
- **Default Value**: `5`
- **Description**: Discards stale surrounding lines. (Minutes, minimum 1; default 5.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicessurroundingchatmaximumcharacters"></a>`AzerothVoices.SurroundingChat.MaximumCharacters`

- **Data Type**: Integer (Characters, minimum 128; default 1200.)
- **Default Value**: `1200`
- **Description**: Rendered surrounding-context cap. (Characters, minimum 128; default 1200.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicessurroundingchatmaximumscopes"></a>`AzerothVoices.SurroundingChat.MaximumScopes`

- **Data Type**: Integer
- **Default Value**: `512`
- **Description**: Hard bound on RAM scope buckets. (Scopes, minimum 1; default 512.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

---

## 21. SNAPSHOT SYSTEM

| Setting Name | Type / Range | Default | Summary |
| :--- | :--- | :--- | :--- |
| [`AzerothVoices.Snapshot.Enable`](#azerothvoicessnapshotenable) | Boolean (`0` or `1`) | `0` | When enabled, adds a richer PlayerBot-only world-thread block with combat, ... |
| [`AzerothVoices.Snapshot.IncludeCombat`](#azerothvoicessnapshotincludecombat) | Boolean (`0` or `1`) | `1` | Adds current health, active power resource, combat state, victim name/level... |
| [`AzerothVoices.Snapshot.IncludeGroup`](#azerothvoicessnapshotincludegroup) | Boolean (`0` or `1`) | `1` | Adds bounded same-map group members with class, race, health, distance, and... |
| [`AzerothVoices.Snapshot.IncludeSpells`](#azerothvoicessnapshotincludespells) | Boolean (`0` or `1`) | `1` | Adds bounded active known spells after removing passive/internal entries an... |
| [`AzerothVoices.Snapshot.IncludeQuests`](#azerothvoicessnapshotincludequests) | Boolean (`0` or `1`) | `1` | Adds bounded quest-log titles and whether each active quest is in progress,... |
| [`AzerothVoices.Snapshot.IncludeLineOfSight`](#azerothvoicessnapshotincludelineofsight) | Boolean (`0` or `1`) | `1` | Adds bounded nearby creatures and spawned game objects that pass the bot's ... |
| [`AzerothVoices.Snapshot.IncludeNearbyPlayers`](#azerothvoicessnapshotincludenearbyplayers) | Boolean (`0` or `1`) | `1` | Adds bounded non-GM players and PlayerBots within range and line of sight w... |
| [`AzerothVoices.Snapshot.Distance`](#azerothvoicessnapshotdistance) | Decimal Yards (Decimal yards, clamped 1-100; default 40.) | `40` | Shared bounded search radius for snapshot creature, game-object, and nearby... |
| [`AzerothVoices.Snapshot.MaximumGroupMembers`](#azerothvoicessnapshotmaximumgroupmembers) | Integer | `5` | Maximum other group members rendered in the current snapshot; 0 omits the g... |
| [`AzerothVoices.Snapshot.MaximumSpells`](#azerothvoicessnapshotmaximumspells) | Integer | `20` | Maximum deduplicated highest-rank usable spell names rendered in the curren... |
| [`AzerothVoices.Snapshot.MaximumQuests`](#azerothvoicessnapshotmaximumquests) | Integer | `10` | Maximum occupied quest-log entries rendered in the current snapshot; 0 omit... |
| [`AzerothVoices.Snapshot.MaximumCreatures`](#azerothvoicessnapshotmaximumcreatures) | Integer | `10` | Maximum line-of-sight creatures rendered after bounded collection and neare... |
| [`AzerothVoices.Snapshot.MaximumGameObjects`](#azerothvoicessnapshotmaximumgameobjects) | Integer | `8` | Maximum spawned line-of-sight game objects rendered after bounded collectio... |
| [`AzerothVoices.Snapshot.MaximumPlayers`](#azerothvoicessnapshotmaximumplayers) | Integer | `8` | Maximum non-GM nearby players rendered after bounded collection and nearest... |
| [`AzerothVoices.Snapshot.MaximumCharacters`](#azerothvoicessnapshotmaximumcharacters) | Integer (Characters, clamped 256-16000; default 3500.) | `3500` | Hard cap on the complete current detailed snapshot before it enters the req... |
| [`AzerothVoices.Snapshot.PromptTemplate`](#azerothvoicessnapshotprompttemplate) | String (Template with placeholders) | `"CURRENT PLAYERBOT SNAPSHOT:\n{combat}\n{group}\n{spells}\n{quests}\n{line_of_sight}\n{nearby_players}"` | Replaces {combat}, {group}, {spells}, {quests}, {line_of_sight}, and {nearb... |
| [`AzerothVoices.Snapshot.StorageMode`](#azerothvoicessnapshotstoragemode) | Integer | `0` | Controls only older delivered snapshots: 0 stores none while the current sn... |
| [`AzerothVoices.Snapshot.DatabaseMaximumSnapshots`](#azerothvoicessnapshotdatabasemaximumsnapshots) | Integer | `10` | --- SQL MODE SETTINGS (StorageMode = 2) --- Used only when StorageMode = 2 ... |
| [`AzerothVoices.Snapshot.DatabaseTTLMinutes`](#azerothvoicessnapshotdatabasettlminutes) | Integer | `10080` | Used only when StorageMode = 2 (SQL); minutes, default 10080 (7 days). Olde... |
| [`AzerothVoices.Snapshot.RamMaximumSnapshots`](#azerothvoicessnapshotrammaximumsnapshots) | Integer | `3` | --- RAM MODE SETTINGS (StorageMode = 1) --- Used only when StorageMode = 1 ... |
| [`AzerothVoices.Snapshot.HistoryTTLMinutes`](#azerothvoicessnapshothistoryttlminutes) | Integer | `30` | Prompt-age limit for older snapshots in RAM or the SQL hot cache; the curre... |
| [`AzerothVoices.Snapshot.HistoryMaximumCharacters`](#azerothvoicessnapshothistorymaximumcharacters) | Integer (Characters, minimum 128; default 1600.) | `1600` | Hard combined prompt cap for older snapshots, separate from the richer curr... |
| [`AzerothVoices.Snapshot.HistoryMaximumActors`](#azerothvoicessnapshothistorymaximumactors) | Integer | `2048` | Hard bound on in-memory PlayerBot snapshot-history buckets in StorageMode 1... |


### <a id="azerothvoicessnapshotenable"></a>`AzerothVoices.Snapshot.Enable`

- **Data Type**: Boolean (`0` or `1`)
- **Default Value**: `0`
- **Description**: When enabled, adds a richer PlayerBot-only world-thread block with combat, group, spells, quests, visible objects, and nearby players; disabling omits it, while enabling increases prompt tokens and may increase provider response time. (Default 0.)
- **Operator Guidance & Impact**: Disabled (`0`) by default. Enable (`1`) only if you wish to activate this optional subsystem.

### <a id="azerothvoicessnapshotincludecombat"></a>`AzerothVoices.Snapshot.IncludeCombat`

- **Data Type**: Boolean (`0` or `1`)
- **Default Value**: `1`
- **Description**: Adds current health, active power resource, combat state, victim name/level/health, and whether the bot is fighting. (Default 1.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicessnapshotincludegroup"></a>`AzerothVoices.Snapshot.IncludeGroup`

- **Data Type**: Boolean (`0` or `1`)
- **Default Value**: `1`
- **Description**: Adds bounded same-map group members with class, race, health, distance, and current fighting state. (Default 1.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicessnapshotincludespells"></a>`AzerothVoices.Snapshot.IncludeSpells`

- **Data Type**: Boolean (`0` or `1`)
- **Default Value**: `1`
- **Description**: Adds bounded active known spells after removing passive/internal entries and keeping the highest useful rank for each spell name. (Default 1.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicessnapshotincludequests"></a>`AzerothVoices.Snapshot.IncludeQuests`

- **Data Type**: Boolean (`0` or `1`)
- **Default Value**: `1`
- **Description**: Adds bounded quest-log titles and whether each active quest is in progress, complete, or failed. (Default 1.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicessnapshotincludelineofsight"></a>`AzerothVoices.Snapshot.IncludeLineOfSight`

- **Data Type**: Boolean (`0` or `1`)
- **Default Value**: `1`
- **Description**: Adds bounded nearby creatures and spawned game objects that pass the bot's live line-of-sight check. (Default 1.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicessnapshotincludenearbyplayers"></a>`AzerothVoices.Snapshot.IncludeNearbyPlayers`

- **Data Type**: Boolean (`0` or `1`)
- **Default Value**: `1`
- **Description**: Adds bounded non-GM players and PlayerBots within range and line of sight with level, race, class, faction, distance, and actor type. (Default 1.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicessnapshotdistance"></a>`AzerothVoices.Snapshot.Distance`

- **Data Type**: Decimal Yards (Decimal yards, clamped 1-100; default 40.)
- **Default Value**: `40`
- **Description**: Shared bounded search radius for snapshot creature, game-object, and nearby-player collection; group distance is reported for same-map members. (Decimal yards, clamped 1-100; default 40.)
- **Operator Guidance & Impact**: Defines the spatial radius for candidate discovery and line-of-sight checks. Keep aligned with visible WoW client chat bubbles.

### <a id="azerothvoicessnapshotmaximumgroupmembers"></a>`AzerothVoices.Snapshot.MaximumGroupMembers`

- **Data Type**: Integer
- **Default Value**: `5`
- **Description**: Maximum other group members rendered in the current snapshot; 0 omits the group-member list. (Entries, clamped 0-40; default 5.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicessnapshotmaximumspells"></a>`AzerothVoices.Snapshot.MaximumSpells`

- **Data Type**: Integer
- **Default Value**: `20`
- **Description**: Maximum deduplicated highest-rank usable spell names rendered in the current snapshot; 0 omits spells. (Entries, clamped 0-100; default 20.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicessnapshotmaximumquests"></a>`AzerothVoices.Snapshot.MaximumQuests`

- **Data Type**: Integer
- **Default Value**: `10`
- **Description**: Maximum occupied quest-log entries rendered in the current snapshot; 0 omits quests. (Entries, clamped 0-20; default 10.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicessnapshotmaximumcreatures"></a>`AzerothVoices.Snapshot.MaximumCreatures`

- **Data Type**: Integer
- **Default Value**: `10`
- **Description**: Maximum line-of-sight creatures rendered after bounded collection and nearest-first sorting; 0 omits creatures. (Entries, clamped 0-50; default 10.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicessnapshotmaximumgameobjects"></a>`AzerothVoices.Snapshot.MaximumGameObjects`

- **Data Type**: Integer
- **Default Value**: `8`
- **Description**: Maximum spawned line-of-sight game objects rendered after bounded collection and nearest-first sorting; 0 omits game objects. (Entries, clamped 0-50; default 8.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicessnapshotmaximumplayers"></a>`AzerothVoices.Snapshot.MaximumPlayers`

- **Data Type**: Integer
- **Default Value**: `8`
- **Description**: Maximum non-GM nearby players rendered after bounded collection and nearest-first sorting; 0 omits players. (Entries, clamped 0-50; default 8.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicessnapshotmaximumcharacters"></a>`AzerothVoices.Snapshot.MaximumCharacters`

- **Data Type**: Integer (Characters, clamped 256-16000; default 3500.)
- **Default Value**: `3500`
- **Description**: Hard cap on the complete current detailed snapshot before it enters the request context budget. (Characters, clamped 256-16000; default 3500.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicessnapshotprompttemplate"></a>`AzerothVoices.Snapshot.PromptTemplate`

- **Data Type**: String (Template with placeholders)
- **Default Value**: `"CURRENT PLAYERBOT SNAPSHOT:\n{combat}\n{group}\n{spells}\n{quests}\n{line_of_sight}\n{nearby_players}"`
- **Description**: Replaces {combat}, {group}, {spells}, {quests}, {line_of_sight}, and {nearby_players}; disabled or empty sections insert no text. (Text template with literal \n line breaks.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicessnapshotstoragemode"></a>`AzerothVoices.Snapshot.StorageMode`

- **Data Type**: Integer
- **Default Value**: `0`
- **Description**: Controls only older delivered snapshots: 0 stores none while the current snapshot still works, 1 stores bounded RAM until restart, and 2 persists SQL plus a lazy RAM cache (falling back to bounded RAM if tables are unavailable). (Mode 0/1/2; default 0.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicessnapshotdatabasemaximumsnapshots"></a>`AzerothVoices.Snapshot.DatabaseMaximumSnapshots`

- **Data Type**: Integer
- **Default Value**: `10`
- **Description**: --- SQL MODE SETTINGS (StorageMode = 2) --- Used only when StorageMode = 2 (SQL); snapshots, default 10. Caps persistent rows and lazy-loaded hot-cache snapshots per PlayerBot.
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicessnapshotdatabasettlminutes"></a>`AzerothVoices.Snapshot.DatabaseTTLMinutes`

- **Data Type**: Integer
- **Default Value**: `10080`
- **Description**: Used only when StorageMode = 2 (SQL); minutes, default 10080 (7 days). Older persistent snapshots are not loaded and are deleted by cleanup.
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicessnapshotrammaximumsnapshots"></a>`AzerothVoices.Snapshot.RamMaximumSnapshots`

- **Data Type**: Integer
- **Default Value**: `3`
- **Description**: --- RAM MODE SETTINGS (StorageMode = 1) --- Used only when StorageMode = 1 (RAM); snapshots, default 3. Caps retained snapshots per PlayerBot until worldserver restarts.
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicessnapshothistoryttlminutes"></a>`AzerothVoices.Snapshot.HistoryTTLMinutes`

- **Data Type**: Integer
- **Default Value**: `30`
- **Description**: Prompt-age limit for older snapshots in RAM or the SQL hot cache; the current live snapshot is never aged out. (Minutes, minimum 1; default 30.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicessnapshothistorymaximumcharacters"></a>`AzerothVoices.Snapshot.HistoryMaximumCharacters`

- **Data Type**: Integer (Characters, minimum 128; default 1600.)
- **Default Value**: `1600`
- **Description**: Hard combined prompt cap for older snapshots, separate from the richer current-snapshot cap. (Characters, minimum 128; default 1600.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicessnapshothistorymaximumactors"></a>`AzerothVoices.Snapshot.HistoryMaximumActors`

- **Data Type**: Integer
- **Default Value**: `2048`
- **Description**: Hard bound on in-memory PlayerBot snapshot-history buckets in StorageMode 1 or 2. (Actors, minimum 1; default 2048.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

---

## 22. Structured local RAG (optional, no embeddings or extra provider call)

| Setting Name | Type / Range | Default | Summary |
| :--- | :--- | :--- | :--- |
| [`AzerothVoices.RAG.Enable`](#azerothvoicesragenable) | Boolean (`0` or `1`) | `0` | Loads cached local JSON lore and retrieves by keywords. (Default 0.) |
| [`AzerothVoices.RAG.Directory`](#azerothvoicesragdirectory) | String | `modules/mod-azeroth-voices/data/rag/` | Loads every .json file here and also checks the installed and source-tree m... |
| [`AzerothVoices.RAG.MaximumItems`](#azerothvoicesragmaximumitems) | Integer | `3` | Maximum RAG excerpts per provider request. (Entries, minimum 1; default 3.) |
| [`AzerothVoices.RAG.SimilarityThreshold`](#azerothvoicesragsimilaritythreshold) | Decimal (Decimal 0.0-1.0; default 0.3.) | `0.3` | Minimum local weighted similarity required to include an entry; lower value... |
| [`AzerothVoices.RAG.MaximumCharacters`](#azerothvoicesragmaximumcharacters) | Integer (Characters, minimum 128; default 1200.) | `1200` | Total structured RAG prompt cap. (Characters, minimum 128; default 1200.) |
| [`AzerothVoices.RAG.ReloadOnRestart`](#azerothvoicesragreloadonrestart) | Boolean (`0` or `1`) | `1` | Reloads JSON when the module restarts via `.av restart`. (Default 1.) |
| [`AzerothVoices.RAG.PromptTemplate`](#azerothvoicesragprompttemplate) | String (Template with placeholders) | `"RELEVANT INFORMATION:\n{rag_info}\nUse this information to provide accurate and detailed responses when applicable."` | The required {rag_info} placeholder is replaced with the selected local RAG... |


### <a id="azerothvoicesragenable"></a>`AzerothVoices.RAG.Enable`

- **Data Type**: Boolean (`0` or `1`)
- **Default Value**: `0`
- **Description**: Loads cached local JSON lore and retrieves by keywords. (Default 0.)
- **Operator Guidance & Impact**: Disabled (`0`) by default. Enable (`1`) only if you wish to activate this optional subsystem.

### <a id="azerothvoicesragdirectory"></a>`AzerothVoices.RAG.Directory`

- **Data Type**: String
- **Default Value**: `modules/mod-azeroth-voices/data/rag/`
- **Description**: Loads every .json file here and also checks the installed and source-tree module data paths so one value works in both deployments. (Directory path.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicesragmaximumitems"></a>`AzerothVoices.RAG.MaximumItems`

- **Data Type**: Integer
- **Default Value**: `3`
- **Description**: Maximum RAG excerpts per provider request. (Entries, minimum 1; default 3.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicesragsimilaritythreshold"></a>`AzerothVoices.RAG.SimilarityThreshold`

- **Data Type**: Decimal (Decimal 0.0-1.0; default 0.3.)
- **Default Value**: `0.3`
- **Description**: Minimum local weighted similarity required to include an entry; lower values increase coverage and noise, higher values increase precision and may miss useful lore, with 0.2-0.4 recommended. (Decimal 0.0-1.0; default 0.3.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicesragmaximumcharacters"></a>`AzerothVoices.RAG.MaximumCharacters`

- **Data Type**: Integer (Characters, minimum 128; default 1200.)
- **Default Value**: `1200`
- **Description**: Total structured RAG prompt cap. (Characters, minimum 128; default 1200.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicesragreloadonrestart"></a>`AzerothVoices.RAG.ReloadOnRestart`

- **Data Type**: Boolean (`0` or `1`)
- **Default Value**: `1`
- **Description**: Reloads JSON when the module restarts via `.av restart`. (Default 1.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

### <a id="azerothvoicesragprompttemplate"></a>`AzerothVoices.RAG.PromptTemplate`

- **Data Type**: String (Template with placeholders)
- **Default Value**: `"RELEVANT INFORMATION:\n{rag_info}\nUse this information to provide accurate and detailed responses when applicable."`
- **Description**: The required {rag_info} placeholder is replaced with the selected local RAG excerpts before the provider request. (Text template with literal \n line breaks.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

---

## 23. Azeroth Voices Companion addon and Chatter compatibility

| Setting Name | Type / Range | Default | Summary |
| :--- | :--- | :--- | :--- |
| [`AzerothVoices.Addon.Enable`](#azerothvoicesaddonenable) | Boolean (`0` or `1`) | `1` | Enables the `.avaddon` and legacy `.llmc` command channels along with `AZER... |
| [`AzerothVoices.Addon.MaximumRosterEntries`](#azerothvoicesaddonmaximumrosterentries) | Integer | `200` | Hard bound on the roster returned to one player. (Entries, minimum 1; defau... |


### <a id="azerothvoicesaddonenable"></a>`AzerothVoices.Addon.Enable`

- **Data Type**: Boolean (`0` or `1`)
- **Default Value**: `1`
- **Description**: Enables the `.avaddon` and legacy `.llmc` command channels along with `AZEROTH_VOICES` addon message and legacy `CHATTER_ADDON` system responses used by the bundled AzerothVoices client addon and stock Chatter clients. No GM security is required: a player only ever sees and edits bots in their own contact roster. Requires azeroth_voices_addon_contacts; when that table is missing the roster degrades to empty and every other subsystem keeps working. (Default 1.)
- **Operator Guidance & Impact**: Keep enabled (`1`) for standard functionality. Set to `0` to completely bypass this feature and eliminate its processing overhead.

### <a id="azerothvoicesaddonmaximumrosterentries"></a>`AzerothVoices.Addon.MaximumRosterEntries`

- **Data Type**: Integer
- **Default Value**: `200`
- **Description**: Hard bound on the roster returned to one player. (Entries, minimum 1; default 200.)
- **Operator Guidance & Impact**: Refer to the technical description above to calibrate this value for your server environment.

---

## Recommended Deployment Presets


### 1. Solo Play / Companion Bot Focus (Cost-Optimized)

Ideal for local single-player setups using a free or low-cost API tier (e.g. OpenAI `gpt-4o-mini`, OpenRouter, or local Ollama):

```ini
AzerothVoices.GlobalRequestsPerMinute = 30
AzerothVoices.WorkerThreads = 4
AzerothVoices.SpeakerCoalesce.Enable = 1
AzerothVoices.SpeakerCoalesce.DelayMilliseconds = 2000
AzerothVoices.BotSpeakerCooldownSeconds = 900
AzerothVoices.General.Enable = 0
AzerothVoices.Proximity.OutdoorChance = 5
AzerothVoices.GroupChatter.Enable = 1
AzerothVoices.Snapshot.Enable = 0
```


### 2. High-Immersion Active World (Vibrant Population)

Ideal for servers with multiple human players where living, breathing bots and NPCs create an authentic world environment:

```ini
AzerothVoices.GlobalRequestsPerMinute = 120
AzerothVoices.WorkerThreads = 12
AzerothVoices.General.Enable = 1
AzerothVoices.General.TriggerIntervalSeconds = 30
AzerothVoices.General.TriggerChance = 20
AzerothVoices.BotSpeakerCooldownSeconds = 600
AzerothVoices.Proximity.Enable = 1
AzerothVoices.Proximity.OutdoorChance = 25
AzerothVoices.Proximity.InstanceChance = 85
AzerothVoices.GuildChatter.Enable = 1
AzerothVoices.GuildChatter.PlayerReplies.Enable = 1
```


### 3. Pure In-Character Roleplay Server

Enforces strict in-world lore immersion for both bots and NPCs, forbidding any meta real-world gamer slang:

```ini
AzerothVoices.GlobalMode = Roleplay
AzerothVoices.Personality.BackgroundMode = 0
AzerothVoices.Replies.Whisper = 1
AzerothVoices.Replies.Say = 1
AzerothVoices.Replies.Yell = 1
AzerothVoices.Replies.Party = 1
AzerothVoices.NPC.AllowNeutralAndHostile = 1
```


## Maintenance & Update Protocol

> [!IMPORTANT]

> **MANDATORY FOR AUTONOMOUS AGENTS (Prime Directive)**:

> Whenever any configuration key is added, modified, renamed, deprecated, or removed in `conf/mod-azeroth-voices.conf.dist` or `src/AzerothVoicesConfig.*`, this guide **MUST** be updated immediately in the same turn.

> The embedded agent rules in [`AGENTS.md`](file:///C:/Users/Admin/AntigravityProfiles/Projects%20Twow/Turtle%20wow%20modules/mod-azeroth-voices/AGENTS.md) strictly enforce this synchronization invariant.
