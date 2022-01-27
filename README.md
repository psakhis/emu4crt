# **Mednafen 1.29 - emu4crt - A18**

# Important: This is is updated project of @Silmalik emu4crt. All information on https://github.com/emu4crt/files Thanks for it!!

emu4crt is a Mednafen emulator mod intended to be used on a system connected to a CRT screen, typically a 15kHz TV or an arcade monitor.

It brings "pixel-perfect" rendering by switching host resolution to match emulated system resolution.

* Mednafen modules ported to emu4crt: 
  * Sony Playstation
  * Sega Saturn (win64 only)
  * Nintendo NES/Famicon (snes & snes_faust)
  * Nintendo Super NES/Super Famicom
  * NEC PC Engine / PC Engine CD / SuperGrafx (pce & pce_fast)
  * NEC PC-FX
  * Sega Megadrive / Genesis
  * Sega Master System

Many options, meaningless in a CRT screen usage, have been removed from Mednafen in provided emu4crt builds (shaders, etc.).

* Requirements:
  * OS: Windows 32/64bits
  * Video display: OpenGL compatible (the only Mednafen tested renderer).
  
emu4crt can be use in three modes:

* `Native Resolution`: Same resolution as emulated system.
   More custom resolution are required (see below)
   Generates more resolution changes, which has side effects

* `Super Resolution`: Requires only four 2560 pixel width resolutions.
   Avoid some resolution change during emulation

* `Switchres`: Resolutions changes with libswitchres library of @Calamity. 
  More information on their github https://github.com/antonioginer/switchres 

## Required resolutions

### `Native resolutions`

* Columns:
  
|       |256|320|341|352|368|512|640|704|
|:------|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|
|NES*   | X |   |   |   |   |   |   |   |
|SNES   | X |   |   |   |   | X |   |   |
|SATURN |   | X |   | X |   |   | X | X |
|PSX    | X | X |   |   | X | X | X |   |
|PCE**  | X | X |   |   |   | X |   |   |
|PCFX** | X |   | X |   |   |   |   |   |
|MD/SMS | X | X |   |   |   |   |   |   |

* Lines, for each column:

   NTSC modes : 240 and 480 lines @ 60 Hz 
   PAL modes: 288 and 576 lines @ 50 Hz

   __*__ -> 240 and 288 lines only

   __**__ -> NTSC modes only

### `Super Resolutions`

   4 custom resolutions cover all needs for every emulated systems:

|      | Columns  |  Lines    |  Frequency  |
|:-----| :------: | :-------: | :---------: |
| NTSC |   2560   |  240      |     60Hz    |
|      |   2560   |  480      |     60Hz    |
| PAL  |   2560   |  288      |     50Hz    |
|      |   2560   |  576      |     50Hz    |

Custom resolutions can be added on Windows by using Calamity's CRT Emudriver, Soft15Hz, Powerstrip, manufacturer drivers...

For CRT Emudriver users:
 * emu4crt_NATIVE_RESOLUTIONS.txt contains all resolution informations to be added to VMMAKER's user_modes.ini.
 * emu4crt_RESOLUTIONS_SUPER.txt

For testing pupose, emu4crt can be used with a standard PC screen and video drivers in window mode.

## Configuration & usage

To enable resolution switch, use "video.resolution_switch" parameter in mednafen.cfg configuration file:

* video.resolution_switch native -> to use native resolution mode
* video.resolution_switch super  -> to use super resolution mode
* video.resolution_switch switchres -> to use libswitchres mode (you need to configure switchres.ini properly!!)
* video.resolution_switch 0 -> to disable resolution switch [DEFAULT MODE]

emu4crt.exe can be placed in an existing mednafen.exe directory, both can share the same configuration file and all ressource files (firmwares, savestates, etc.).

## Limits and known issues

- For native and super modes, the emulator does not deal with resolution refresh rate. So, to get a deterministic behavior, a resolution must only exist at the expected refresh rate (ie. no 320x240 @ 55Hz). With switchres mode, refresh tries to match with expected refresh rate.
  
- Using emu4crt, resolution switch is not seamless process as it is on a console or with some other emulators. At each resolution change, some sound and graphical glitches will occur. 
  
- Graphical glitches can be limited by using full black Windows desktop background and taskbar hidding. 
  
- Windows 7 seems quicker than Windows 10 to switch resolution, at least with CRT Emudriver.
  
- Emulation logic is preserved, so, expected game compatibility is same as with the Mednafen official release.
  
If any specific issue with emu4crt mod, of course, do not bother the Mednafen Team!

Contact forum thread:
http://forum.arcadecontrols.com/index.php/topic,155264.0.html

## Thanks

The Mednafen Team (https://mednafen.github.io)  for... Mednafen!

CRT Emudriver's and libswitchres author, Calamity (http://geedorah.com/eiusdemmodi/forum/)

Silmalik for this great emu4crt project!

ArcadeControls.com (www.arcadecontrols.com)

No$psx author, Martin Korth for publishing PSX GPU documentation

hotdog963al for his PSX core horizontal centering improvment
