param([string]$proj = "C:\Users\atput\Desktop\OUTTA_THISWORLD")

function Patch-File([string]$path, [string]$old, [string]$new) {
    $content = [System.IO.File]::ReadAllText($path)
    if ($content.Contains($old)) {
        $content = $content.Replace($old, $new)
        [System.IO.File]::WriteAllText($path, $content)
        Write-Host "[OK] $path"
    } else {
        Write-Host "[WARN] Pattern not found in $path"
    }
}

# === porkchop.h: add mode enums ===
$ph = [System.IO.File]::ReadAllText("$proj\src\core\porkchop.h")
$ph = $ph.Replace("PIGSYNC_CALL, // PigSync active call",
    "PIGSYNC_CALL, // PigSync active call`r`n    THREATSCAN_MODE,  // Live surveillance threat radar`r`n    AUDIO_PLAYER_MODE, // WAV/MP3 player")
$ph = $ph.Replace("CHARGING        // Low power charging mode", "CHARGING,        // Low power charging mode")
[System.IO.File]::WriteAllText("$proj\src\core\porkchop.h", $ph)
Write-Host "[OK] porkchop.h"

# === porkchop.cpp: multiple patches ===
$pcpp = [System.IO.File]::ReadAllText("$proj\src\core\porkchop.cpp")

# Add includes
$pcpp = $pcpp.Replace('#include "../modes/charging.h"',
    '#include "../modes/charging.h"`r`n#include "../modes/threatscan.h"`r`n#include "../modes/audioplayer.h"`r`n#include "../core/threat.h"`r`n#include "../holmes/detective.h"')

# Add ThreatDB + Detective update in Porkchop::update()
$pcpp = $pcpp.Replace("cursed_update();",
    "cursed_update();`r`n    ThreatDB::update();`r`n    Detective::update();")

# modeToString cases
$pcpp = $pcpp.Replace("case PorkchopMode::CHARGING: return ""CHARGING"";",
    "case PorkchopMode::THREATSCAN_MODE: return ""THREATSCAN"";`r`n        case PorkchopMode::AUDIO_PLAYER_MODE: return ""AUDIO_PLAYER"";`r`n        case PorkchopMode::CHARGING: return ""CHARGING"";")

# Menu callback cases
$pcpp = $pcpp.Replace("case 21: setMode(PorkchopMode::CHARGING); break;",
    "case 21: setMode(PorkchopMode::CHARGING); break;`r`n            case 22: setMode(PorkchopMode::THREATSCAN_MODE); break;`r`n            case 23: setMode(PorkchopMode::AUDIO_PLAYER_MODE); break;")

# IDLE key shortcuts
$pcpp = $pcpp.Replace("case 'c': // Charging mode",
    "case 'r': case 'R': setMode(PorkchopMode::THREATSCAN_MODE); break;`r`n                case 'z': case 'Z': setMode(PorkchopMode::AUDIO_PLAYER_MODE); break;`r`n                case 'c': // Charging mode")

# setMode cleanup cases
$pcpp = $pcpp.Replace("case PorkchopMode::BACON_MODE:`r`n            BaconMode::stop();",
    "case PorkchopMode::THREATSCAN_MODE: ThreatScanMode::stop(); break;`r`n        case PorkchopMode::AUDIO_PLAYER_MODE: AudioPlayerMode::stop(); break;`r`n        case PorkchopMode::BACON_MODE:`r`n            BaconMode::stop();")

# setMode init cases (add before CHARGING)
$pcpp = $pcpp.Replace("case PorkchopMode::CHARGING:`r`n            if (!shutdownAnimating) {",
    "case PorkchopMode::THREATSCAN_MODE:`r`n            Avatar::setState(AvatarState::HUNTING);`r`n            Detective::setState(DetectiveState::STAKEOUT);`r`n            SDLog::log(""PORK"", ""Mode: THREATSCAN"");`r`n            ThreatScanMode::start();`r`n            break;`r`n        case PorkchopMode::AUDIO_PLAYER_MODE:`r`n            Avatar::setState(AvatarState::HAPPY);`r`n            Detective::setState(DetectiveState::COLD_CASE);`r`n            SDLog::log(""PORK"", ""Mode: AUDIO PLAYER"");`r`n            AudioPlayerMode::start();`r`n            break;`r`n        case PorkchopMode::CHARGING:`r`n            if (!shutdownAnimating) {")

# updateMode cases
$pcpp = $pcpp.Replace("case PorkchopMode::BACON_MODE:`r`n            BaconMode::update();",
    "case PorkchopMode::THREATSCAN_MODE:`r`n            ThreatScanMode::update();`r`n            break;`r`n        case PorkchopMode::AUDIO_PLAYER_MODE:`r`n            AudioPlayerMode::update();`r`n            if (!AudioPlayerMode::isRunning()) {`r`n                setMode(PorkchopMode::IDLE);`r`n            }`r`n            break;`r`n        case PorkchopMode::BACON_MODE:`r`n            BaconMode::update();")

[System.IO.File]::WriteAllText("$proj\src\core\porkchop.cpp", $pcpp)
Write-Host "[OK] porkchop.cpp"

# === main.cpp ===
$m = [System.IO.File]::ReadAllText("$proj\src\main.cpp")
$m = $m.Replace('#include "modes/oink.h"',
    '#include "modes/oink.h"`r`n#include "modes/warhog.h"`r`n#include "core/threat.h"`r`n#include "holmes/detective.h"`r`n#include "modes/threatscan.h"`r`n#include "modes/audioplayer.h"')
$m = $m.Replace("Mood::init();",
    "Mood::init();`r`n    ThreatDB::init();`r`n    Detective::init();`r`n    ThreatScanMode::init();`r`n    AudioPlayerMode::init();")
[System.IO.File]::WriteAllText("$proj\src\main.cpp", $m)
Write-Host "[OK] main.cpp"

# === avatar.cpp ===
$acpp = [System.IO.File]::ReadAllText("$proj\src\piglet\avatar.cpp")
# Add include for detective
$acpp = $acpp.Replace('#include "../core/cursed.h"',
    '#include "../core/cursed.h"`r`n#include "../holmes/detective.h"')
# Replace the draw function body (find the closing of Avatar::draw)
$drawStart = "void Avatar::draw(M5Canvas& canvas) {"
if ($acpp.Contains($drawStart)) {
    $idx = $acpp.IndexOf($drawStart)
    # Find the matching closing brace
    $closing = $acpp.IndexOf("`n}", $idx)
    if ($closing -gt 0) {
        $before = $acpp.Substring(0, $idx)
        $after = $acpp.Substring($closing + 2)
        $acpp = $before + "void Avatar::draw(M5Canvas& canvas) {`r`n    Detective::update();`r`n    Detective::draw(canvas);`r`n}" + $after
    }
}
[System.IO.File]::WriteAllText("$proj\src\piglet\avatar.cpp", $acpp)
Write-Host "[OK] avatar.cpp"

Write-Host "`n=== ALL INTEGRATION PATCHES APPLIED ==="
