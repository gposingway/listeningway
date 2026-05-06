## Pick your download

**Most users want x64.** Use x86 only if your game is 32-bit.

| Architecture     | Download                                                                                                                                              | Use when                                                                                                  |
| ---------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------- |
| **x64** (64-bit) | [Listeningway-x64-{{VERSION}}.zip](https://github.com/gposingway/listeningway/releases/download/v{{VERSION}}/Listeningway-x64-{{VERSION}}.zip)        | FFXIV, FFVII Remake, most modern AAA, anything from ~2015 onwards                                          |
| **x86** (32-bit) | [Listeningway-x86-{{VERSION}}.zip](https://github.com/gposingway/listeningway/releases/download/v{{VERSION}}/Listeningway-x86-{{VERSION}}.zip)        | Dead Cells, FFX/X-2 HD, Skyrim Legendary Edition, Dark Souls: PtDE, GTA San Andreas, most pre-2015 indies |

Not sure? Open Task Manager while the game is running. `*32` after the process name means **x86**, otherwise **x64**.

## Installation
1. Download the ZIP that matches your game's architecture.
2. Extract the contents of the ZIP. You will find these files:

    | File Name                  | Where to Place It          | Notes / Purpose                                    |
    | :------------------------- | :------------------------- | :------------------------------------------------- |
    | `Listeningway.addon`       | Game Directory             | ReShade loads `.addon` files from this directory.  |
    | `Listeningway.fx`          | `reshade-shaders/Shaders/` | The example shader effect file.                    |
    | `ListeningwayUniforms.fxh` | `reshade-shaders/Shaders/` | Include file needed by shaders using `#include`.   |

   *(Reminder: The `.addon` file goes directly into your main game folder with the ReShade DLL, not inside `reshade-shaders`!)*
3. Restart your game or application.

## Notes
- For more information and troubleshooting, see the [GitHub repository](https://github.com/gposingway/listeningway).
- The 32-bit (x86) build target was contributed by [@slendereater-sketch](https://github.com/slendereater-sketch); see the project README for full credits.
