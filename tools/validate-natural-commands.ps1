param(
    [string]$CoreRoot = ""
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

if (-not $CoreRoot) {
    $CoreRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\..\.."))
}

$moduleRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$coreRegistryPath = Join-Path $CoreRoot "src\modules\PlayerBots\playerbot\strategy\triggers\ChatTriggerContext.h"
$moduleRegistryPath = Join-Path $moduleRoot "src\AzerothVoicesNaturalCommands.cpp"
$managerPath = Join-Path $moduleRoot "src\AzerothVoicesManager.cpp"
$providerPath = Join-Path $moduleRoot "src\AzerothVoicesProvider.cpp"
$typesPath = Join-Path $moduleRoot "src\AzerothVoicesTypes.h"
$catalogPath = Join-Path $moduleRoot "data\NATURAL_COMMAND_ACTIONS.txt"
$configPath = Join-Path $moduleRoot "conf\mod-azeroth-voices.conf.dist"
$configLoaderPath = Join-Path $moduleRoot "src\AzerothVoicesConfig.cpp"

$failures = [Collections.Generic.List[string]]::new()
function Assert-Check([bool]$Condition, [string]$Message) {
    if (-not $Condition) { $script:failures.Add($Message) }
}

foreach ($path in @($coreRegistryPath, $moduleRegistryPath, $managerPath, $providerPath,
    $typesPath, $catalogPath, $configPath, $configLoaderPath)) {
    Assert-Check (Test-Path -LiteralPath $path) "Missing required validation input: $path"
}
if ($failures.Count) {
    $failures | ForEach-Object { Write-Error $_ }
    exit 1
}

$coreText = Get-Content -Raw -LiteralPath $coreRegistryPath
$moduleText = Get-Content -Raw -LiteralPath $moduleRegistryPath
$managerText = Get-Content -Raw -LiteralPath $managerPath
$providerText = Get-Content -Raw -LiteralPath $providerPath
$typesText = Get-Content -Raw -LiteralPath $typesPath
$catalogText = Get-Content -Raw -LiteralPath $catalogPath
$configText = Get-Content -Raw -LiteralPath $configPath
$configLoaderText = Get-Content -Raw -LiteralPath $configLoaderPath

$coreNames = @([regex]::Matches($coreText, 'creators\["([^"]+)"\]') | ForEach-Object { $_.Groups[1].Value })
$actionMatches = @([regex]::Matches($moduleText,
    '\{\s*"([^"]+)"\s*,\s*"([^"]*)"\s*,\s*(\d+)\s*,\s*(true|false)\s*\}'))
$actions = @($actionMatches | ForEach-Object {
    [pscustomobject]@{
        Name = $_.Groups[1].Value
        Meaning = $_.Groups[2].Value
        Usefulness = [int]$_.Groups[3].Value
        Forbidden = $_.Groups[4].Value -eq 'true'
    }
})
$moduleNames = @($actions.Name)
$catalogNames = @([regex]::Matches($catalogText, '(?m)^([a-z][a-z0-9]*(?: [a-z0-9]+)*) - ') |
    ForEach-Object { $_.Groups[1].Value })
$expectedForbidden = @('cdebug', 'cheat', 'debug', 'destroy', 'reset values', 'set value')
$actualForbidden = @($actions | Where-Object Forbidden | ForEach-Object Name | Sort-Object)

Assert-Check ($coreNames.Count -eq 137) "Expected 137 PlayerBots creators, found $($coreNames.Count)."
Assert-Check ($moduleNames.Count -eq 137) "Expected 137 module actions, found $($moduleNames.Count)."
Assert-Check (($coreNames | Sort-Object -Unique).Count -eq $coreNames.Count) "PlayerBots registry contains duplicate creator names."
Assert-Check (($moduleNames | Sort-Object -Unique).Count -eq $moduleNames.Count) "Module registry contains duplicate action names."
Assert-Check (-not (Compare-Object ($coreNames | Sort-Object) ($moduleNames | Sort-Object))) `
    "Module action names do not exactly match ChatTriggerContext.h."
Assert-Check (-not (Compare-Object ($moduleNames | Sort-Object) ($catalogNames | Sort-Object))) `
    "NATURAL_COMMAND_ACTIONS.txt action names do not exactly match the module registry."
Assert-Check (-not (Compare-Object $expectedForbidden $actualForbidden)) `
    "The permanently forbidden action set changed."
Assert-Check (@($actions | Where-Object { -not $_.Meaning -or $_.Usefulness -lt 1 -or $_.Usefulness -gt 5 }).Count -eq 0) `
    "One or more actions have invalid description/usefulness metadata."

$aliasBlock = [regex]::Match($moduleText,
    'static std::map<std::string, std::string> const aliases = \{(?<body>.*?)\};',
    [Text.RegularExpressions.RegexOptions]::Singleline).Groups['body'].Value
$aliases = @{}
foreach ($match in [regex]::Matches($aliasBlock, '\{"([^"]+)",\s*"([^"]+)"\}')) {
    $aliases[$match.Groups[1].Value] = $match.Groups[2].Value
}
foreach ($entry in $aliases.GetEnumerator()) {
    Assert-Check ($moduleNames -contains $entry.Key) "Alias '$($entry.Key)' is not a registered action."
    Assert-Check ($moduleNames -contains $entry.Value) "Alias '$($entry.Key)' has unknown canonical action '$($entry.Value)'."
}

foreach ($requiredSymbol in @('GetNaturalCommandMetadata', 'ShortlistNaturalCommandActions',
    'ValidateNaturalCommandArguments', 'PreserveNaturalCommandLinks', 'NaturalCommandRiskName',
    'ExpandNaturalCommandPreset')) {
    Assert-Check ($moduleText.Contains($requiredSymbol)) "Missing metadata/validation symbol: $requiredSymbol"
}
Assert-Check ($moduleText.Contains('if (action.forbidden) return NaturalCommandRisk::Forbidden')) `
    "Forbidden actions are not forced to forbidden risk."
Assert-Check ($moduleText.Contains('if (action.forbidden || !allowed.count(action.name))')) `
    "Runtime shortlist/catalog is not visibly constrained to the allowlist."
Assert-Check ($moduleText.Contains('else if (action->forbidden)')) `
    "Configured explicit forbidden actions are not rejected."
Assert-Check ($moduleText.Contains('if (action.forbidden)') -and $moduleText.Contains('allowed.insert(action.name)')) `
    "Wildcard allowlist implementation could not be verified."
Assert-Check ($moduleText.Contains("result << action.name << '|' << argumentCode") -and
    -not $moduleText.Contains('<< " | risk="')) `
    "Compact natural-command prompt metadata is missing."
Assert-Check ($moduleText.Contains('actionUsage') -and $moduleText.Contains('left.usage > right.usage')) `
    "Most-used action shortlist promotion is missing."
Assert-Check ($moduleText.Contains('normalized == "light"') -and
    $moduleText.Contains('normalized == "medium"') -and
    $moduleText.Contains('normalized == "heavy"')) `
    "Natural-command preset expansion is missing."
Assert-Check ($configText.Contains('AzerothVoices.NaturalCommands.AllowedActions = medium')) `
    "The distributed balanced natural-command preset default is missing."

$requiredConfigKeys = @(
    'AzerothVoices.NaturalCommands.Enable',
    'AzerothVoices.NaturalCommands.MasterOnly',
    'AzerothVoices.NaturalCommands.LocalFastPath',
    'AzerothVoices.NaturalCommands.LLMFallback',
    'AzerothVoices.NaturalCommands.Model',
    'AzerothVoices.NaturalCommands.MinimumConfidence',
    'AzerothVoices.NaturalCommands.RequestTTLSeconds',
    'AzerothVoices.NaturalCommands.RetryMaximum',
    'AzerothVoices.NaturalCommands.MaximumPendingPerBot',
    'AzerothVoices.NaturalCommands.ShortlistMaximum',
    'AzerothVoices.NaturalCommands.MaximumRecipients',
    'AzerothVoices.NaturalCommands.MaximumActionsPerMessage',
    'AzerothVoices.NaturalCommands.RequireHighRiskConfirmation',
    'AzerothVoices.NaturalCommands.ConfirmationTTLSeconds',
    'AzerothVoices.NaturalCommands.Feedback',
    'AzerothVoices.NaturalCommands.AcknowledgementMode',
    'AzerothVoices.NaturalCommands.Telemetry.Enable',
    'AzerothVoices.NaturalCommands.PromoteFrequentlyUsedActions',
    'AzerothVoices.NaturalCommands.Audit.Enable',
    'AzerothVoices.NaturalCommands.Audit.MaximumRecords',
    'AzerothVoices.NaturalCommands.Audit.IncludeArguments',
    'AzerothVoices.NaturalCommands.AllowedActions',
    'AzerothVoices.NaturalCommands.ExcludedChannels'
)
foreach ($key in $requiredConfigKeys) {
    $count = [regex]::Matches($configText, "(?m)^$([regex]::Escape($key))\s*=").Count
    Assert-Check ($count -eq 1) "Config key '$key' occurs $count times; expected exactly once."
    Assert-Check ($configLoaderText.Contains('"' + $key + '"')) "Config loader does not read '$key'."
}

Assert-Check ($managerText.Contains('SameSubGroup(speaker, bot)')) "Party same-subgroup recipient check is missing."
Assert-Check ($managerText.Contains('addressing = "party-single"') -and
    $managerText.Contains('addressing = "ambiguous-party"') -and
    $managerText.Contains('GetFirstMember()')) `
    "Single-eligible-PartyBot implicit recipient resolution is missing or unbounded."
Assert-Check ($managerText.Contains('bot->GetGroup() == speaker->GetGroup()')) "Raid same-group recipient check is missing."
Assert-Check ($managerText.Contains('bot->GetGuildId() == speaker->GetGuildId()')) "Guild recipient check is missing."
Assert-Check ($managerText.Contains('GR_RIGHT_OFFCHATLISTEN')) "Officer listen-right check is missing."
Assert-Check ($managerText.Contains('ObjectAccessor::FindPlayerByName(token.c_str())')) `
    "Targeted PlayerBot name lookup is missing."
Assert-Check (-not $managerText.Contains('m_latestNaturalCommandByActor')) `
    "Obsolete latest-wins natural-command state remains."
Assert-Check ($managerText.Contains('m_pendingNaturalCommandsByActor')) "Per-bot natural-command FIFO state is missing."
Assert-Check ($managerText.Contains('m_pendingNaturalConfirmations')) "Confirmation state is missing."
Assert-Check ($managerText.Contains('generatedAcknowledgement ? 5 : 4') -and
    $managerText.Contains('generatedAcknowledgement ? 3 : 2')) `
    "Strict optional-acknowledgment single/batch decision parsers are missing."
Assert-Check ($managerText.Contains('sourceLinks != completion.request.naturalPreservedLinks')) `
    "Local link-source integrity check is missing."
Assert-Check ($managerText.Contains('Use one natural-language action per message.')) `
    "Default one-action rejection is missing."
Assert-Check ($managerText.Contains('naturalCommandsMaximumRecipients') -and
    $managerText.Contains('naturalCommandsMaximumActions')) `
    "Bounded configurable recipient/action limits are missing."
Assert-Check ($managerText.Contains('NaturalCommandPendingForAll') -and
    $managerText.Contains('RemoveNaturalCommandPending')) `
    "Multi-recipient FIFO accounting is missing."
Assert-Check ($configLoaderText.Contains('"AzerothVoices.NaturalCommands.ShortlistMaximum", 20, 0, 131')) `
    "Dynamic shortlist configuration does not accept automatic mode and the complete registry range."
Assert-Check ($moduleText.Contains('maximumActions == 0 || available.size() <= maximumActions')) `
    "Dynamic shortlist does not expand to the complete fitting allowlist."
Assert-Check ($managerText.Contains('"wait here", "stay"') -and
    $managerText.Contains('"please follow me", "follow"')) `
    "Required local no-double-dispatch phrases are missing."
Assert-Check ($managerText.Contains('Set AiPlayerbot.CommandPrefix = !')) `
    "Empty native-prefix protection/warning is missing."
Assert-Check ($managerText.Contains('NaturalCommandChannelExcluded')) `
    "Excluded-channel gate is missing."
Assert-Check ($providerText.Contains('request.kind != RequestKind::NaturalCommand') -and
    $providerText.Contains('Lower(config.parserMode) == "legacyregex"')) `
    "Natural-command decisions are not isolated from LegacyRegex."
Assert-Check ($providerText.Contains('requestTimeoutMillisecondsOverride')) `
    "Provider timeouts are not capped with remaining-TTL millisecond precision."
Assert-Check ($typesText.Contains('std::string modelOverride') -and
    $providerText.Contains('request.modelOverride.empty()') -and
    $managerText.Contains('request.modelOverride = m_config->naturalCommandsModel')) `
    "Separate natural-command classifier model override is missing."
Assert-Check ($managerText.Contains('m_naturalTelemetry') -and
    $managerText.Contains('NaturalCommandMostUsedActions') -and
    $managerText.Contains('RecordNaturalCommandAudit') -and
    $managerText.Contains('ScheduleNaturalCommandAcknowledgement')) `
    "Natural-command telemetry, audit, usage promotion, or acknowledgment hooks are missing."

$botPchUsers = @(Get-ChildItem -LiteralPath (Join-Path $moduleRoot 'src') -Filter '*.cpp' |
    Where-Object { (Get-Content -Raw -LiteralPath $_.FullName).Contains('#include "botpch.h"') } |
    ForEach-Object Name)
Assert-Check (-not (Compare-Object @('AzerothVoicesPlayerbotBridge.cpp') $botPchUsers)) `
    "botpch.h must be isolated to AzerothVoicesPlayerbotBridge.cpp."

# Reproduce the catalog-size calculation from the value-owned registry. This is
# deliberately a character count, not a provider billing/token estimate.
$legacyCatalog = ($actions | Where-Object { -not $_.Forbidden } |
    ForEach-Object { "$($_.Name) | $($_.Meaning)" }) -join "`n"
$representatives = [ordered]@{
    'equip this sword' = @('equip', 'use', 'items', 'outfit', 'keep')
    'attack my target' = @('attack', 'pull', 'attack rti', 'pull rti', 'possible attack targets')
    'invite him to the guild' = @('guild invite', 'invite', 'guild join', 'guild promote', 'guild remove')
    'follow me' = @('follow', 'follow target', 'stay', 'guard', 'free')
}

Write-Host "Natural-command registry validation"
Write-Host "  PlayerBots inputs: $($coreNames.Count)"
Write-Host "  Module inputs:     $($moduleNames.Count)"
Write-Host "  Allowed by '*':    $(@($actions | Where-Object { -not $_.Forbidden }).Count)"
Write-Host "  Permanently denied:$($actualForbidden.Count)"
Write-Host "  Legacy full catalog characters (name + description): $($legacyCatalog.Length)"
foreach ($entry in $representatives.GetEnumerator()) {
    $subset = @($entry.Value | Where-Object { $moduleNames -contains $_ -and $actualForbidden -notcontains $_ })
    $compact = ($subset | ForEach-Object {
        $definition = $actions | Where-Object Name -eq $_ | Select-Object -First 1
        "$($definition.Name) | $($definition.Meaning)"
    }) -join "`n"
    Write-Host "  Legacy compact reference '$($entry.Key)': $($subset.Count) actions, $($compact.Length) catalog characters (excludes current metadata)"
}
Write-Host "  Compile tools/NaturalCommandValidation.cpp with src/AzerothVoicesNaturalCommands.cpp for exact current shortlist/prompt measurements."

if ($failures.Count) {
    Write-Host "FAILED ($($failures.Count))"
    $failures | ForEach-Object { Write-Host "  - $_" }
    exit 1
}

Write-Host "PASS: registry, forbidden set, catalog, aliases, metadata hooks, config keys, FIFO, channel gates, parser isolation, bridge isolation, and TTL timeout cap."
