$proj = "C:\Users\atput\Desktop\OUTTA_THISWORLD"

# Fix 1: threat.cpp — add NetworkRecon include, fix Arduino::Display
$t = [System.IO.File]::ReadAllText("$proj\src\core\threat.cpp")
$t = $t.Replace('#include "../audio/sfx.h"', '#include "../audio/sfx.h"`r`n#include "network_recon.h"')
$t = $t.Replace('Arduino::Display.setBrightness', 'M5.Display.setBrightness')
[System.IO.File]::WriteAllText("$proj\src\core\threat.cpp", $t)

# Fix 2: main.cpp — fix includes
$m = [System.IO.File]::ReadAllText("$proj\src\main.cpp")
$m = $m.Replace('#include "modes/warhog.h"', '#include "core/threat.h"')
$m = $m.Replace('#include "core/threat.h"', '#include "core/threat.h"')
$m = $m.Replace('#include "holmes/detective.h"', '#include "../holmes/detective.h"')
$m = $m.Replace('#include "modes/threatscan.h"', '#include "../modes/threatscan.h"')
$m = $m.Replace('#include "modes/audioplayer.h"', '#include "../modes/audioplayer.h"')
[System.IO.File]::WriteAllText("$proj\src\main.cpp", $m)

# Fix 3: porkchop.cpp — verify includes
$p = [System.IO.File]::ReadAllText("$proj\src\core\porkchop.cpp")
$p = $p.Replace('#include "../modes/threatscan.h"', '#include "../modes/threatscan.h"')
$p = $p.Replace('#include "../modes/audioplayer.h"', '#include "../modes/audioplayer.h"')
$p = $p.Replace('#include "threat.h"', '#include "threat.h"')
$p = $p.Replace('#include "../holmes/detective.h"', '#include "../holmes/detective.h"')
$p = $p.Replace('#include "../core/threat.h"', '#include "threat.h"')
[System.IO.File]::WriteAllText("$proj\src\core\porkchop.cpp", $p)

# Fix 4: audioplayer.cpp — access currentTrack
$ap = [System.IO.File]::ReadAllText("$proj\src\modes\audioplayer.cpp")
$ap = $ap.Replace('WAVPlayer::currentTrack', 'currentTrack')
$ap = $ap.Replace('WAVPlayer::currentTrack', 'currentTrack')
$ap = $ap.Replace('WAVPlayer::currentTrack', 'currentTrack')
[System.IO.File]::WriteAllText("$proj\src\modes\audioplayer.cpp", $ap)

Write-Host "All fixes applied"