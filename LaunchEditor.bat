@echo off
REM Open the project in the Unreal editor, for placing targets and building levels.
REM
REM Inside the editor, prefer Play -> Standalone Game over the default Play button. Standalone
REM gives the transmitter a single unambiguous window to deliver input to.

set UE=C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe
set PROJECT=%~dp0FPVDrone.uproject

start "" "%UE%" "%PROJECT%"
