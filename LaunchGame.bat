@echo off
REM Launch the game standalone -- no editor, closest to how it will actually be played.
REM
REM Standalone matters for the transmitter: RawInput only delivers to the focused window, and
REM Play-In-Editor complicates which window that is. If the Tango 2 ever stops responding,
REM click the game window first.
REM
REM Drop the -ExecCmds argument once you have real targets placed in a level.

set UE=C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe
set PROJECT=%~dp0FPVDrone.uproject
set MAP=/Game/NewMap

start "" "%UE%" "%PROJECT%" %MAP% -game -windowed -ResX=1600 -ResY=900 -ExecCmds="fpv.SpawnTestTargets"
