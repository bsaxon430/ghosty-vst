; Windows installer for the GhostBlock collection (Inno Setup 6). Built by CI: iscc /DAppVersion=x.y.z GhostBlock.iss
#ifndef AppVersion
  #define AppVersion "3.1.0"
#endif
[Setup]
AppId={{7B1E2C4D-6A3F-4E21-9C8B-0F5D3A2E1B90}
AppName=GhostBlock Plugins
AppVersion={#AppVersion}
AppPublisher=GhostBlock
DefaultDirName={autopf}\GhostBlock
DefaultGroupName=GhostBlock
DisableProgramGroupPage=yes
OutputDir=..\..\out
OutputBaseFilename=GhostBlock-Plugins-Windows-Setup
Compression=lzma2/max
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
WizardStyle=modern
LicenseFile=EULA.txt
UninstallDisplayName=GhostBlock Plugins

[Types]
Name: "full"; Description: "Everything"
Name: "custom"; Description: "Choose plugins"; Flags: iscustom

[Components]
Name: "mickens"; Description: "Mr. Mickens (drum & bass break machine)"; Types: full
Name: "poltergeist"; Description: "Poltergeist (FM spirit synth)"; Types: full
Name: "beats"; Description: "Ghosty Beats (drum machine)"; Types: full
Name: "spectrum"; Description: "Ghosty Spectrum (paint-a-loop instrument)"; Types: full
Name: "ghostypro"; Description: "Ghosty Pro (wavetable synth + Ghost FX rack)"; Types: full
Name: "ghostbox"; Description: "Ghost Box (FX rack for any track)"; Types: full
Name: "arpezator"; Description: "Professor Bernstein's ArpEZator (arp + chord + melody generator)"; Types: full
Name: "ghostslice"; Description: "Ghost Slice (nondestructive audio editor: cut, rearrange, stretch, warp)"; Types: full

[Files]
Source: "..\..\stage\VST3\Mr Mickens.vst3\*"; DestDir: "{commoncf64}\VST3\Mr Mickens.vst3"; Components: mickens; Flags: recursesubdirs ignoreversion
Source: "..\..\stage\VST3\Poltergeist.vst3\*"; DestDir: "{commoncf64}\VST3\Poltergeist.vst3"; Components: poltergeist; Flags: recursesubdirs ignoreversion
Source: "..\..\stage\VST3\Ghosty Beats.vst3\*"; DestDir: "{commoncf64}\VST3\Ghosty Beats.vst3"; Components: beats; Flags: recursesubdirs ignoreversion
Source: "..\..\stage\VST3\Ghosty Spectrum.vst3\*"; DestDir: "{commoncf64}\VST3\Ghosty Spectrum.vst3"; Components: spectrum; Flags: recursesubdirs ignoreversion
Source: "..\..\stage\VST3\Ghosty Pro.vst3\*"; DestDir: "{commoncf64}\VST3\Ghosty Pro.vst3"; Components: ghostypro; Flags: recursesubdirs ignoreversion
Source: "..\..\stage\VST3\Ghost Box.vst3\*"; DestDir: "{commoncf64}\VST3\Ghost Box.vst3"; Components: ghostbox; Flags: recursesubdirs ignoreversion
Source: "..\..\stage\VST3\ArpEZator.vst3\*"; DestDir: "{commoncf64}\VST3\ArpEZator.vst3"; Components: arpezator; Flags: recursesubdirs ignoreversion
Source: "..\..\stage\VST3\Ghost Slice.vst3\*"; DestDir: "{commoncf64}\VST3\Ghost Slice.vst3"; Components: ghostslice; Flags: recursesubdirs ignoreversion
Source: "..\..\stage\Standalone\Mr Mickens.exe"; DestDir: "{app}"; Components: mickens; Flags: ignoreversion
Source: "..\..\stage\Standalone\Poltergeist.exe"; DestDir: "{app}"; Components: poltergeist; Flags: ignoreversion
Source: "..\..\stage\Standalone\Ghosty Beats.exe"; DestDir: "{app}"; Components: beats; Flags: ignoreversion
Source: "..\..\stage\Standalone\Ghosty Spectrum.exe"; DestDir: "{app}"; Components: spectrum; Flags: ignoreversion
Source: "..\..\stage\Standalone\Ghosty Pro.exe"; DestDir: "{app}"; Components: ghostypro; Flags: ignoreversion
Source: "..\..\stage\Standalone\Ghost Box.exe"; DestDir: "{app}"; Components: ghostbox; Flags: ignoreversion
Source: "..\..\stage\Standalone\ArpEZator.exe"; DestDir: "{app}"; Components: arpezator; Flags: ignoreversion
Source: "..\..\stage\Standalone\Ghost Slice.exe"; DestDir: "{app}"; Components: ghostslice; Flags: ignoreversion
Source: "third_party\WORLD-LICENSE.txt"; DestDir: "{app}\licenses"; Components: spectrum; Flags: ignoreversion
Source: "third_party\EURORACK-MIT-LICENSE.txt"; DestDir: "{app}\licenses"; Components: ghostypro; Flags: ignoreversion
Source: "third_party\SIGNALSMITH-MIT-LICENSE.txt"; DestDir: "{app}\licenses"; Components: ghostslice; Flags: ignoreversion
Source: "third_party\R8BRAIN-MIT-LICENSE.txt"; DestDir: "{app}\licenses"; Components: ghostslice; Flags: ignoreversion

[Icons]
Name: "{group}\Mr. Mickens"; Filename: "{app}\Mr Mickens.exe"; Components: mickens
Name: "{group}\Poltergeist"; Filename: "{app}\Poltergeist.exe"; Components: poltergeist
Name: "{group}\Ghosty Beats"; Filename: "{app}\Ghosty Beats.exe"; Components: beats
Name: "{group}\Ghosty Spectrum"; Filename: "{app}\Ghosty Spectrum.exe"; Components: spectrum
Name: "{group}\Ghosty Pro"; Filename: "{app}\Ghosty Pro.exe"; Components: ghostypro
Name: "{group}\Ghost Box"; Filename: "{app}\Ghost Box.exe"; Components: ghostbox
Name: "{group}\ArpEZator"; Filename: "{app}\ArpEZator.exe"; Components: arpezator
Name: "{group}\Ghost Slice"; Filename: "{app}\Ghost Slice.exe"; Components: ghostslice

[Messages]
FinishedLabel=Done! Open your DAW and rescan plug-ins (FL Studio: Options > Manage plugins > Find more plugins). Each plugin opens in demo mode until you click UNLOCK and paste your license key.
