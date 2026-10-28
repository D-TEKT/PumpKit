# PumpKit: get it into FL Studio on your Mac

PumpKit is a sidechain-style volume shaper (the "pumping" effect). It is free to build, needs no Xcode and no coding. You upload the project to GitHub once, GitHub builds the plugin on its own Mac servers, and you download the finished file.

Total time: about 20 minutes, most of it waiting for the build.

---

## Part 1: Build it on GitHub (once)

1. **Make a free GitHub account** at github.com (skip if you have one).
2. **Create a repository.** Click the **+** at the top right, then **New repository**. Name it `PumpKit`, choose **Public** (public repos get unlimited free builds), and click **Create repository**.
3. **Upload the project.**
   - Unzip `PumpKit.zip` on your Mac and open the `PumpKit` folder.
   - On the new repo page, click **uploading an existing file**.
   - Select everything inside the folder (`CMakeLists.txt`, `Source`, `tests`, `github-workflow`, this guide) and drag it all into the browser window.
   - Wait for the upload bar, then click **Commit changes**.
4. **Add the build recipe.** (GitHub's upload page can't take hidden folders, so we make this one by hand.)
   - Open the `github-workflow` folder on your Mac, open `build-mac.yml` in TextEdit, select all and copy.
   - In your repo, click **Add file**, then **Create new file**.
   - In the name box, type exactly: `.github/workflows/build-mac.yml` (typing the slashes makes the folders).
   - Paste the copied text into the big box, then click **Commit changes** and confirm.
5. **Wait for the build.** Click the **Actions** tab. A run called "Build PumpKit (macOS VST3)" starts by itself. A yellow dot means it is running; it takes roughly 5 to 15 minutes the first time (it downloads and compiles the JUCE framework). A green tick means it worked.
6. **Download the plugin.** Click the finished run, scroll to **Artifacts** at the bottom, and click **PumpKit-VST3-macOS**. You get a zip. Unzip it, and unzip the zip inside it. You now have **`PumpKit.vst3`**.

## Part 2: Install it

1. In Finder, press **Shift + Cmd + G** and paste: `~/Library/Audio/Plug-Ins/VST3`
   (If Finder says the folder doesn't exist, create it: go to `~/Library/Audio/Plug-Ins/`, make a new folder called `VST3`, then go in.)
2. Drag `PumpKit.vst3` into that folder.
3. Open **Terminal** (Cmd + Space, type Terminal) and run this one line:

   ```
   xattr -cr ~/Library/Audio/Plug-Ins/VST3/PumpKit.vst3
   ```

   This tells macOS you trust the file. It is needed because the plugin isn't signed with a paid Apple developer account. Nothing prints when it works.

## Part 3: Load it in FL Studio

1. Open FL Studio, go to **Options, then Manage plugins**, and click **Find installed plugins** (start a scan). Make sure the VST3 folder above is in the search paths.
2. Put PumpKit on a mixer insert (click an empty effect slot and pick PumpKit from the list; it is under the installed/effect plugins, category Dynamics). Menu names change a little between FL versions, so if you can't find it, search for "PumpKit" in the plugin manager.

---

## Using it

| Control | What it does |
|---|---|
| **Mode: Sync** | Locks to the song tempo. Put it on your bass/pad/lead, press play, it pumps in time. Nothing to route. |
| **Mode: MIDI** | Every MIDI note-on starts the curve. Useful if you have a kick pattern in the piano roll. |
| **Mode: Audio** | Listens to a sidechain input (your kick) and starts the curve on every kick hit. |
| **Length** | How long one pump lasts (1/4 = one beat). For your 158 BPM track, 1/4 is the normal choice. |
| **Curve** | 16 built-in shapes. You can also **drag on the curve to draw your own**. Top = full volume, bottom = silent. The white dot shows where playback is. |
| **Mix** | How deep the ducking is: 100% = full duck, 0% = no effect. |
| **Smooth** | Rounds off hard edges of the curve (in ms). Raise it if you hear clicks. |
| **Band Split + Split Freq** | Only duck the frequencies *below* the split point (20 Hz to 5.12 kHz); everything above stays untouched. Great for ducking only the bass of a layered sound. |
| **Threshold / Retrigger** | Audio mode only. Threshold = how loud the kick must be to trigger. Retrigger = the minimum time between triggers. |

**Sync mode is the easiest, so start there.** Put PumpKit on a bass, set Length to 1/4, pick a curve, and press play.

**Notes on MIDI and Audio mode in FL Studio.** These need routing that I could not test in FL Studio. For MIDI: set a MIDI output port on the channel that plays your kick pattern and the same input port in PumpKit's plugin wrapper settings (the gear icon at the top left of the plugin window). For Audio: on the kick's mixer track, right-click the send knob of your PumpKit track and choose **Sidechain to this track**, then pick that sidechain input in the plugin wrapper settings. FL's manual covers both. If one of them gives you trouble, Sync mode always works.

---

## If something goes wrong

- **The Actions run turned red (X).** Click the run, click the failed step, and copy the red error text to me. I'll fix the code and give you the changed file to re-upload.
- **PumpKit doesn't show up in FL Studio.** Check the file is in `~/Library/Audio/Plug-Ins/VST3`, that you ran the `xattr` line, and run the plugin scan again. A restart of FL Studio helps too.
- **macOS says it is "damaged" or from an unidentified developer.** Run the `xattr` line again, or go to **System Settings, Privacy & Security** and click **Open Anyway**.
- **It loads but nothing seems to happen.** In Sync mode FL must be playing. Check that Mix is above 0 and the curve actually dips.

## Good to know

- This is **not** Kickstart 2 and shares none of its code or graphics. It copies the *idea* (tempo-synced volume shaping with MIDI and audio triggers, curves, band split). I built it from the public feature descriptions, so some of Kickstart's smaller features are missing (for example its oscilloscope view of the kick, and its exact control layout).
- **License:** PumpKit uses the JUCE framework, which is free for personal use. If you ever want to sell or release PumpKit publicly, check JUCE's license terms first.
- The engine (timing, curves, kick detection) has automated tests that pass. The full plugin has been checked to compile cleanly against JUCE, but it has **not** been run inside FL Studio, so expect to report back any quirks.
