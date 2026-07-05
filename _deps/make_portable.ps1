# Build the QElectroTech portable (zero-trace) folder.
# ASCII-only on purpose: the Chinese launcher filename is built from char codes
# so this script is immune to PS 5.1's ANSI-vs-UTF8 source decoding.
$ErrorActionPreference = 'Stop'
$root = 'C:\Users\JasonLin\Documents\ClaudeCode\QET'
$src  = Join-Path $root 'build'
$dst  = Join-Path $root 'dist\QElectroTech-portable'
$qtbin = 'C:\Qt\6.8.3\msvc2022_64\bin'
$crt  = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Redist\MSVC\14.44.35112\x64\Microsoft.VC143.CRT'

# "Qi Dong QElectroTech.bat"  (Qi=U+555F Dong=U+52D5)
$batName = [string]([char]0x555F) + [char]0x52D5 + ' QElectroTech.bat'

if (Test-Path $dst) { Remove-Item $dst -Recurse -Force }
New-Item -ItemType Directory -Force $dst | Out-Null

# 1) main exe + third-party shared DLLs (windeployqt ignores these)
Copy-Item "$src\qelectrotech.exe" $dst
Copy-Item "$src\pugixml.dll" $dst
foreach ($d in 'dxcompiler.dll','dxil.dll') {
    if (Test-Path "$src\$d") { Copy-Item "$src\$d" $dst }
}
# KF6 shared libs that a from-scratch build may produce in build\bin
Get-ChildItem "$src\bin\*.dll" -ErrorAction SilentlyContinue | ForEach-Object { Copy-Item $_.FullName $dst }

# 2) Qt DLLs / plugins
& "$qtbin\windeployqt.exe" --release --no-translations --no-system-d3d-compiler --no-opengl-sw (Join-Path $dst 'qelectrotech.exe') | Out-Null
Remove-Item (Join-Path $dst 'vc_redist.x64.exe') -ErrorAction SilentlyContinue

# 3) VC++ runtime (so a clean machine needs no redist)
'msvcp140.dll','msvcp140_1.dll','msvcp140_2.dll','vcruntime140.dll','vcruntime140_1.dll','concrt140.dll' |
    ForEach-Object { Copy-Item (Join-Path $crt $_) $dst }

# 4) common element / title-block library (on-disk folders, not embedded)
robocopy "$root\elements" "$dst\elements" /E /NFL /NDL /NJH /NJS /NP | Out-Null
robocopy "$root\titleblocks" "$dst\titleblocks" /E /NFL /NDL /NJH /NJS /NP | Out-Null

# 5) launcher (ASCII content, Unicode filename)
Copy-Item "$root\_deps\portable_launcher.bat" (Join-Path $dst $batName)

# 6) shortcut with QET icon
$lnk = Join-Path $dst 'QElectroTech.lnk'
$ws = New-Object -ComObject WScript.Shell
$sc = $ws.CreateShortcut($lnk)
$sc.TargetPath = $env:ComSpec
$sc.Arguments = '/c "' + (Join-Path $dst $batName) + '"'
$sc.WorkingDirectory = $dst
$sc.IconLocation = (Join-Path $dst 'qelectrotech.exe') + ',0'
$sc.WindowStyle = 7
$sc.Description = 'QElectroTech portable (zero-trace)'
$sc.Save()

Write-Output "DONE: $dst"
