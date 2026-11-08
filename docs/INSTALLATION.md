# Install, update and add DLCs

This guide is for **RedBaron_Recomp v0.23 Public Test** on Windows. The launcher interface and the button names below are in English.

## Start here

| Your situation | Follow these steps |
|---|---|
| You have never installed the game, or an earlier installation failed | [Install the game](#install-the-game-or-retry-a-failed-installation) in a new, empty folder |
| Your v0.10 or v0.20 installation already works | [Move your existing installation to v0.23](#move-a-working-v010-or-v020-installation-to-v023) |
| You use v0.21 or newer and want to install v0.23 or a later release | [Install future updates](#install-future-updates-from-v021-onward) |
| You want to add your own DLC packages | [Add DLCs](#add-dlcs-to-an-installed-game) |

## What you need

- Windows 10/11 **64-bit**, .NET Framework 4.8, an updated graphics driver and a Direct3D 12-capable graphics card.
- Your own compatible **Snoopy Flying Ace** Xbox 360 game files (Title ID `584109B5`). Neither download includes the game or DLCs.
- Free space for the game, temporary build files and a previous-version backup when updating. Installation and updates need more space than the finished game.
- Internet access for missing tools and online features. The offline download includes build tools, but Epic online features still need an Internet connection. Microsoft tools may request administrator approval or a restart.

The installer compiles the game on your computer. You do not need to enter commands or select a compiler yourself. Preparation can take several minutes; watch the current stage and progress in the launcher.

## Choose and extract a download

Open the [v0.23 release page](https://github.com/BTr-RJ/RedBaron_Recomp/releases/tag/v0.23) and expand **Assets** if the downloads are hidden.

| Download | Choose this when… |
|---|---|
| `RedBaron_Recomp-v0.23-windows-x64.zip` | You want the smaller, recommended download. Missing build tools are downloaded when needed. |
| `RedBaron_Recomp-v0.23-windows-x64-offline.zip` | You want build tools included in the download. It contains the same game port as the smaller package. |
| `SHA256SUMS.txt` | You want to check the ZIP's file checksum yourself. The launcher updater already checks downloaded packages automatically. |

1. Download **one** of the Windows ZIPs. GitHub's **Source code** downloads are for developers; they are not the player installer.
2. In File Explorer, right-click the ZIP and select **Extract All**. Extract it into its own folder, separate from your installed game; for example, `Desktop\RedBaron-v0.23-Installer`.
3. Open the extracted folder and find `RedBaron_Launcher.exe`. Do not run it from inside the ZIP. Keep `RedBaron_Launcher.exe.config` beside it.
4. If you chose the offline ZIP, also keep its entire **Tools** folder beside the launcher. Do not move only the EXE out of that folder.

## Install the game, or retry a failed installation

Use these steps when there is no complete working installation. If your older version already plays correctly, use the migration steps in the next section instead.

1. Open the extracted **v0.23** `RedBaron_Launcher.exe` and select **Install**.
2. Under **Game files**, choose your extracted game folder, its `default.xex`, or your compatible original Xbox 360 content package. Content packages without a file extension are accepted. The installer checks that the files belong to this game.
3. Optional: under **DLC files**, choose the folder containing your supported gameplay DLC packages. You can leave this empty and add DLCs later.
4. Choose a **new, empty installation folder**, such as `C:\Games\RedBaron`. Choose a different folder if that example already contains files. Keep the installer download folder and the game installation folder separate.
5. Click **Install** once. Allow the Microsoft tools prompt if requested. Wait while the launcher prepares tools, copies or extracts your game files and builds the game. Keep the launcher open until it finishes.
6. When installation is complete, select **Game language**, set your preferred **Video** and **Controls**, and enter your **Player name** under **Online**.
7. Click **Save**, then **Play**. Use the installed launcher or its desktop shortcut next time.

**If v0.10 or v0.20 failed during compilation:** follow these same steps with v0.23 and a new empty destination. Keep your original game files and any saves. Do not reuse the failed build folder as the new destination: it can retain the incompatible compiler settings. You do not need to install v0.20 first.

<a id="move-a-working-v010-or-v020-installation-to-v021"></a>
<a id="move-a-working-v010-or-v020-installation-to-v022"></a>

## Move a working v0.10 or v0.20 installation to v0.23

This is a **one-time manual transition**. Already distributed v0.10/v0.20 launchers can download the new installer, but cannot apply the update themselves.

1. Close the game. Open your old launcher and select **Update > Check for updates**. If your shortcut starts the game directly, hold **Shift** while opening it to show the launcher.
2. Click **Download update** and wait for the verified download to finish. Click **Open downloads folder** to find the ZIP. Alternatively, download either v0.23 Windows ZIP directly from the [release page](https://github.com/BTr-RJ/RedBaron_Recomp/releases/tag/v0.23).
3. **Close the old launcher.** Extract the new ZIP into a separate installer folder, following [the extraction steps above](#choose-and-extract-a-download). Keep the offline package's **Tools** folder beside its launcher.
4. Open the **new extracted v0.23 launcher**. Select **Install > Use existing installation…**.
5. Select your **existing installed game folder**. It is the folder containing both `game` and `build_cpp_release`, usually with `RedBaron_Launcher.exe` beside them. For example, choose `C:\Games\RedBaron`, not `C:\Games\RedBaron\build_cpp_release` and not the newly extracted installer folder.
6. Confirm that the launcher shows **Installation found**. Open **Update** and click **Update selected installation**. This button is provided by the full installer; the ordinary Install action on an existing game is for adding DLCs.
7. Wait for the update window to prepare and check the new build. It uses the game files already in your installation. You do not need to select the original game files again.
8. The updater applies the new components and **reopens the installed launcher automatically**. Start the game through your usual shortcut or the launcher in the existing game folder.

Your game files, installed DLCs, saves, existing settings and installation location are kept. Future releases can be installed using the new updater described below. If your old installation never finished, use the fresh-install steps instead of **Use existing installation…**.

The **Launcher version** shown in the extracted installer identifies that installer. Selecting an older game folder does not update it until **Update selected installation** finishes.

## Install future updates from v0.21 onward

**Already using v0.21 or v0.22? These steps install v0.23 automatically after you click Download and install update.** No ZIP extraction or game-file selection is needed.

1. Close the game and open your **installed** launcher. Hold **Shift** when opening the shortcut if it normally starts the game directly.
2. Save any settings you have changed. Open **Update** and click **Check for updates**.
3. If a newer release is available, click **Download and install update** once. This is the action that starts the download; checking for updates alone does not download or install anything.
4. Wait while the download is verified. The launcher then opens the updater, prepares the new game build in a temporary folder and checks the new runtime.
5. When the update is ready, the updater replaces the installed components and reopens the launcher. Click **Play** when you are ready.

For these future updates, you do not have to find or extract a ZIP, choose your game files again, or select an installation folder. The updater chooses the smaller standard package and reuses compatible cached build tools. Missing dependencies may still need Internet access and Microsoft approval.

The original v0.22 displayed **Launcher version: 0.21** even though its updater already used **0.22.0**. v0.23 corrects this label and includes the running version in the no-newer-release message. This display defect does not establish the cause of every reported update-check failure.

**No newer public release is available** means this launcher is up to date. A v0.23 launcher will not offer v0.23 as another update. Published test releases are included in update checks.

### If you cancel, lose power or see an update error

- You can use **Cancel update** while the new build is being prepared. Your installed version remains in place during preparation.
- Once file replacement starts, wait for it to finish. Cancellation is disabled for that step.
- A file-replacement failure attempts to restore the previous version from the update backup.
- If the process is interrupted, reopen the installed launcher. It starts recovery before allowing Play. Wait for recovery to finish.
- If recovery reports an error, keep the backup and working folder. Open the updater's **Open logs** button and report the error with `update.log`.

Update backups and logs are stored in your installation under `launcher-data\update-<id>`. Do not delete a pending update's folder while an update or recovery is in progress. If the updater has closed, open that location in File Explorer and look for the most recently modified update folder.

## Add DLCs to an installed game

The supported gameplay packs are **Dogs of War Map Pack** and **Suppertime of Destruction Pack**. You provide the original DLC packages. Avatar items are not supported.

1. Close the game and open the installed launcher. Hold **Shift** while opening the shortcut if needed.
2. Open **Install** and check that **Installation found** is shown. If using a separate full installer, use **Use existing installation…** to select your installed game first.
3. Under **DLC files**, select the folder containing your DLC packages. You can select a parent folder containing both packs.
4. Click **Install DLCs** and wait for completion. You do not need to select the base game source again.
5. Click **Play**. DLC installation adds the content to the runtime's user-data location. The game executable is not rebuilt by this DLC-only action.

Existing installed packs are recognized and skipped. A later game update preserves installed DLCs. If you are installing the base game for the first time, you can select the same DLC folder in step 3 of the fresh-install instructions and install both in one workflow.

Local testing covered installation of both gameplay packs, repeat installation and preservation after a game update. Gameplay in every added map or with every added item still needs player testing.

## Game language, controls and online play

The launcher uses the game's seven original languages: English, French, German, Italian, Japanese, Portuguese and Spanish. Select **Game language**, click **Save** and restart the game. Portuguese is the original game localization. The launcher stays in English.

Use the game's **FlyingAce** control preset. **W/A/S/D** navigate, **Space** confirms, **Backspace** goes back and **Enter/Esc** send Start. Flight uses WASD, arrows for maneuvers, mouse buttons or J/K for weapons, Space for boost, C for brake and E for action. Mouse movement does not steer the aircraft. See [CONTROLS.txt](../port/config/CONTROLS.txt) for the full mapping.

For controller plus keyboard in local play, select **Exclusive player 2** for the keyboard and keep player 1 on the controller. Save, restart and join through the local menu. Two native split-screen cameras are supported.

Sign in to Epic when prompted for online features. Create a public Player Match or use Quick Game / Custom Match to find compatible hosts. Private matches use Epic invitations; RT in the hangar/lobby opens friends, and **Shift+F3** opens the Epic overlay. Co-op has its own two-player category. No room-server address is needed. For the November 7 Epic access update and retry instructions, see [Online](EOS.md#epic-sign-in-update--november-7-2026). Offline play remains available when authentication is unavailable.

## Where your files and logs are kept

| Files | Location |
|---|---|
| Installed launcher | `RedBaron_Launcher.exe` in your game installation folder |
| Runtime settings | `build_cpp_release\snoopyflyingace.toml` in that folder |
| Launcher settings | `launcher-settings.json` in that folder |
| Game/session and installation logs | `launcher-data\logs`; use **Update > Open logs folder** |
| Update backup and update log | `launcher-data\update-<id>`; use the updater's **Open logs** button |
| Saves, profiles and installed DLC content | The runtime user-data folder, normally in your Documents area; an explicit `user_data_root` setting takes precedence |

Successful installation removes owned temporary compilation files and keeps reusable tools in the local application-data cache. Update backups are kept for recovery. Keep your original game/DLC packages and saves.

## Troubleshooting and reporting a problem

- **Clang missing or outdated after preparing tools:** v0.23 fixes a reproduced tool-selection conflict where a shared CMake/Ninja directory hid the verified compatible Clang after the Visual Studio environment was imported. For a failed installation, use the v0.23 installer in a new empty destination. If it still fails, include the final tool names, versions and selected paths from the installation log; the cause on every affected PC has not been confirmed.
- **Epic sign-in: Application Access Is Restricted:** the project's Epic application-access restriction likely affected external accounts. The owner reports the RedBaron_Recomp brand is now published. Close and reopen the game and retry sign-in; a working installation does not need reinstalling for this service-side update. External-account success still needs player confirmation. See the [Epic sign-in update](EOS.md#epic-sign-in-update--november-7-2026) to report success or a continuing failure.
- **LLVM extraction error mentioning `xz -d -qq` in an older installer:** use the **v0.23** standard or offline installer in a **new, empty installation folder**. v0.23 extracts LLVM with CMake and does not require external `tar` or `xz` for that step. Keep the failed installation and its log for diagnosis. You cannot update an installation that never finished.
- **Compilation failed in v0.10:** use the v0.23 installer in a new empty destination. It includes the v0.20 fix for incompatible Clang selection, including errors mentioning `std::chrono::clock_time_conversion`.
- **Update selected installation is missing:** open the extracted **full v0.23 installer**, select **Install > Use existing installation…**, choose the folder containing `game` and `build_cpp_release`, and return to **Update**. The lightweight installed launcher uses **Download and install update** for newer releases instead.
- **Another launcher is open:** close the other launcher for this game installation, then retry. Close the game before updating or adding DLCs.
- **White 3D scene:** keep `render_target_path_d3d12 = "rov"` in the runtime settings.
- **Temporal image artifacts:** try Bilinear, FSR 1 or CAS. HDR requires Windows HDR to be enabled.
- **Administrator approval or restart requested:** Microsoft/EOS dependencies can require this during setup. The game does not need permanent administrator execution.

Report problems through [GitHub Issues](https://github.com/BTr-RJ/RedBaron_Recomp/issues). Include the version, standard/offline package, Windows version, hardware, the step that failed and the relevant log. Remove tokens and personal information. **Do not upload the entire installation or update folder, game/DLC files, saves, generated sources, or `eos/config.ini`.**
