; KoeNote の Windows インストーラー（Inno Setup 6）。
; VST3 を共通の VST3 フォルダ（C:\Program Files\Common Files\VST3）へ配置し、ライセンス文と説明書を
; C:\Program Files\KoeNote へ置く。ニューラル加工の onnxruntime.dll と重みは含めない（要件定義書 9章）。
;
; ビルド（リポジトリのルートで。CI の release ワークフローも同じコマンドを使う）:
;   iscc /DAppVersion=0.1.0 /DSourceRoot=<リポジトリのルート> /DVst3Dir=<KoeNote.vst3 のパス> /O<出力先> installer\KoeNote.iss

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef SourceRoot
  #define SourceRoot ".."
#endif
#ifndef Vst3Dir
  #define Vst3Dir SourceRoot + "\build\src\plugin\KoeNote_artefacts\Release\VST3\KoeNote.vst3"
#endif

[Setup]
; AppId は固定する（変えると上書き更新・アンインストールが別製品扱いになる）
AppId={{6F2C1E4A-6B1D-4C77-9C2E-4B6E0D9A1F53}
AppName=KoeNote
AppVersion={#AppVersion}
AppVerName=KoeNote {#AppVersion}
AppPublisher=KoeNote Project
AppPublisherURL=https://github.com/shiki-01/KoeNote
AppSupportURL=https://github.com/shiki-01/KoeNote/issues
AppUpdatesURL=https://github.com/shiki-01/KoeNote/releases
DefaultDirName={autopf}\KoeNote
DisableProgramGroupPage=yes
DisableDirPage=yes
LicenseFile={#SourceRoot}\LICENSE
OutputBaseFilename=KoeNote-{#AppVersion}-windows-x64-setup
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
; 共通の VST3 フォルダへ書き込むため、管理者権限で実行する
PrivilegesRequired=admin
MinVersion=10.0
UninstallDisplayName=KoeNote {#AppVersion}
WizardStyle=modern

[Languages]
Name: "ja"; MessagesFile: "compiler:Languages\Japanese.isl"
Name: "en"; MessagesFile: "compiler:Default.isl"

[Files]
; VST3 バンドル（フォルダ一式）
Source: "{#Vst3Dir}\*"; DestDir: "{commoncf64}\VST3\KoeNote.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs
; ライセンス・説明書
Source: "{#SourceRoot}\LICENSE"; DestDir: "{app}"; DestName: "LICENSE.txt"; Flags: ignoreversion
Source: "{#SourceRoot}\THIRD_PARTY_NOTICES.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceRoot}\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceRoot}\docs\user-guide.md"; DestDir: "{app}\docs"; Flags: ignoreversion
Source: "{#SourceRoot}\docs\neural-setup.md"; DestDir: "{app}\docs"; Flags: ignoreversion

[InstallDelete]
; 旧バージョンのバンドルに残っているファイルを消してから入れ直す
Type: filesandordirs; Name: "{commoncf64}\VST3\KoeNote.vst3"

[UninstallDelete]
Type: filesandordirs; Name: "{commoncf64}\VST3\KoeNote.vst3"
