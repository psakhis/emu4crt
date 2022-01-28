/* Mednafen - Multi-system Emulator
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */


#include "main.h"

#ifdef WIN32
 #include <SDL_syswm.h>
 #include <mednafen/win32-common.h>
#else
 #include "icon.h"
#endif

#include <trio/trio.h>

#include "video.h"
#include "opengl.h"
#include "shader.h"
#include "nongl.h"

#include "netplay.h"
#include "cheat.h"

#include "nnx.h"
#include "debugger.h"
#include "fps.h"
#include "help.h"
#include "video-state.h"

#ifdef WANT_FANCY_SCALERS
#include "scalebit.h"
#include "hqxx-common.h"
#include "2xSaI.h"
#endif

#include <stdio.h> //psakhis 
#include <stdlib.h> //psakhis
#include <mednafen/switchres/switchres_wrapper.h> //psakhis

class SDL_to_MDFN_Surface_Wrapper : public MDFN_Surface
{
 public:
 INLINE SDL_to_MDFN_Surface_Wrapper(SDL_Surface *sdl_surface) : ss(sdl_surface)
 {
  // Locking should be first thing.
  if(SDL_MUSTLOCK(ss))
   SDL_LockSurface(ss);

  format = MDFN_PixelFormat(MDFN_COLORSPACE_RGB,
	ss->format->BitsPerPixel >> 3,
	ss->format->Rshift,
	ss->format->Gshift,
	ss->format->Bshift,
	ss->format->Ashift,
	8 - ss->format->Rloss,
	8 - ss->format->Gloss,
	8 - ss->format->Bloss,
	8 - ss->format->Aloss);

  pixels_is_external = true;

  pixels16 = NULL;
  pixels = NULL;

  if(ss->format->BitsPerPixel == 16)
  {
   pixels16 = (uint16*)ss->pixels;
   pitchinpix = ss->pitch >> 1;
  }
  else
  {
   pixels = (uint32*)ss->pixels;
   pitchinpix = ss->pitch >> 2;
  }

  w = ss->w;
  h = ss->h;
 }

 INLINE ~SDL_to_MDFN_Surface_Wrapper()
 {
  if(SDL_MUSTLOCK(ss))
   SDL_UnlockSurface(ss);
  ss = NULL;
 }
 private:
 SDL_Surface *ss;
};

enum
{
 VDRIVER_OPENGL = 0,
 VDRIVER_SOFTSDL = 1,
 VDRIVER__COUNT
};

// SLK
static int _resolution_switch;
enum
{
 RES_STATIC = 0,
 RES_NATIVE = 1,
 RES_SUPER = 2,
 RES_SWITCHRES = 3 //psakhis
};
// SLK - end

unsigned char retSR; //psakhis

enum
{
 NTVB_NONE = 0,

 NTVB_HQ2X,
 NTVB_HQ3X,
 NTVB_HQ4X,

 NTVB_SCALE2X,
 NTVB_SCALE3X,
 NTVB_SCALE4X,

 NTVB_NN2X,
 NTVB_NN3X,
 NTVB_NN4X,

 NTVB_NNY2X,
 NTVB_NNY3X,
 NTVB_NNY4X,

 NTVB_2XSAI,
 NTVB_SUPER2XSAI,
 NTVB_SUPEREAGLE,
};

static const MDFNSetting_EnumList VDriver_List[] =
{
 { "default", VDRIVER__COUNT, gettext_noop("Default"), gettext_noop("Selects the default video driver.  Currently, this is OpenGL for all platforms, but may change in the future if better platform-specific drivers are added.") },

 { "opengl", VDRIVER_OPENGL, "OpenGL", gettext_noop("All video-related Mednafen features are available with this video driver.") },
 { "softfb", VDRIVER_SOFTSDL, "Software Blitting to Framebuffer", gettext_noop("Slower with lower-quality scaling than OpenGL, but if you don't have hardware-accelerated OpenGL rendering, it will probably be faster than software OpenGL rendering. Bilinear interpolation not available. OpenGL shaders do not work with this output method, of course.") },

 // Backwards-compat:
 { "0", VDRIVER_OPENGL },
 { "1", VDRIVER_SOFTSDL },
 { "sdl", VDRIVER_SOFTSDL },
 { "overlay", VDRIVER__COUNT },

 { NULL, 0 },
};

enum
{
 CURSORVIS_HIDDEN = 0,
 CURSORVIS_VISIBLE
};

static const MDFNSetting_EnumList CursorVis_List[] =
{
 { "hidden", CURSORVIS_HIDDEN, gettext_noop("Hidden") },
 { "visible", CURSORVIS_VISIBLE, gettext_noop("Visible") },

 { "0", CURSORVIS_HIDDEN },
 { "1", CURSORVIS_VISIBLE },

 { NULL, 0 }
};

static const MDFNSetting_EnumList GLFormat_List[] =
{
 { "auto", EVFSUPPORT_NONE, gettext_noop("Auto"), gettext_noop("Currently the same as \"truecolor\", but may automatically select deeper color formats in the future.") },
 //{ "deepcolor", EVFSUPPORT_RGB101010, gettext_noop("Deepcolor, 1B colors"), gettext_noop("RGB, 10 bits per color component.") },
 { "truecolor", EVFSUPPORT_NONE, gettext_noop("Truecolor, 16M colors"), gettext_noop("RGB, 8 bits per color component.") },
 { "hicolor", EVFSUPPORT_RGB555 | EVFSUPPORT_RGB565, gettext_noop("Hicolor, 32K/64K colors"), gettext_noop("RGB565 or RGB555, with priority given to RGB565.") },

 { "rgb565", EVFSUPPORT_RGB565, gettext_noop("RGB565, 64K colors") },
 { "rgb555", EVFSUPPORT_RGB555, gettext_noop("RGB555, 32K colors") },

 { NULL, 0 },
};

// SLK - custom setting
static const MDFNSetting_EnumList Resolution_Switch[] =
{
 // Legacy:
 { "0", RES_STATIC},
 { "native", RES_NATIVE, "Native resolutions", gettext_noop("Use emulated system native resolution for output") },
 { "super", RES_SUPER, "Super resolutions", gettext_noop("Use super resolutions for output") },
 { "switchres", RES_SWITCHRES, "Switchres 2", gettext_noop("Use switchres 2 for output") }, //psakhis
 { NULL, 0 }
};
// SLK - end

static const MDFNSetting GlobalVideoSettings[] =
{
 { "video.driver", MDFNSF_NOFLAGS, gettext_noop("Video output driver."), NULL, MDFNST_ENUM, "default", NULL, NULL, NULL, NULL, VDriver_List },

 { "video.fs", MDFNSF_NOFLAGS, gettext_noop("Enable fullscreen mode."), NULL, MDFNST_BOOL, "0", },
 { "video.fs.display", MDFNSF_NOFLAGS, gettext_noop("Display to use with fullscreen mode."), gettext_noop("Specify -1 to use the display on which the center of the window lies in windowed mode."), MDFNST_INT, "-1", "-1", "32767" },

 //{ "video.window.x", MDFNSF_NOFLAGS, gettext_noop("Window starting X position."), NULL, MDFNST_INT, "0x2FFF0000" },
 //{ "video.window.y", MDFNSF_NOFLAGS, gettext_noop("Window starting Y position."), NULL, MDFNST_INT, "0x2FFF0000" },

 { "video.cursorvis", MDFNSF_NOFLAGS, gettext_noop("Preferred window manager cursor visibility."), gettext_noop("The cursor will still be forcibly hidden in relative mouse mode(used automatically when emulating a mouse input device in fullscreen mode or in windowed mode and input grabbing is toggled on), and forcibly shown in the debugger."), MDFNST_ENUM, "hidden", NULL, NULL, NULL, NULL, CursorVis_List },

 { "video.glformat", MDFNSF_NOFLAGS, gettext_noop("Preferred source data pixel format for emulated video."), gettext_noop("Using hicolor(RGB555/RGB565) formats may boost performance, but at the cost of color fidelity.  Noticeability of color degradation depends on the emulated system and features, such as custom palettes or NTSC blitter, being used.  When hicolor format support is not available for the emulation module or special scaler being used, Mednafen will automatically fall back to using a truecolor format.  Examine Mednafen's startup output to see the actual OpenGL texture formats being used."), MDFNST_ENUM, "auto", NULL, NULL, NULL, NULL, GLFormat_List },

 { "video.force_bbclear", MDFNSF_NOFLAGS, gettext_noop("Force backbuffer clear before drawing."), gettext_noop("Enabling may result in a noticeable negative impact on performance with the \"softfb\" video driver, and with the \"opengl\" video driver on underpowered GPUs."), MDFNST_BOOL, "0" },

 { "video.glvsync", MDFNSF_NOFLAGS, gettext_noop("Attempt to synchronize OpenGL page flips to vertical retrace period."), 
			       gettext_noop("Note: Additionally, if the environment variable \"__GL_SYNC_TO_VBLANK\" does not exist, then it will be created and set to the value specified for this setting.  This has the effect of forcibly enabling or disabling vblank synchronization when running under Linux with NVidia's drivers."),
				MDFNST_BOOL, "1" },

 { "video.disable_composition", MDFNSF_NOFLAGS, gettext_noop("Attempt to disable desktop composition."), gettext_noop("Currently, this setting only has an effect on Windows Vista and Windows 7(and probably the equivalent server versions as well)."), MDFNST_BOOL, "1" },
 // SLK - admit new parameter
 { "video.resolution_switch", MDFNSF_NOFLAGS, gettext_noop("Video resolution switch (0, native, super or switchres)."), NULL, MDFNST_ENUM, "0", NULL, NULL, NULL, NULL, Resolution_Switch},
 // SLK - end

 { NULL }
};

static const MDFNSetting_EnumList StretchMode_List[] =
{
 { "0", 0, gettext_noop("Disabled") },
 { "off", 0 },
 { "none", 0 },

 { "1", 1 },
 { "full", 1, gettext_noop("Full"), gettext_noop("Full-screen stretch, disregarding aspect ratio.") },

 { "2", 2 },
 { "aspect", 2, gettext_noop("Aspect Preserve"), gettext_noop("Full-screen stretch as far as the aspect ratio(in this sense, the equivalent xscalefs == yscalefs) can be maintained.") },

 { "aspect_int", 3, gettext_noop("Aspect Preserve + Integer Scale"), gettext_noop("Full-screen stretch, same as \"aspect\" except that the equivalent xscalefs and yscalefs are rounded down to the nearest integer.") },
 { "aspect_mult2", 4, gettext_noop("Aspect Preserve + Integer Multiple-of-2 Scale"), gettext_noop("Full-screen stretch, same as \"aspect_int\", but rounds down to the nearest multiple of 2.") },

 { NULL, 0 },
};

static const MDFNSetting_EnumList VideoIP_List[] =
{
 { "0", VIDEOIP_OFF, gettext_noop("Disabled") },

 { "1", VIDEOIP_BILINEAR, gettext_noop("Bilinear") },

 // Disabled until a fix can be made for rotation.
 { "x", VIDEOIP_LINEAR_X, gettext_noop("Linear (X)"), gettext_noop("Interpolation only on the X axis.") },
 { "y", VIDEOIP_LINEAR_Y, gettext_noop("Linear (Y)"), gettext_noop("Interpolation only on the Y axis.") },

 { NULL, 0 },
};

static const MDFNSetting_EnumList Special_List[] =
{
    { "0", 	NTVB_NONE },
    { "none", 	NTVB_NONE, "None/Disabled" },

#ifdef WANT_FANCY_SCALERS
    { "hq2x", 	NTVB_HQ2X, "hq2x" },
    { "hq3x", 	NTVB_HQ3X, "hq3x" },
    { "hq4x", 	NTVB_HQ4X, "hq4x" },
    { "scale2x",NTVB_SCALE2X, "scale2x" },
    { "scale3x",NTVB_SCALE3X, "scale3x" },
    { "scale4x",NTVB_SCALE4X, "scale4x" },

    { "2xsai", 	NTVB_2XSAI, "2xSaI" },
    { "super2xsai", NTVB_SUPER2XSAI, "Super 2xSaI" },
    { "supereagle", NTVB_SUPEREAGLE, "Super Eagle" },
#endif

    { "nn2x",	NTVB_NN2X, "Nearest-neighbor 2x" },
    { "nn3x",	NTVB_NN3X, "Nearest-neighbor 3x" },
    { "nn4x",	NTVB_NN4X, "Nearest-neighbor 4x" },
    { "nny2x",	NTVB_NNY2X, "Nearest-neighbor 2x, y axis only" },
    { "nny3x",	NTVB_NNY3X, "Nearest-neighbor 3x, y axis only" }, 
    { "nny4x",	NTVB_NNY4X, "Nearest-neighbor 4x, y axis only" },

    { NULL, 0 },
};

static const MDFNSetting_EnumList Shader_List[] =
{
    { "none",		SHADER_NONE,		gettext_noop("None/Disabled") },
    { "autoip", 	SHADER_AUTOIP,		gettext_noop("Auto Interpolation"), gettext_noop("Will automatically interpolate on each axis if the corresponding effective scaling factor is not an integer.") },
    { "autoipsharper",	SHADER_AUTOIPSHARPER,	gettext_noop("Sharper Auto Interpolation"), gettext_noop("Same as \"autoip\", but when interpolation is done, it is done in a manner that will reduce blurriness if possible.") },
    { "scale2x", 	SHADER_SCALE2X,    	"Scale2x" },
    { "sabr",		SHADER_SABR,		"SABR v3.0", gettext_noop("GPU-intensive.") },
    { "ipsharper", 	SHADER_IPSHARPER,  	gettext_noop("Sharper bilinear interpolation.") },
    { "ipxnoty", 	SHADER_IPXNOTY,    	gettext_noop("Linear interpolation on X axis only.") },
    { "ipynotx", 	SHADER_IPYNOTX,    	gettext_noop("Linear interpolation on Y axis only.") },
    { "ipxnotysharper", SHADER_IPXNOTYSHARPER, 	gettext_noop("Sharper version of \"ipxnoty\".") },
    { "ipynotxsharper", SHADER_IPYNOTXSHARPER, 	gettext_noop("Sharper version of \"ipynotx\".") },

    { "goat", 		SHADER_GOAT, 		gettext_noop("Simple approximation of a color TV CRT look."), gettext_noop("Intended for fullscreen modes with a vertical resolution of around 1000 to 1500 pixels.  Doesn't simulate halation and electron beam energy distribution nuances.") },

    { NULL, 0 },
};

static const MDFNSetting_EnumList GoatPat_List[] =
{
	{ "goatron",	ShaderParams::GOAT_MASKPAT_GOATRON,	gettext_noop("Goatron"), gettext_noop("Brightest.") },
	{ "goattron",	ShaderParams::GOAT_MASKPAT_GOATRON },
	{ "goatronprime",	ShaderParams::GOAT_MASKPAT_GOATRONPRIME },
	{ "goattronprime",	ShaderParams::GOAT_MASKPAT_GOATRONPRIME },

	{ "borg",	ShaderParams::GOAT_MASKPAT_BORG,	gettext_noop("Borg"), gettext_noop("Darkest.") },
	{ "slenderman",	ShaderParams::GOAT_MASKPAT_SLENDERMAN,	gettext_noop("Slenderman"), gettext_noop("Spookiest?") },

	{ NULL, 0 },
};

void Video_MakeSettings(void)
{
 static const char *CSD_xres = gettext_noop("Full-screen horizontal resolution.");
 static const char *CSD_yres = gettext_noop("Full-screen vertical resolution.");
 static const char *CSDE_xres = gettext_noop("A value of \"0\" will cause the current desktop horizontal resolution to be used.");
 static const char *CSDE_yres = gettext_noop("A value of \"0\" will cause the current desktop vertical resolution to be used.");

 static const char *CSD_xscale = gettext_noop("Scaling factor for the X axis in windowed mode.");
 static const char *CSD_yscale = gettext_noop("Scaling factor for the Y axis in windowed mode.");

 static const char *CSD_xscalefs = gettext_noop("Scaling factor for the X axis in fullscreen mode.");
 static const char *CSD_yscalefs = gettext_noop("Scaling factor for the Y axis in fullscreen mode.");
 static const char *CSDE_xyscalefs = gettext_noop("For this setting to have any effect, the \"<system>.stretch\" setting must be set to \"0\".");

 static const char *CSD_scanlines = gettext_noop("Enable scanlines with specified opacity.");
 static const char *CSDE_scanlines = gettext_noop("Opacity is specified in %; IE a value of \"100\" will give entirely black scanlines.\n\nNegative values are the same as positive values for non-interlaced video, but for interlaced video will cause the scanlines to be overlaid over the previous(if the video.deinterlacer setting is set to \"weave\", the default) field's lines.");

 static const char *CSD_stretch = gettext_noop("Stretch to fill screen.");
 static const char *CSDvideo_settingsip = gettext_noop("Enable (bi)linear interpolation.");

 static const char *CSD_special = gettext_noop("Enable specified special video scaler.");
 static const char *CSDE_special = gettext_noop("The destination rectangle is NOT altered by this setting, so if you have xscale and yscale set to \"2\", and try to use a 3x scaling filter like hq3x, the image is not going to look that great. The nearest-neighbor scalers are intended for use with bilinear interpolation enabled, for a sharper image, though the \"autoipsharper\" shader may provide better results.");

 static const char *CSD_shader = gettext_noop("Enable specified OpenGL shader.");
 static const char *CSDE_shader = gettext_noop("Obviously, this will only work with the OpenGL \"video.driver\" setting, and only on cards and OpenGL implementations that support shaders, otherwise you will get a black screen, or Mednafen may display an error message when starting up. When a shader is enabled, the \"<system>.videoip\" setting is ignored.");

 for(unsigned int i = 0; i < MDFNSystems.size() + 1; i++)
 {
  int nominal_width;
  //int nominal_height;
  const char* default_videoip;
  const char *sysname;
  char default_value[256];
  const int default_xres = 0, default_yres = 0;
  const double default_scalefs = 1.0;
  double default_scale;

  if(i == MDFNSystems.size())
  {
   nominal_width = 384;
   //nominal_height = 240;
   default_videoip = "0";
   sysname = "player";
  }
  else
  {
   const int mr = MDFNSystems[i]->multires;

   nominal_width = MDFNSystems[i]->nominal_width;
   //nominal_height = MDFNSystems[i]->nominal_height;

   if(mr < 0)
    default_videoip = "x";
   else if(!mr)
    default_videoip = "0";
   else
    default_videoip = "1";

   sysname = (const char *)MDFNSystems[i]->shortname;
  }

  default_scale = ceil(1024 / nominal_width);

  if(default_scale * nominal_width > 1024)
   default_scale--;

  if(!default_scale)
   default_scale = 1;

  trio_snprintf(default_value, 256, "%d", default_xres);
  AddSystemSetting(sysname, "xres", CSD_xres, CSDE_xres, MDFNST_UINT, strdup(default_value), "0", "65536", NULL, NULL, NULL, MDFNSF_FREE_DEFAULT);

  trio_snprintf(default_value, 256, "%d", default_yres);
  AddSystemSetting(sysname, "yres", CSD_yres, CSDE_yres, MDFNST_UINT, strdup(default_value), "0", "65536", NULL, NULL, NULL, MDFNSF_FREE_DEFAULT);

  trio_snprintf(default_value, 256, "%f", default_scale);
  AddSystemSetting(sysname, "xscale", CSD_xscale, NULL, MDFNST_FLOAT, strdup(default_value), "0.01", "256", NULL, NULL, NULL, MDFNSF_FREE_DEFAULT);
  AddSystemSetting(sysname, "yscale", CSD_yscale, NULL, MDFNST_FLOAT, strdup(default_value), "0.01", "256", NULL, NULL, NULL, MDFNSF_FREE_DEFAULT);

  trio_snprintf(default_value, 256, "%f", default_scalefs);
  AddSystemSetting(sysname, "xscalefs", CSD_xscalefs, CSDE_xyscalefs, MDFNST_FLOAT, strdup(default_value), "0.01", "256", NULL, NULL, NULL, MDFNSF_FREE_DEFAULT);
  AddSystemSetting(sysname, "yscalefs", CSD_yscalefs, CSDE_xyscalefs, MDFNST_FLOAT, strdup(default_value), "0.01", "256", NULL, NULL, NULL, MDFNSF_FREE_DEFAULT);

  AddSystemSetting(sysname, "scanlines", CSD_scanlines, CSDE_scanlines, MDFNST_INT, "0", "-100", "100");
  AddSystemSetting(sysname, "stretch", CSD_stretch, NULL, MDFNST_ENUM, "aspect_mult2", NULL, NULL, NULL, NULL, StretchMode_List);
  AddSystemSetting(sysname, "videoip", CSDvideo_settingsip, NULL, MDFNST_ENUM, default_videoip, NULL, NULL, NULL, NULL, VideoIP_List);
  AddSystemSetting(sysname, "special", CSD_special, CSDE_special, MDFNST_ENUM, "none", NULL, NULL, NULL, NULL, Special_List);
  AddSystemSetting(sysname, "shader", CSD_shader, CSDE_shader, MDFNST_ENUM, "none", NULL, NULL, NULL, NULL, Shader_List);
  AddSystemSetting(sysname, "shader.goat.hdiv", gettext_noop("Constant RGB horizontal divergence."), nullptr, MDFNST_FLOAT, "0.50", "-2.00", "2.00");
  AddSystemSetting(sysname, "shader.goat.vdiv", gettext_noop("Constant RGB vertical divergence."), nullptr, MDFNST_FLOAT, "0.50", "-2.00", "2.00");
  AddSystemSetting(sysname, "shader.goat.pat", gettext_noop("Mask pattern."), nullptr, MDFNST_ENUM, "goatron", NULL, NULL, NULL, NULL, GoatPat_List);
  AddSystemSetting(sysname, "shader.goat.tp", gettext_noop("Transparency of otherwise-opaque mask areas."), nullptr, MDFNST_FLOAT, "0.50", "0.00", "1.00");
  AddSystemSetting(sysname, "shader.goat.fprog", gettext_noop("Force interlaced video to be treated as progressive."), gettext_noop("When disabled, the default, the \"video.deinterlacer\" setting is effectively ignored with respect to what appears on the screen, unless it's set to \"blend\" or \"blend_rg\".  When enabled, it may be prudent to disable the scanlines effect controlled by the *.goat.slen setting, or else the scanline effect may look objectionable."), MDFNST_BOOL, "0");
  AddSystemSetting(sysname, "shader.goat.slen", gettext_noop("Enable scanlines effect."), nullptr, MDFNST_BOOL, "1");
 }

 MDFNI_MergeSettings(GlobalVideoSettings);
}

static struct
{
 int fullscreen;
 int fs_display;
 int xres, yres;
 double xscale, xscalefs;
 double yscale, yscalefs;
 int videoip;
 int stretch;
 int special;
 int scanlines;
 bool force_bbclear;
 ShaderType shader;
 ShaderParams shader_params;
 unsigned cursorvis;

 std::string special_str, shader_str, goat_pat_str;
} video_settings;

static unsigned vdriver;
static bool osd_alpha_blend;

static const struct ScalerDefinition
{
	int id;
	int xscale;
	int yscale;
} Scalers[] = 
{
	{ NTVB_HQ2X, 2, 2 },
	{ NTVB_HQ3X, 3, 3 },
	{ NTVB_HQ4X, 4, 4 },

	{ NTVB_SCALE2X, 2, 2 },
	{ NTVB_SCALE3X, 3, 3 },
	{ NTVB_SCALE4X, 4, 4 },

	{ NTVB_NN2X, 2, 2 },
        { NTVB_NN3X, 3, 3 },
        { NTVB_NN4X, 4, 4 },

	{ NTVB_NNY2X, 1, 2 },
	{ NTVB_NNY3X, 1, 3 },
	{ NTVB_NNY4X, 1, 4 },

	{ NTVB_2XSAI, 2, 2 },
	{ NTVB_SUPER2XSAI, 2, 2 },
	{ NTVB_SUPEREAGLE, 2, 2 },
};

static MDFNGI *VideoGI;

static const ScalerDefinition* CurrentScaler = NULL;

static int winpos_x, winpos_y;
static bool winpos_applied;
static SDL_Window* window = NULL;
static SDL_GLContext glcontext = NULL;
static SDL_Surface *screen = NULL;
static OpenGL_Blitter *ogl_blitter = NULL;
static SDL_Surface *IconSurface=NULL;

static int32 screen_w, screen_h;
static MDFN_Rect screen_dest_rect;

static MDFN_Surface *HelpSurface = NULL;
static MDFN_Rect HelpRect;

static MDFN_Surface *SMSurface = NULL;
static MDFN_Rect SMRect;
static MDFN_Rect SMDRect;

static double exs,eys;
static int evideoip;

static int NeedClear = 0;
static uint32 LastBBClearTime = 0;

static WMInputBehavior CurWMIB;

static int rotated;

static MDFN_PixelFormat game_pf; // Pixel format for game texture/surface
static MDFN_PixelFormat osd_pf;	 // Pixel format for OSD textures/surfaces
static MDFN_PixelFormat emu_pf;

// SLK SR
int sr_x_scale = 1;
int sr_y_scale = 1;

static INLINE void MarkNeedBBClear(void)
{
 NeedClear = 15;
}

static void SyncCleanup(void)
{
 if(SMSurface)
 {
  delete SMSurface;
  SMSurface = nullptr;
 }

 if(HelpSurface)
 {
  delete HelpSurface;
  HelpSurface = nullptr;
 }

 if(ogl_blitter)
 {
  delete ogl_blitter;
  ogl_blitter = nullptr;
 }

 if(glcontext)
 {
  SDL_GL_DeleteContext(glcontext);
  glcontext = nullptr;
 }

/*
*/
}

void Video_Kill(void)
{
 if (use_switchres) { //psakhis
  sr_deinit(); 
  printf("VIDEO - Video_Kill - SWITCHRES unloaded \n");	//psakhis
 }	
 
 SyncCleanup();

 if(window)
 {
  if(SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN)
   SDL_SetWindowFullscreen(window, 0);
  //
  SDL_SetRelativeMouseMode(SDL_FALSE);
  SDL_ShowCursor(SDL_TRUE);
  SDL_SetWindowGrab(window, SDL_FALSE);

  SDL_DestroyWindow(window);
  window = nullptr;  
 }
 //
 //
 //
 if(IconSurface)
 {
  //SDL_FreeSurface(IconSurface);
  //IconSurface = nullptr;
 }

 screen = nullptr;
 VideoGI = nullptr;
 screen_w = 0;
 screen_h = 0;
}

// May be called before Video_Init(), and after Video_Kill().
bool Video_ErrorPopup(bool warning, const char* title, const char* text)
{
 bool ret = false;

 #ifdef WIN32
 if(window)
 {
  // Best not to drive the user stark raving mad.
  SDL_SetRelativeMouseMode(SDL_FALSE);
  SDL_ShowCursor(SDL_TRUE);
  SDL_SetWindowGrab(window, SDL_FALSE);
  // should we or shouldn't we...: SDL_SetHint(SDL_HINT_WINDOWS_NO_CLOSE_ON_ALT_F4, "0");
 }
#if 0
 SDL_Window* pw = window;

 // Avoid comically-horrible behavior.
 if(SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN)
  pw = nullptr;

 ret = !SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, title, text, pw);
#endif
 ret = (bool)MessageBox(NULL, (TCHAR*)Win32Common::UTF8_to_T(text).c_str(), (TCHAR*)Win32Common::UTF8_to_T(title).c_str(), MB_OK | (warning ? MB_ICONWARNING : MB_ICONERROR) | MB_TASKMODAL | MB_SETFOREGROUND | MB_TOPMOST);

 if(window)
  Video_SetWMInputBehavior(CurWMIB);
 #endif

 return ret;
}

static void GenerateWindowedDestRect(void)
{
 exs = video_settings.xscale;
 eys = video_settings.yscale;

 screen_dest_rect.x = 0;
 screen_dest_rect.y = 0;
 screen_dest_rect.w = floor(0.5 + VideoGI->nominal_width * exs);
 screen_dest_rect.h = floor(0.5 + VideoGI->nominal_height * eys);

 if(rotated == MDFN_ROTATE90 || rotated == MDFN_ROTATE270)
  std::swap(screen_dest_rect.w, screen_dest_rect.h);
}

static bool GenerateFullscreenDestRect(void)
{
 if(video_settings.stretch)
 {
  int nom_width, nom_height;

  if(rotated)
  {
   nom_width = VideoGI->nominal_height;
   nom_height = VideoGI->nominal_width;
  }
  else
  {
   nom_width = VideoGI->nominal_width;
   nom_height = VideoGI->nominal_height;
  }

  if(video_settings.stretch == 2 || video_settings.stretch == 3 || video_settings.stretch == 4)	// Aspect-preserve stretch
  {
   exs = (double)screen_w / nom_width;
   eys = (double)screen_h / nom_height;

   if(video_settings.stretch == 3 || video_settings.stretch == 4)	// Round down to nearest int.
   {
    double floor_exs = floor(exs);
    double floor_eys = floor(eys);

    if(!floor_exs || !floor_eys)
    {
     MDFN_printf(_("WARNING: Resolution is too low for stretch mode selected.  Falling back to \"aspect\" mode.\n"));
    }
    else
    {
     exs = floor_exs;
     eys = floor_eys;

     if(video_settings.stretch == 4)	// Round down to nearest multiple of 2.
     {
      int even_exs = (int)exs & ~1;
      int even_eys = (int)eys & ~1;

      if(!even_exs || !even_eys)
      {
       MDFN_printf(_("WARNING: Resolution is too low for stretch mode selected.  Falling back to \"aspect_int\" mode.\n"));
      }
      else
      {
       exs = even_exs;
       eys = even_eys;
      }
     }
    }
   }

   // Check if we are constrained horizontally or vertically
   if (exs > eys)
   {
    // Too tall for screen, fill vertically
    exs = eys;
   }
   else
   {
    // Too wide for screen, fill horizontally
    eys = exs;
   }

   //printf("%f %f\n", exs, eys);

   screen_dest_rect.w = floor(0.5 + exs * nom_width);
   screen_dest_rect.h = floor(0.5 + eys * nom_height);

   // Centering:
   int nx = (int)((screen_w - screen_dest_rect.w) / 2);
   if(nx < 0) nx = 0;
   screen_dest_rect.x = nx;

   int ny = (int)((screen_h - screen_dest_rect.h) / 2);
   if(ny < 0) ny = 0;
   screen_dest_rect.y = ny;
  }
  else 	// Full-stretch
  {
   screen_dest_rect.x = 0;
   screen_dest_rect.w = screen_w;

   screen_dest_rect.y = 0;
   screen_dest_rect.h = screen_h;

   exs = (double)screen_w / nom_width;
   eys = (double)screen_h / nom_height;
  }
 }
 else
 {
  exs = video_settings.xscalefs;
  eys = video_settings.yscalefs;

  screen_dest_rect.w = floor(0.5 + VideoGI->nominal_width * exs);
  screen_dest_rect.h = floor(0.5 + VideoGI->nominal_height * eys);

  if(rotated == MDFN_ROTATE90 || rotated == MDFN_ROTATE270)
   std::swap(screen_dest_rect.w, screen_dest_rect.h);

  screen_dest_rect.x = (screen_w - screen_dest_rect.w) / 2;
  screen_dest_rect.y = (screen_h - screen_dest_rect.h) / 2;
 }

 // Quick and dirty kludge for VB's "hli" and "vli" 3D modes.
 screen_dest_rect.x &= ~1;
 screen_dest_rect.y &= ~1;

 return screen_dest_rect.w < 16384 && screen_dest_rect.h < 16384;
}

static bool weset_glstvb = false; 

void Video_SetWMInputBehavior(const WMInputBehavior& beeeeees)
{
 CurWMIB = beeeeees;
 //
 const bool fs = (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN);
 const bool grab = CurWMIB.Grab;
 const bool relm = CurWMIB.MouseRel && !CurWMIB.MouseAbs && !CurWMIB.Cursor && (grab || fs);
 const bool curse = !relm && (CurWMIB.Cursor || video_settings.cursorvis == CURSORVIS_VISIBLE);

 //printf("Grab: %d, RelM: %d, Curse: %d\n", grab, relm, curse);

 if(grab)
 {
  SDL_ShowWindow(window);
  SDL_RaiseWindow(window);
  SDL_SetWindowGrab(window, SDL_TRUE);
 }

 SDL_ShowCursor(SDL_FALSE);
 SDL_SetRelativeMouseMode(relm ? SDL_TRUE : SDL_FALSE);
 SDL_ShowCursor(curse ? SDL_TRUE : SDL_FALSE);

 if(!grab)
  SDL_SetWindowGrab(window, SDL_FALSE);

 SDL_SetHint(SDL_HINT_WINDOWS_NO_CLOSE_ON_ALT_F4, grab ? "1" : "0");
}

#if 0
struct ModeInfo
{
 int32 width = 0;
 int32 height = 0;
 MDFN_PixelFormat format(MDFN_COLORSPACE_RGB, 4, 0, 8, 16, 24);
 bool fullscreen = false;
 float refresh_rate = 0.0
 bool vsync = false;
};

static ModeInfo SetMode(const ModeInfo& mode)
{
 if(window && !(SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN))
  SDL_GetWindowPosition(window, &winpos_x, &winpos_y);

 if(window && !(SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN))
  SDL_GetWindowPosition(window, &winpos_x, &winpos_y);
 //
 SyncCleanup();
}

 if(new_mode == last_tried_mode && new_mode == cur_mode)
  SetMode(new_mode);
#endif

// SLK
#ifdef WIN32
static void Video_WinSetVideoMode(int iWidth, int iHeight)
{
  DEVMODE Mode;
  printf("EnumDisplaySettings return: %ld\n",EnumDisplaySettings(NULL, ENUM_CURRENT_SETTINGS, &Mode));

  //Mode.dmBitsPerPel = iBpp;
  printf("EnumDisplaySettings return refresh rate: %ld\n",Mode.dmDisplayFrequency);
  Mode.dmPelsWidth = iWidth;
  Mode.dmPelsHeight = iHeight;
  Mode.dmSize = sizeof(Mode);
  Mode.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL;

  Mode.dmDisplayFrequency = 59.941002;

  printf("ChangeDisplaySettings return: %ld\n",ChangeDisplaySettings(&Mode, CDS_FULLSCREEN));

};
#endif

//psakhis + SLK
int Video_SetSwitchresFS(int w,int h,double vfreq)
{
 printf("VIDEO - Video_SetSwitchresFS - called for %dx%d@%f\n",w,h,vfreq);
 sr_mode swres_result;
 unsigned char interlace = 0;	       
 if (h > 288) 
  interlace = 1;

 retSR = sr_add_mode(w, h, vfreq, interlace, &swres_result);
 printf("VIDEO - Video_SetSwitchresFS - result %dx%d@%f - x=%d y=%d\n", swres_result.width, swres_result.height,swres_result.refresh, swres_result.x_scale, swres_result.y_scale);
 
 sr_x_scale = swres_result.x_scale;
 sr_y_scale = swres_result.y_scale;
 video_settings.xres = swres_result.width;
 video_settings.yres = swres_result.height;
 video_settings.xscale = 1;
 video_settings.yscale = 1; 
  
 retSR = sr_switch_to_mode(swres_result.width, swres_result.height, swres_result.refresh, swres_result.interlace, &swres_result);
 printf("VIDEO - Video_SetSwitchresFS - sr_switch_to_mode return: %u\n", (unsigned int)retSR);         
 
 return swres_result.width;
}
// end psakhis

void Video_ChangeResolution(MDFNGI *gi, int w, int h, double vfreq)
{
  printf("VIDEO - Video_ChangeResolution - Requested video mode: %dx%d@%f\n", w, h, vfreq);  
  
  //psakhis
  if (use_super_resolution) {
      	w = 2560;
  }  
  if (use_super_resolution && resolution_to_change_h == current_game_resolution_h) { //no switch requeried
  	sr_x_scale = (int) (w / resolution_to_change_w);  	
  	sr_y_scale = (int) (h / resolution_to_change_h);  	
  	printf("VIDEO - Video_ChangeResolution - Super resolution change bypassed. Only apply scaling scale (%d,%d) \n",sr_x_scale,sr_y_scale);    	  	
  	return;	
  }  
  //end psakhis    
  MarkNeedBBClear();

  // SLK TODO - rebuild HelpSurface on resolution change ???

  // WINDOW 
  if(video_settings.fullscreen == 0)
  {
    printf("  VIDEO - Video_ChangeResolution - Video mode: WINDOWED\n");

    SDL_SetWindowSize(window, w, h);

    //VideoGI->nominal_width = w;

    int x, y;
    SDL_GetWindowPosition(window, &x, &y);
    SDL_PumpEvents();
    SDL_SetWindowPosition(window, x, y);

    sr_x_scale = 1;
    sr_y_scale = 1;
  }
  else 
  // FULLSCREEN
  {
    printf("  VIDEO - Video_ChangeResolution - Video mode: FULLSCREEN\n");
   //if(0)
    if (use_switchres) //psakhis new method
    {
    	w = Video_SetSwitchresFS(w,h,vfreq);   
    	if (w == current_game_resolution_w && resolution_to_change_h == current_game_resolution_h) { //no switch requeried
    	 	printf("VIDEO - Video_ChangeResolution - Switchres Super resolution change bypassed. Only apply scaling scale (%d,%d) \n",sr_x_scale,sr_y_scale);
    	 	return;    	  	
    	}  
    	current_game_resolution_w = w;
    	current_game_resolution_h = h;     	 	 
    	
    	if(SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN) < 0) 
         if(SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP) < 0)
          MDFN_Notify(MDFN_NOTICE_WARNING, _("Reverting switchres to windowed mode because SDL_SetWindowFullscreen() failed: %s"), SDL_GetError());       
         SDL_PumpEvents();         
    }
    else
    {
      // Use good old NATIVE or SUPER switch resolution process
      SDL_DisplayMode current;
      SDL_DisplayMode mode;
      SDL_DisplayMode trymode;
      int displayIndex;
             
      displayIndex = SDL_GetWindowDisplayIndex(window);
      if(displayIndex < 0)
      {
        printf("  VIDEO - Video_ChangeResolution - ERROR Could not get screen index: %s\n", SDL_GetError());
        return;
      }
      else
      {
        printf("  VIDEO - Video_ChangeResolution - Screen index: %d\n",displayIndex);
      }
    
      if(SDL_GetCurrentDisplayMode(displayIndex, &current) != 0)
      {
        printf("  VIDEO - Video_ChangeResolution - Could not get display mode for video display %d: %s\n", displayIndex, SDL_GetError());
      }
      else
      {
        printf("  VIDEO - Video_ChangeResolution - Display #%d: Current display mode is %dx%dpx @ %dhz.\n", displayIndex, current.w, current.h, current.refresh_rate);
        trymode.w = w;
        trymode.h = h;
        trymode.refresh_rate = (int) vfreq;

        if (SDL_GetClosestDisplayMode(displayIndex, &trymode, &mode) == NULL)
        {
          printf("  VIDEO - Video_ChangeResolution - No suitable display mode was found, %s\n",SDL_GetError());
        }
        else
        {   
          //psakhis     
          sr_x_scale = (int) (mode.w / resolution_to_change_w);
          sr_y_scale = (int) (mode.h / resolution_to_change_h);
          current_game_resolution_w = mode.w;
          current_game_resolution_h = h;          
          //end psakhis
          printf("  VIDEO - Video_ChangeResolution - Received: \t%dx%dpx @ %dhz ,scale (%d,%d)\n", mode.w, mode.h, mode.refresh_rate,sr_x_scale,sr_y_scale);                                        
          #ifdef WIN32
          // Use Win32 API for resolution chang
          Video_WinSetVideoMode(mode.w,mode.h);
          SDL_SetWindowSize(window, w, h);
          #else
          // Use SDL video change function - may be slower
          if(SDL_SetWindowDisplayMode(window, &mode) < 0)
          {
            printf("  VIDEO - Video_ChangeResolution - ERROR - SDL_SetWindowDisplayMode: '%s'\n", SDL_GetError());
          }
          #endif
        }
      }
    }
  }

  // OSD D Rect - vertical offest
  switch(h){
    case 240:SMDRect.y = h - SMDRect.h - 18;
             break;
    case 288:SMDRect.y = h - SMDRect.h - 32;
             break;
    case 480:SMDRect.y = h - SMDRect.h - 32;
             break;
    case 576:SMDRect.y = h - SMDRect.h - 64;
             break;
    default: SMDRect.y = h - SMDRect.h - 64;
  }
  SMDRect.w = w; // OSD rect w
    
  //if(SMSurface)
  //{
  //  MDFN_PixelFormat SMFormat = SMSurface->format;
  //  delete SMSurface;
  //  SMSurface = nullptr;
  //  SMSurface = new MDFN_Surface(NULL, SMRect.w, SMRect.h, SMRect.w, SMFormat);
  //}

  //printf("VIDEO - Video_ChangeResolution - Resize video output to %dx%d\n", w, h);
  //SDL_SetWindowSize(window, w, h);   
  
  //psakhis
  VideoGI->nominal_width = w;
  if (h * 2 == VideoGI->nominal_height || h * 0.5 == VideoGI->nominal_height) //only changes interlaced/progressive
    	  VideoGI->nominal_height = h;  
    	  
  screen_w = w;
  screen_h = h;
  if (!GenerateFullscreenDestRect()) {
    GenerateWindowedDestRect();
    screen_w = screen_dest_rect.w;  
    screen_h = screen_dest_rect.h; 
  }
  printf("VIDEO - Video_sync - screen dest: %dx%d - %d,%d\n", screen_dest_rect.w,screen_dest_rect.h,screen_dest_rect.x,screen_dest_rect.y);
    
  SDL_SetWindowSize(window, screen_dest_rect.w, screen_dest_rect.h);
  //end psakhis
    
  video_settings.xres = w;
  video_settings.yres = h;
  video_settings.xscalefs = 1;
  video_settings.yscalefs = 1;
  
  ogl_blitter->SetViewport(screen_w, screen_h);
  printf("VIDEO - Video_ChangeResolution completed\n");
}
// SLK - end

void Video_Sync(MDFNGI *gi)
{
 MDFNI_printf(_("Initializing video...\n"));
 MDFN_AutoIndent aindv(1);

 if(window && winpos_applied && !(SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN))
  SDL_GetWindowPosition(window, &winpos_x, &winpos_y);
 //
 SyncCleanup();
 //Time::SleepMS(1000);

 VideoGI = gi;
 rotated = gi->rotated;
 //
 #ifdef WIN32
 if(MDFN_GetSettingB("video.disable_composition"))
 {
  static bool dwm_already_try_disable = false;

  if(!dwm_already_try_disable)
  {
   HMODULE dwmdll;

   dwm_already_try_disable = true;

   if((dwmdll = LoadLibrary(TEXT("Dwmapi.dll"))) != NULL)
   {
    HRESULT WINAPI (*p_DwmEnableComposition)(UINT) = (HRESULT WINAPI (*)(UINT))GetProcAddress(dwmdll, "DwmEnableComposition");

    if(p_DwmEnableComposition != NULL)
    {
     p_DwmEnableComposition(0);
    }
   }
  }
 }
 #endif

 if(!getenv("__GL_SYNC_TO_VBLANK") || weset_glstvb)
 {
  if(MDFN_GetSettingB("video.glvsync"))
  {
   #if HAVE_PUTENV
   static char gl_pe_string[] = "__GL_SYNC_TO_VBLANK=1";
   putenv(gl_pe_string); 
   #elif HAVE_SETENV
   setenv("__GL_SYNC_TO_VBLANK", "1", 1);
   #endif
  }
  else
  {
   #if HAVE_PUTENV
   static char gl_pe_string[] = "__GL_SYNC_TO_VBLANK=0";
   putenv(gl_pe_string); 
   #elif HAVE_SETENV
   setenv("__GL_SYNC_TO_VBLANK", "0", 1);
   #endif
  }
  weset_glstvb = true;
 }
 //
 osd_alpha_blend = MDFN_GetSettingB("osd.alpha_blend");

 std::string snp = std::string(gi->shortname) + ".";

 if(gi->GameType == GMT_PLAYER)
  snp = "player.";

 video_settings.xres = MDFN_GetSettingUI(snp + "xres");
 video_settings.yres = MDFN_GetSettingUI(snp + "yres");
 video_settings.xscale = MDFN_GetSettingF(snp + "xscale");
 video_settings.yscale = MDFN_GetSettingF(snp + "yscale");
 video_settings.xscalefs = MDFN_GetSettingF(snp + "xscalefs");
 video_settings.yscalefs = MDFN_GetSettingF(snp + "yscalefs");
 video_settings.videoip = MDFN_GetSettingI(snp + "videoip");
 video_settings.stretch = MDFN_GetSettingUI(snp + "stretch");
 video_settings.scanlines = MDFN_GetSettingI(snp + "scanlines");

 video_settings.special = MDFN_GetSettingUI(snp + "special");
 video_settings.special_str = MDFN_GetSettingS(snp + "special");

 video_settings.shader_params.goat_hdiv = MDFN_GetSettingF(snp + "shader.goat.hdiv");
 video_settings.shader_params.goat_vdiv = MDFN_GetSettingF(snp + "shader.goat.vdiv");
 video_settings.shader_params.goat_pat = MDFN_GetSettingI(snp + "shader.goat.pat");
 video_settings.goat_pat_str = MDFN_GetSettingS(snp + "shader.goat.pat");
 video_settings.shader_params.goat_tp = MDFN_GetSettingF(snp + "shader.goat.tp");
 video_settings.shader_params.goat_slen = MDFN_GetSettingB(snp + "shader.goat.slen");
 video_settings.shader_params.goat_fprog = MDFN_GetSettingB(snp + "shader.goat.fprog");
 //

 video_settings.fullscreen = MDFN_GetSettingB("video.fs");
 video_settings.fs_display = MDFN_GetSettingI("video.fs.display");
 vdriver = MDFN_GetSettingI("video.driver");
 if(vdriver == VDRIVER__COUNT) // "default"
  vdriver = VDRIVER_OPENGL;

 video_settings.force_bbclear = MDFN_GetSettingB("video.force_bbclear");
 video_settings.cursorvis = MDFN_GetSettingUI("video.cursorvis");

 video_settings.shader = (ShaderType)MDFN_GetSettingI(snp + "shader");
 video_settings.shader_str = MDFN_GetSettingS(snp + "shader");
 
 // SLK - set video settings - disable some options in conflict with native resolution
 _resolution_switch = MDFN_GetSettingI("video.resolution_switch");
 
 if(_resolution_switch){
  // Enable dynamic output resolution switch
  printf("VIDEO - Video_Sync - Output video resolution swithing: ON\n");
  if(_resolution_switch == RES_NATIVE) // Native Res
  {
   use_native_resolution = true;
   printf("VIDEO - Video_Sync - apply NATIVE resolution settings - set to %dx%d\n",resolution_to_change_w,resolution_to_change_h);
   video_settings.xres = resolution_to_change_w;  // for fullscreen mode
   VideoGI->nominal_width = resolution_to_change_w;  // for windowed mode
  }
  if(_resolution_switch == RES_SUPER)
  {
   use_super_resolution = true;
   printf("VIDEO - Video_Sync - apply SUPER resolution settings - set to 2560x%d\n",resolution_to_change_h);
   video_settings.xres = current_game_resolution_w = 2560;  // for fullscreen mode   
   VideoGI->nominal_width = 2560;  // SLK TODO: nothing else?
  }
  if(_resolution_switch == RES_SWITCHRES) // psakhis
  {
   use_switchres = true;
   sr_init();
   retSR=sr_init_disp("auto",NULL);
   printf("VIDEO - Video_Sync - SWITCHRES loaded - %u\n",(unsigned int)retSR);
   printf("VIDEO - Video_Sync - apply SWITCHRES resolution settings - set to %dx%d@%f\n",resolution_to_change_w,resolution_to_change_h,resolution_to_change_vfreq);     
   video_settings.xres = current_game_resolution_w = Video_SetSwitchresFS(resolution_to_change_w,resolution_to_change_h,resolution_to_change_vfreq); // for fullscreen mode
   VideoGI->nominal_width = current_game_resolution_w;  // for windowed mode      
  }
  video_settings.yres = current_game_resolution_h = resolution_to_change_h;  // for fullscreen mode    
  //VideoGI->nominal_height = resolution_to_change_h;  // for windowed mode -> psakhis:this destroys 224 rectangles on 240p  
  //printf("VideoGI->nominal_height %d vfreq %f \n",VideoGI->nominal_height ,resolution_to_change_vfreq);
  //psakhis
  if (resolution_to_change_h * 2 == VideoGI->nominal_height || resolution_to_change_h * 0.5 == VideoGI->nominal_height) //only changes interlaced/progressive
    VideoGI->nominal_height = resolution_to_change_h;  
  
  video_settings.xscale = 1;
  video_settings.yscale = 1;
  video_settings.xscalefs = 1;
  video_settings.yscalefs = 1;
  video_settings.videoip = 0;
  video_settings.shader = SHADER_NONE;
  video_settings.stretch = 0;  
 }
 //
 //
 if(0)
 {
  // Gotta keep the compiler on its toes(the floor is SPAGHETTI)!
  TryWindowed:;
  MDFNI_SetSettingB("video.fs", false);
  video_settings.fullscreen = 0;
  if(window)
  {
   if(SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN)
    SDL_SetWindowFullscreen(window, 0);
  }
 }

 GenerateWindowedDestRect();
 screen_w = screen_dest_rect.w;  
 screen_h = screen_dest_rect.h;
 //
 //

 if(screen_dest_rect.w > 16383 || screen_dest_rect.h > 16383)
  throw MDFN_Error(0, _("Window size(%dx%d) is too large!"), screen_dest_rect.w, screen_dest_rect.h);

 SDL_SetWindowFullscreen(window, 0);
 SDL_PumpEvents();
 SDL_SetWindowSize(window, screen_dest_rect.w, screen_dest_rect.h);
 SDL_PumpEvents();
 SDL_SetWindowPosition(window, winpos_x, winpos_y);
 winpos_applied = true;
 SDL_PumpEvents();
 SDL_SetWindowTitle(window, (gi && gi->name.size()) ? gi->name.c_str() : "Mednafen");
 SDL_PumpEvents();
 SDL_ShowWindow(window);
 SDL_PumpEvents();

 //
 if(0)
 {
  TryNonGL:;

  if(ogl_blitter)
  {
   delete ogl_blitter;
   ogl_blitter = nullptr;
  }

  if(glcontext)
  {
   SDL_GL_DeleteContext(glcontext);
   glcontext = nullptr;
  }
  vdriver = VDRIVER_SOFTSDL;
 }
 //
 if(vdriver == VDRIVER_OPENGL)
 {
  if(!(SDL_GetWindowFlags(window) & SDL_WINDOW_OPENGL))
  {
   MDFN_Notify(MDFN_NOTICE_WARNING, _("Reverting to soft SDL driver because window is not OpenGL-capable."));
   goto TryNonGL;
  }
  else
  {
   SDL_GL_ResetAttributes();
   SDL_GL_SetAttribute(SDL_GL_ACCELERATED_VISUAL, 1);
   SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
   //SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 1);
   //SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 4);
   SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

   if(!(glcontext = SDL_GL_CreateContext(window)))
   {
    MDFN_Notify(MDFN_NOTICE_WARNING, _("Reverting to soft SDL driver because of error creating OpenGL context: %s"), SDL_GetError());
    goto TryNonGL;
   }
   else
   {
    SDL_GL_SetSwapInterval(MDFN_GetSettingB("video.glvsync"));
   }
  }
 }

#if 1
 // Kludge, TODO: figure out where the bug exists in SDL or wherever...
 {
  int x, y;
  SDL_GetWindowPosition(window, &x, &y);
  SDL_PumpEvents();
  SDL_SetWindowPosition(window, x, y);
 }
#endif

 //
 //
 //
 CurrentScaler = nullptr;
 for(auto& scaler : Scalers)
  if(video_settings.special == scaler.id)
   CurrentScaler = &scaler;
 assert(video_settings.special == NTVB_NONE || CurrentScaler);

 evideoip = video_settings.videoip;

 //
 //
 //
 if(video_settings.fullscreen)
 {
  const unsigned xres = video_settings.xres;
  const unsigned yres = video_settings.yres;
  SDL_DisplayMode trymode;
  SDL_DisplayMode mode;
  int dindex;

  if(video_settings.fs_display >= 0)
   dindex = video_settings.fs_display;
  else
  {
#if 0
  if((dindex = SDL_GetWindowDisplayIndex(window)) < 0)
  {
   MDFN_Notify(MDFN_NOTICE_WARNING, _("Reverting to windowed mode because SDL_GetWindowDisplayIndex() failed: %s"), SDL_GetError());
   goto TryWindowed;
  }
#endif
   int num_displays;
   int wx, wy;
   int ww, wh;
   int wcx, wcy;

   dindex = 0;

   SDL_GetWindowPosition(window, &wx, &wy);
   SDL_GetWindowSize(window, &ww, &wh);

   wcx = wx + ww / 2;
   wcy = wy + wh / 2;

   if((num_displays = SDL_GetNumVideoDisplays()) < 0)
   {
    MDFN_Notify(MDFN_NOTICE_WARNING, _("Reverting to windowed mode because SDL_GetNumVideoDisplays() failed: %s"), SDL_GetError());
    goto TryWindowed;
   }

   //
   // Fullscreen with mirrored displays is kind of glitchy(maybe partly SDL2's fault?), but try to force usage of the one with the largest pixel count.
   //
   struct DIndexModeBounds
   {
    int dindex;
    SDL_DisplayMode mode;
    SDL_Rect bounds;
   };
   std::vector<DIndexModeBounds> dimbs;

   for(int d = 0; d < num_displays; d++)
   {
    DIndexModeBounds z;

    z.dindex = d;
    if(SDL_GetCurrentDisplayMode(d, &z.mode) < 0)
    {
     MDFN_Notify(MDFN_NOTICE_WARNING, _("Reverting to windowed mode because SDL_GetCurrentDisplayMode() failed: %s"), SDL_GetError());
     goto TryWindowed;
    }

    if(SDL_GetDisplayBounds(d, &z.bounds) < 0)
    {
     MDFN_Notify(MDFN_NOTICE_WARNING, _("Reverting to windowed mode because SDL_DisplayBounds() failed: %s"), SDL_GetError());
     goto TryWindowed;     
    }

    bool add_at_end = true;
    // Won't perform well if someone has thousands of displays ;)
    for(DIndexModeBounds& dimbs_e : dimbs)
    {
     if(z.bounds.x == dimbs_e.bounds.x && z.bounds.y == dimbs_e.bounds.y)
     {
      if((z.mode.w * z.mode.h) > (dimbs_e.mode.w * dimbs_e.mode.h))
       dimbs_e = z;

      add_at_end = false;
      break;
     }
    }

    if(add_at_end)
     dimbs.push_back(z);
   }

   int64 last_distance = INT64_MAX;
   for(const DIndexModeBounds& e : dimbs)
   {
    const int bcx = e.bounds.x + (e.bounds.w / 2);
    const int bcy = e.bounds.y + (e.bounds.h / 2);
    const int dx = wcx - bcx;
    const int dy = wcy - bcy;
    const int64 distance = (int64)dx * dx + (int64)dy * dy;

    if(distance < last_distance)
    {
     dindex = e.dindex;
     last_distance = distance;
    }
   }
  }

  //
  //
  {
   SDL_Rect dbr;

   if(SDL_GetDisplayBounds(dindex, &dbr) < 0)
   {
    MDFN_Notify(MDFN_NOTICE_WARNING, _("Reverting to windowed mode because SDL_GetDisplayBounds() failed: %s"), SDL_GetError());
    goto TryWindowed;
   }

   SDL_SetWindowPosition(window, dbr.x, dbr.y);
   SDL_SetWindowSize(window, dbr.w, dbr.h);
  }
  //
  //

  if(SDL_GetCurrentDisplayMode(dindex, &trymode) < 0)
  {
   MDFN_Notify(MDFN_NOTICE_WARNING, _("Reverting to windowed mode because SDL_GetCurrentDisplayMode() failed: %s"), SDL_GetError());
   goto TryWindowed;
  }

  if(xres > 0)
   trymode.w = xres;

  if(yres > 0)
   trymode.h = yres;
  
  if (resolution_to_change_vfreq > 0) //psakhis
   trymode.refresh_rate = resolution_to_change_vfreq;
   
  if(!SDL_GetClosestDisplayMode(dindex, &trymode, &mode))
  {
   MDFN_Notify(MDFN_NOTICE_WARNING, _("Reverting to windowed mode because no modes big enough for %dx%d."), trymode.w, trymode.h);
   goto TryWindowed;
  }
  
  //psakhis
  sr_x_scale = (int) (mode.w / resolution_to_change_w);
  sr_y_scale = (int) (mode.h / resolution_to_change_h);
  //end psakhis
     
  if(SDL_SetWindowDisplayMode(window, &mode) < 0)
  {
   MDFN_Notify(MDFN_NOTICE_WARNING, _("Reverting to windowed mode because SDL_SetWindowDisplayMode() failed: %s"), SDL_GetError());
   goto TryWindowed;
  }

#if 0
  int old_mousex = 0;
  int old_mousey = 0;
  SDL_GetGlobalMouseState(&old_mousex, &old_mousey);
#endif

  if(SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN) < 0)
  {
   MDFN_Notify(MDFN_NOTICE_WARNING, _("Reverting to windowed mode because SDL_SetWindowFullscreen() failed: %s"), SDL_GetError());
   goto TryWindowed;
  }

#if 0
  // ugggh
  SDL_PumpEvents();
  SDL_Delay(20);
  SDL_PumpEvents();
  SDL_WarpMouseGlobal(old_mousex, old_mousey);
#endif
  
  screen_w = mode.w;
  screen_h = mode.h;
  if(!GenerateFullscreenDestRect())
  {
   MDFN_Notify(MDFN_NOTICE_WARNING, _("Reverting to windowed mode because screen destination rectangle(%dx%d) is too large!"), screen_dest_rect.w, screen_dest_rect.h);
   goto TryWindowed;
  }
  printf("VIDEO - Video_sync - screen dest: %dx%d - %d,%d - scale(%d,%d)\n", screen_dest_rect.w,screen_dest_rect.h,screen_dest_rect.x,screen_dest_rect.y,sr_x_scale,sr_y_scale); // SLK
 }
 //
 //
 MDFN_printf(_("Driver: %s\n"), (vdriver == VDRIVER_OPENGL) ? _("OpenGL") : _("Software SDL") );
 //
 //
 MDFN_PixelFormat pf;

 screen = nullptr;

 {
  SDL_DisplayMode mode;
  int dindex;

  if((dindex = SDL_GetWindowDisplayIndex(window)) < 0)
   throw MDFN_Error(0, "SDL_GetWindowDisplayIndex() failed: %s", SDL_GetError());

  if(SDL_GetCurrentDisplayMode(dindex, &mode) < 0)
   throw MDFN_Error(0, "SDL_GetCurrentDisplayMode() failed: %s", SDL_GetError());

  if(mode.refresh_rate)
   MDFN_printf(_("Display Mode: %u x %u x %u bpp @ %dHz  (Window: %u x %u)\n"), mode.w, mode.h, SDL_BITSPERPIXEL(mode.format), mode.refresh_rate, screen_w, screen_h);
  else
   MDFN_printf(_("Display Mode: %u x %u x %u bpp  (Window: %u x %u)\n"), mode.w, mode.h, SDL_BITSPERPIXEL(mode.format), screen_w, screen_h);

  if(vdriver != VDRIVER_OPENGL)
  {
   if(!(screen = SDL_GetWindowSurface(window)))
    throw MDFN_Error(0, _("SDL_GetWindowSurface() failed: %s"), SDL_GetError());

   if(screen->format->BitsPerPixel != 32)
    throw MDFN_Error(0, _("Window surface bit depth(%ubpp) is not supported by Mednafen."), screen->format->BitsPerPixel);

   int rs = screen->format->Rshift;
   int gs = screen->format->Gshift;
   int bs = screen->format->Bshift;

   int as = 0;
   while(as == rs || as == gs || as == bs) // Find unused 8-bits to use as our alpha channel
    as += 8;

   osd_pf = game_pf = emu_pf = MDFN_PixelFormat(MDFN_COLORSPACE_RGB, 4, rs, gs, bs, as, 8, 8, 8, 8);
  }
 }

 if(vdriver == VDRIVER_OPENGL)
 {
  MDFN_AutoIndent aindps;
  char sp[128] = { 0 };

  if(video_settings.shader == SHADER_GOAT)
  {
   trio_snprintf(sp, sizeof(sp), " (pat=%s, hdiv=%f, vdiv=%f, tp=%f, slen=%d, fprog=%d)",
	video_settings.goat_pat_str.c_str(), video_settings.shader_params.goat_hdiv, video_settings.shader_params.goat_vdiv, video_settings.shader_params.goat_tp, video_settings.shader_params.goat_slen, video_settings.shader_params.goat_fprog);
  }

  MDFN_printf(_("Shader: %s%s\n"), video_settings.shader_str.c_str(), sp);
 }

 MDFN_printf(_("Fullscreen: %s\n"), video_settings.fullscreen ? _("Yes") : _("No"));
 MDFN_printf(_("Special Scaler: %s\n"), (video_settings.special == NTVB_NONE) ? _("None") : video_settings.special_str.c_str());

 if(!video_settings.scanlines)
  MDFN_printf(_("Scanlines: Off\n"));
 else
  MDFN_printf(_("Scanlines: %d%% opacity%s\n"), abs(video_settings.scanlines), (video_settings.scanlines < 0) ? _(" (with interlace field obscure)") : "");

 MDFN_printf(_("Destination Rectangle: X=%d, Y=%d, W=%d, H=%d\n"), screen_dest_rect.x, screen_dest_rect.y, screen_dest_rect.w, screen_dest_rect.h);
 if(screen_dest_rect.x < 0 || screen_dest_rect.y < 0 || (screen_dest_rect.x + screen_dest_rect.w) > screen_w || (screen_dest_rect.y + screen_dest_rect.h) > screen_h)
 {
  MDFN_AutoIndent ainddr;
  MDFN_printf(_("Warning:  Destination rectangle exceeds screen dimensions.  This is ok if you really do want the clipping...\n"));
 }

 if(vdriver == VDRIVER_OPENGL)
 {
  try
  {
   uint32 preferred_format = MDFN_GetSettingUI("video.glformat") & gi->ExtraVideoFormatSupport;

   if(CurrentScaler && (CurrentScaler->id == NTVB_HQ2X || CurrentScaler->id == NTVB_HQ3X || CurrentScaler->id == NTVB_HQ4X))
    preferred_format = EVFSUPPORT_NONE;

   ogl_blitter = new OpenGL_Blitter(video_settings.scanlines, video_settings.shader, video_settings.shader_params, &game_pf, &osd_pf, preferred_format);
   ogl_blitter->SetViewport(screen_w, screen_h);

   emu_pf = game_pf;
  }
  catch(std::exception& e)
  {
   MDFN_Notify(MDFN_NOTICE_WARNING, _("Reverting to soft SDL driver because of error initializing OpenGL blitter: %s"), e.what());
   goto TryNonGL;
  }
 }

 /* HQXX only supports this pixel format, sadly, and other pixel formats
    can't be easily supported without rewriting the filters.
    We do conversion to the real screen format in the blitting function. 
 */
#ifdef WANT_FANCY_SCALERS
 if(CurrentScaler)
 {
  if(CurrentScaler->id == NTVB_HQ2X || CurrentScaler->id == NTVB_HQ3X || CurrentScaler->id == NTVB_HQ4X)
   emu_pf = MDFN_PixelFormat::ARGB32_8888;
  else if(CurrentScaler->id == NTVB_2XSAI || CurrentScaler->id == NTVB_SUPER2XSAI || CurrentScaler->id == NTVB_SUPEREAGLE)
   SAI_SetFormat(emu_pf.opp * 8, emu_pf.Gprec == 5);
 }
#endif

 {
  int xmu = std::max<int>(1, screen_w / 402);
  int ymu = std::max<int>(1, screen_h / 288);

  SMRect.h = 18 + 2;
  SMRect.x = 0;
  SMRect.y = 0;
  SMRect.w = screen_w;

  // SLK - Set OSD message rendering rect.
  if(use_super_resolution || use_switchres) // enlarge OSD message in super resolution mode
  {
    SMDRect.w = SMRect.w * xmu;
    printf("VIDEO - Init : SMRect.w: %d xmu: %d\n",SMRect.w,xmu);
  }
  else
  {
    SMDRect.w = SMRect.w * xmu;
  }
  // SLK - end
  
  //SMDRect.w = SMRect.w * xmu;
  SMDRect.h = SMRect.h * ymu;
  SMDRect.x = (screen_w - SMDRect.w) / 2;
  // SMDRect.y = screen_h - SMDRect.h;

  // SLK - Vertical offset for OSD messages
  if(use_native_resolution || use_super_resolution || use_switchres)
  {
   switch(screen_h / sr_y_scale){
    case 240:SMDRect.y = screen_h - SMDRect.h - 18;
		 break;
    case 288:SMDRect.y = screen_h - SMDRect.h - 32;
		 break;
    case 480:SMDRect.y = screen_h - SMDRect.h - 32;
		 break;
    case 576:SMDRect.y = screen_h - SMDRect.h - 64;
		 break;
    default:SMDRect.y = screen_h - SMDRect.h * xmu; // TODO: ???
   }
  }
  else
  {
   SMDRect.y = screen_h - SMDRect.h;
  }
  // SLK - end  

  if(SMDRect.x < 0)
  {
   SMRect.w += SMDRect.x * 2 / xmu;
   SMDRect.w = SMRect.w * xmu;
   SMDRect.x = 0;
  }
  SMSurface = new MDFN_Surface(NULL, SMRect.w, SMRect.h, SMRect.w, osd_pf);
 }

 if(vdriver == VDRIVER_OPENGL)
 {
  for(int i = 0; i < 2; i++)
  {
   ogl_blitter->ClearBackBuffer();
   SDL_GL_SwapWindow(window);
   //ogl_blitter->HardSync();
  }
  ogl_blitter->ClearBackBuffer();
 }
 else
 {
  SDL_FillRect(screen, NULL, 0);
  for(int i = 0; i < 2; i++)
   SDL_UpdateWindowSurface(window);
 }

 MarkNeedBBClear();
 //
 //
 //
 Video_SetWMInputBehavior(CurWMIB);
}

void Video_Init(void)
{
 winpos_x = SDL_WINDOWPOS_CENTERED; //MDFN_GetSettingI("video.window.x");
 winpos_y = SDL_WINDOWPOS_CENTERED; //MDFN_GetSettingI("video.window.y");
 winpos_applied = false;

 CurWMIB.Cursor = true;
 CurWMIB.MouseAbs = true;
 CurWMIB.MouseRel = false;
 CurWMIB.Grab = false;
 //
 for(unsigned i = 0; i < 2 && !window; i++)
 {
  static const uint32 try_flags[2] =
  {
   SDL_WINDOW_HIDDEN | SDL_WINDOW_OPENGL,
   SDL_WINDOW_HIDDEN
  };
  window = SDL_CreateWindow("Mednafen", winpos_x, winpos_y, 64, 64, try_flags[i]);
 }

 if(!window)
  throw MDFN_Error(0, _("SDL_CreateWindow() failed: %s\n"), SDL_GetError());
 //
#ifdef WIN32
 {
  SDL_SysWMinfo wminf;

  //printf("Version: %08x\n", GetVersion());

  SDL_VERSION(&wminf.version);

  if(SDL_GetWindowWMInfo(window, &wminf))
  {
   HWND hwnd = wminf.info.win.window;
   HICON small_icon;
   HICON large_icon;

   if(GetVersion() & 0x80000000)
   {
    int small_w = GetSystemMetrics(SM_CXSMICON);
    int small_h = GetSystemMetrics(SM_CYSMICON);
    int large_w = GetSystemMetrics(SM_CXICON);
    int large_h = GetSystemMetrics(SM_CYICON);

    if(!small_w || !small_h)
     small_w = small_h = 32;

    if(!large_w || !large_h)
     large_w = large_h = 48;

    small_icon = (HICON)LoadImage(GetModuleHandle(NULL), TEXT("MEDNAFEN_ICON"), IMAGE_ICON, small_w, small_h, 0);
    large_icon = (HICON)LoadImage(GetModuleHandle(NULL), TEXT("MEDNAFEN_ICON"), IMAGE_ICON, large_w, large_h, 0);
   }
   else
    small_icon = large_icon = (HICON)LoadImage(GetModuleHandle(NULL), TEXT("MEDNAFEN_ICON"), IMAGE_ICON, 128, 128, 0);

   SendMessage(hwnd, WM_SETICON, ICON_BIG,   (LPARAM)large_icon);
   SendMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)small_icon);
  }
 }
#else
 IconSurface = SDL_CreateRGBSurfaceFrom((void*)icon_128x128, 128, 128, 32, 128 * 4, 0xFF, 0xFF00, 0xFF0000, 0xFF000000);
 SDL_SetWindowIcon(window, IconSurface);
#endif
}

static uint32 howlong = 0;
static char* CurrentMessage = NULL;
static MDFN_NoticeType CurrentMessageType;

void Video_ShowNotice(MDFN_NoticeType t, char* s)
{
 howlong = Time::MonoMS() + MDFN_GetSettingUI("osd.message_display_time");

 if(CurrentMessage)
 {
  free(CurrentMessage);
  CurrentMessage = NULL;
 }

 CurrentMessage = s;
 CurrentMessageType = t;
}

void BlitOSD(MDFN_Surface *src, const MDFN_Rect *src_rect, const MDFN_Rect *dest_rect, int source_alpha)
{
 if(ogl_blitter)
  ogl_blitter->BlitOSD(src, src_rect, dest_rect, (source_alpha != 0) && osd_alpha_blend);
 else
 {
  SDL_to_MDFN_Surface_Wrapper m_surface(screen);

  //MDFN_SrcAlphaBlitSurface(src, src_rect, &m_surface, dest_rect);
  MDFN_StretchBlitSurface(src, *src_rect, &m_surface, *dest_rect, (source_alpha > 0) && osd_alpha_blend);
 }

 bool cond1 = (dest_rect->x < screen_dest_rect.x || (dest_rect->x + dest_rect->w) > (screen_dest_rect.x + screen_dest_rect.w));
 bool cond2 = (dest_rect->y < screen_dest_rect.y || (dest_rect->y + dest_rect->h) > (screen_dest_rect.y + screen_dest_rect.h));

 if(cond1 || cond2)
  MarkNeedBBClear();
}

static bool BlitInternalMessage(const uint32 curtime)
{
 if(curtime >= howlong)
 {
  if(CurrentMessage)
  {
   free(CurrentMessage);
   CurrentMessage = NULL;
  }
  return false;
 }

 if(CurrentMessage)
 {
  uint32 shad_color = SMSurface->MakeColor(0x00, 0x00, 0x00, 0xFF);
  uint32 text_color;
  uint8 fill_alpha = 0xC0;

  switch(CurrentMessageType)
  {
   default:
   case MDFN_NOTICE_STATUS:
	text_color = SMSurface->MakeColor(0xFF, 0xFF, 0xFF, 0xFF);
	break;

   case MDFN_NOTICE_WARNING:
	text_color = SMSurface->MakeColor(0xFF, 0xFF, 0x00, 0xFF);
	break;

   case MDFN_NOTICE_ERROR:
	text_color = SMSurface->MakeColor(0xFF, 0x00, 0x00, 0xFF);
	fill_alpha = 0xFF;
	break;
  }

  SMSurface->Fill(0x00, 0x00, 0x00, fill_alpha);

  DrawTextShadow(SMSurface, 0, 1, CurrentMessage, text_color, shad_color, MDFN_FONT_9x18_18x18, SMRect.w);
  free(CurrentMessage);
  CurrentMessage = NULL;
 }

 BlitOSD(SMSurface, &SMRect, &SMDRect);

 return true;
}

#ifdef WANT_FANCY_SCALERS
template<typename T>
static void BlitSaI(const MDFN_Surface* src, const MDFN_Rect& src_rect, MDFN_Surface* dest)
{
 MDFN_Surface saisrc(NULL, src_rect.w + 4, src_rect.h + 4, src_rect.w + 4, src->format);
 const T* source_pixies = src->pix<T>() + src_rect.x + src_rect.y * src->pitchinpix;

 for(int y = 0; y < 2; y++)
 {
  memcpy(saisrc.pix<T>() + (y * saisrc.pitchinpix) + 2, source_pixies, src_rect.w * sizeof(T));
  memcpy(saisrc.pix<T>() + ((2 + y + src_rect.h) * saisrc.pitchinpix) + 2, source_pixies + (src_rect.h - 1) * src->pitchinpix, src_rect.w * sizeof(T));
 }

 for(int y = 0; y < src_rect.h; y++)
 {
  memcpy(saisrc.pix<T>() + ((2 + y) * saisrc.pitchinpix) + 2, source_pixies + y * src->pitchinpix, src_rect.w * sizeof(T));
  memcpy(saisrc.pix<T>() + ((2 + y) * saisrc.pitchinpix) + (2 + src_rect.w),
	 saisrc.pix<T>() + ((2 + y) * saisrc.pitchinpix) + (2 + src_rect.w - 1), sizeof(T));
 }
 //
 //
 uint8* spix = (uint8 *)(saisrc.pix<T>() + 2 * saisrc.pitchinpix + 2);
 uint32 spitch = saisrc.pitchinpix * sizeof(T);
 uint8* dpix = (uint8*)dest->pix<T>();
 uint32 dpitch = dest->pitchinpix * sizeof(T);

 if(CurrentScaler->id == NTVB_2XSAI)
 {
  if(sizeof(T) == 2)
   SAI_2xSaI(spix, spitch, dpix, dpitch, src_rect.w, src_rect.h);
  else
   SAI_2xSaI32(spix, spitch, dpix, dpitch, src_rect.w, src_rect.h);
 }
 else if(CurrentScaler->id == NTVB_SUPER2XSAI)
 {
  if(sizeof(T) == 2)
   SAI_Super2xSaI(spix, spitch, dpix, dpitch, src_rect.w, src_rect.h);
  else
   SAI_Super2xSaI32(spix, spitch, dpix, dpitch, src_rect.w, src_rect.h);
 }
 else if(CurrentScaler->id == NTVB_SUPEREAGLE)
 {
  if(sizeof(T) == 2)
   SAI_SuperEagle(spix, spitch, dpix, dpitch, src_rect.w, src_rect.h);
  else
   SAI_SuperEagle32(spix, spitch, dpix, dpitch, src_rect.w, src_rect.h);
 }
}
#endif

static void SubBlit(const MDFN_Surface *source_surface, const MDFN_Rect &src_rect, const MDFN_Rect &dest_rect, const int InterlaceField)
{
 const MDFN_Surface *eff_source_surface = source_surface;
 MDFN_Rect eff_src_rect = src_rect;

 if(!(src_rect.w > 0 && src_rect.w <= 32767) || !(src_rect.h > 0 && src_rect.h <= 32767))
 {
  //fprintf(stderr, "BUG: Source rect out of range; w=%d, h=%d\n", src_rect.w, src_rect.h);
  return;
 }
//#if 0
// assert(src_rect.w > 0 && src_rect.w <= 32767);
// assert(src_rect.h > 0 && src_rect.h <= 32767);
//#endif

 assert(dest_rect.w > 0);
 assert(dest_rect.h > 0);

   if(CurrentScaler)
   {
    MDFN_Rect boohoo_rect({0, 0, eff_src_rect.w * CurrentScaler->xscale, eff_src_rect.h * CurrentScaler->yscale});
    MDFN_Surface bah_surface(NULL, boohoo_rect.w, boohoo_rect.h, boohoo_rect.w, eff_source_surface->format, false);
    const uint32 bypp = eff_source_surface->format.opp;
    uint8* screen_pixies = (bypp == 4) ? (uint8 *)bah_surface.pixels : (uint8*)bah_surface.pixels16;
    uint32 screen_pitch = bah_surface.pitchinpix * bypp;

    if(CurrentScaler->id == NTVB_SCALE4X || CurrentScaler->id == NTVB_SCALE3X || CurrentScaler->id == NTVB_SCALE2X)
    {
#ifdef WANT_FANCY_SCALERS
     //
     // scale2x and scale3x apparently can't handle source heights less than 2.
     // scale4x, it's less than 4
     //
     // None can handle source widths less than 2.
     //
     if(eff_src_rect.w < 2 || eff_src_rect.h < 2 || (CurrentScaler->id == NTVB_SCALE4X && eff_src_rect.h < 4))
     {
      nnx(CurrentScaler->id - NTVB_SCALE2X + 2, eff_source_surface, eff_src_rect, &bah_surface, boohoo_rect);
     }
     else
     {
      const unsigned sf = (CurrentScaler->id ==  NTVB_SCALE2X) ? 2 : (CurrentScaler->id == NTVB_SCALE4X) ? 4 : 3;
      uint8 *source_pixies = ((bypp == 4) ? (uint8*)eff_source_surface->pixels : (uint8*)eff_source_surface->pixels16) + eff_src_rect.x * bypp + eff_src_rect.y * eff_source_surface->pitchinpix * bypp;

	//printf("%d %d\n", sf, bypp);

      scale(sf, screen_pixies, screen_pitch, source_pixies, eff_source_surface->pitchinpix * bypp, bypp, eff_src_rect.w, eff_src_rect.h);
     }
#endif
    }
    else if(CurrentScaler->id == NTVB_NN2X || CurrentScaler->id == NTVB_NN3X || CurrentScaler->id == NTVB_NN4X)
    {
     nnx(CurrentScaler->id - NTVB_NN2X + 2, eff_source_surface, eff_src_rect, &bah_surface, boohoo_rect);
    }
    else if(CurrentScaler->id == NTVB_NNY2X || CurrentScaler->id == NTVB_NNY3X || CurrentScaler->id == NTVB_NNY4X)
    {
     nnyx(CurrentScaler->id - NTVB_NNY2X + 2, eff_source_surface, eff_src_rect, &bah_surface, boohoo_rect);
    }
#ifdef WANT_FANCY_SCALERS
    else
    {
     uint8 *source_pixies = (uint8 *)(eff_source_surface->pixels + eff_src_rect.x + eff_src_rect.y * eff_source_surface->pitchinpix);

     if(CurrentScaler->id == NTVB_HQ2X)
      hq2x_32(source_pixies, screen_pixies, eff_src_rect.w, eff_src_rect.h, eff_source_surface->pitchinpix * sizeof(uint32), screen_pitch);
     else if(CurrentScaler->id == NTVB_HQ3X)
      hq3x_32(source_pixies, screen_pixies, eff_src_rect.w, eff_src_rect.h, eff_source_surface->pitchinpix * sizeof(uint32), screen_pitch);
     else if(CurrentScaler->id == NTVB_HQ4X)
      hq4x_32(source_pixies, screen_pixies, eff_src_rect.w, eff_src_rect.h, eff_source_surface->pitchinpix * sizeof(uint32), screen_pitch);
     else if(CurrentScaler->id == NTVB_2XSAI || CurrentScaler->id == NTVB_SUPER2XSAI || CurrentScaler->id == NTVB_SUPEREAGLE)
     {
      if(bypp == 4)
       BlitSaI<uint32>(eff_source_surface, eff_src_rect, &bah_surface);
      else
       BlitSaI<uint16>(eff_source_surface, eff_src_rect, &bah_surface);
     }

     // TODO: check performance of this conversion
     bah_surface.SetFormat(game_pf, true);
    }
#endif

    if(ogl_blitter)
     ogl_blitter->Blit(&bah_surface, &boohoo_rect, &dest_rect, &eff_src_rect, InterlaceField, evideoip, rotated);
    else
    {
     SDL_to_MDFN_Surface_Wrapper m_surface(screen);

     MDFN_StretchBlitSurface(&bah_surface, boohoo_rect, &m_surface, dest_rect, false, video_settings.scanlines, &eff_src_rect, rotated, InterlaceField);
    }
   }
   else // No special scaler:
   {
    if(ogl_blitter)
     ogl_blitter->Blit(eff_source_surface, &eff_src_rect, &dest_rect, &eff_src_rect, InterlaceField, evideoip, rotated);
    else
    {
     SDL_to_MDFN_Surface_Wrapper m_surface(screen);

     MDFN_StretchBlitSurface(eff_source_surface, eff_src_rect, &m_surface, dest_rect, false, video_settings.scanlines, &eff_src_rect, rotated, InterlaceField);
    }
   }
}

// FIXME: JoyGunTranslate is kind of sloppy, not entirely thread safe, though the consequences in practice should be negligible...
static struct
{
 float mul;
 float sub;
} volatile JoyGunTranslate[2] = { { 0, 0 }, { 0, 0 } };

void BlitScreen(MDFN_Surface *msurface, const MDFN_Rect *DisplayRect, const int32 *LineWidths, const int new_rotated, const int InterlaceField, const bool take_ssnapshot)
{
 //
 // Reduce CPU usage when minimized, and prevent OpenGL memory quasi-leaks on Windows(though I have the feeling there's a
 // cleaner less-racey way to prevent that memory leak problem).
 //
 {
  int wflags = SDL_GetWindowFlags(window);

  // printf("Borp %08x %u\n", wflags, Time::MonoMS());

  if(wflags & (SDL_WINDOW_HIDDEN | SDL_WINDOW_MINIMIZED))
  {
   return;
  }
 }
 //
 //
 //
 MDFN_Rect src_rect;

 if(rotated != new_rotated)
 {
  rotated = new_rotated;
  //
  if(video_settings.fullscreen)
  {
   if(MDFN_UNLIKELY(!GenerateFullscreenDestRect()))
    Video_Sync(VideoGI);
   else
    MarkNeedBBClear();
  }
  else
   Video_Sync(VideoGI);
 }

 if(vdriver != VDRIVER_OPENGL)
 {
  int ow, oh, op;
  int nw, nh, np;
  SDL_PixelFormat of, nf;

  of = *screen->format;
  ow = screen->w;
  oh = screen->h;
  op = screen->pitch;
  if(!(screen = SDL_GetWindowSurface(window)))
   throw MDFN_Error(0, _("SDL_GetWindowSurface() failed: %s"), SDL_GetError());

  nf = *screen->format;
  nw = screen->w;
  nh = screen->h;
  np = screen->pitch;

  if((of.format != nf.format) || (of.BitsPerPixel != nf.BitsPerPixel) || (of.BytesPerPixel != nf.BytesPerPixel) ||
     (of.Rmask != nf.Rmask) || (of.Gmask != nf.Gmask) || (of.Bmask != nf.Bmask) || (of.Amask != nf.Amask) ||
     (ow != nw) || (oh != nh) || (op != np))
  {
   Video_Sync(VideoGI);
  }
 }

#if 1
 {
  int sub[2] = { 0, 0 };
  SDL_DisplayMode mode;

  if(!(SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN))
   SDL_GetWindowPosition(window, &sub[0], &sub[1]);

  sub[0] += screen_dest_rect.x;
  sub[1] += screen_dest_rect.y;

  if(SDL_GetCurrentDisplayMode(SDL_GetWindowDisplayIndex(window), &mode) >= 0)
  {
   JoyGunTranslate[0].mul = (float)mode.w / screen_dest_rect.w / 32768.0;
   JoyGunTranslate[0].sub = (float)sub[0] / screen_dest_rect.w;

   JoyGunTranslate[1].mul = (float)mode.h / screen_dest_rect.h / 32768.0;
   JoyGunTranslate[1].sub = (float)sub[1] / screen_dest_rect.h;
  }
 }
#endif
 //
 //
 //
 const uint32 curtime = Time::MonoMS();

 if(NeedClear || video_settings.force_bbclear)
 {
  //printf("BBClear 0x%08x %d\n", curtime, NeedClear);

  if((curtime - LastBBClearTime) >= 30)
  {
   LastBBClearTime = curtime;
   NeedClear -= (bool)NeedClear;
  }

  if(vdriver == VDRIVER_OPENGL)
   ogl_blitter->ClearBackBuffer();
  else
  {
   SDL_FillRect(screen, NULL, 0);
   NeedClear = 0;
  }
 }

 msurface->SetFormat(emu_pf, true);

 src_rect.x = DisplayRect->x;
 src_rect.w = DisplayRect->w;
 src_rect.y = DisplayRect->y;
 src_rect.h = DisplayRect->h;

 //printf("      Src: %dx%d - %d,%d\n",src_rect.w,src_rect.h,src_rect.x,src_rect.y); 
 //printf("VIDEO SLK Scale factors: x=%d y=%d\n", sr_x_scale, sr_y_scale);
 //printf("  src_rect:  %dx%d - %d,%d\n",src_rect.w,src_rect.h,src_rect.x,src_rect.y);
 //printf("  Screen_dest_rect: %dx%d - %d,%d\n",screen_dest_rect.w,screen_dest_rect.h,screen_dest_rect.x,screen_dest_rect.y);

 if(LineWidths[0] == ~0) // Skip multi line widths code?
 {
  //printf("  --- Blit full frame ---\n");
  //SLK - blit for: MD & NES & [...]
  //printf("      Src: %dx%d - %d,%d\n",src_rect.w,src_rect.h,src_rect.x,src_rect.y);
  //printf("     Dest: %dx%d - %d,%d\n",screen_dest_rect.w,screen_dest_rect.h,screen_dest_rect.x,screen_dest_rect.y);
  if(use_native_resolution || use_super_resolution || use_switchres)
  {
   MDFN_Rect sub_dest_rect = screen_dest_rect;
   
   //printf("Sergi %d %d %d %d \n",src_rect.w,sr_x_scale,sub_dest_rect.w,screen_dest_rect.w);
   if((src_rect.w * sr_x_scale) > sub_dest_rect.w)
   {
    //printf("Horizontal crop: ON\n");
    src_rect.x = (src_rect.w - sub_dest_rect.w) / 2;
    src_rect.w = sub_dest_rect.w / sr_x_scale;
   }
   sub_dest_rect.y = (((sub_dest_rect.h / sr_y_scale) - src_rect.h) / 2) * sr_y_scale; // V. Center
   sub_dest_rect.h = src_rect.h * sr_y_scale;
       
   //printf("    Output - SLK sub_dest_rect:   %dx%d %d,%d\n",sub_dest_rect.w,sub_dest_rect.h,sub_dest_rect.x,sub_dest_rect.y);
   //printf("    Output - SLK sub_screen_rect: %dx%d %d,%d\n",screen_dest_rect.w,screen_dest_rect.h,screen_dest_rect.x,screen_dest_rect.y);   
   SubBlit(msurface, src_rect, screen_dest_rect, InterlaceField);
   
  }
  else
  {
   SubBlit(msurface, src_rect, screen_dest_rect, InterlaceField);
  }
 }
 else
 {
  int y;
  int last_y = src_rect.y;
  int last_width = LineWidths[src_rect.y];  
   
  MDFN_Rect sub_src_rect;
  MDFN_Rect sub_dest_rect;

  for(y = src_rect.y; y < (src_rect.y + src_rect.h + 1); y++)
  {
   if(y == (src_rect.y + src_rect.h) || LineWidths[y] != last_width)
   {
    sub_src_rect.x = src_rect.x;
    sub_src_rect.w = last_width;
    sub_src_rect.y = last_y;
    sub_src_rect.h = y - last_y;

    if(rotated == MDFN_ROTATE90)
    {
     sub_dest_rect.x = screen_dest_rect.x + (last_y - src_rect.y) * screen_dest_rect.w / src_rect.h;
     sub_dest_rect.y = screen_dest_rect.y;

     sub_dest_rect.w = sub_src_rect.h * screen_dest_rect.w / src_rect.h;
     sub_dest_rect.h = screen_dest_rect.h;
     //printf("sdr.x=%f, sdr.w=%f\n", (double)screen_dest_rect.x + (double)(last_y - src_rect.y) * screen_dest_rect.w / src_rect.h, (double)sub_src_rect.h * screen_dest_rect.w / src_rect.h);
    }
    else if(rotated == MDFN_ROTATE270)
    {
     sub_dest_rect.x = screen_dest_rect.x + (src_rect.h - (y - src_rect.y)) * screen_dest_rect.w / src_rect.h;
     sub_dest_rect.y = screen_dest_rect.y;

     sub_dest_rect.w = sub_src_rect.h * screen_dest_rect.w / src_rect.h;
     sub_dest_rect.h = screen_dest_rect.h;
    }
    else
    {
     if (use_native_resolution || use_super_resolution || use_switchres)
      {
       // SLK - (prevent) resizing and centering
       //printf("      src_rect:    %dx%d - X:%d, cmdY:%d rect.x %d\n",src_rect.w,src_rect.h,src_rect.x,src_rect.y,screen_dest_rect.x);
       sub_dest_rect.x = screen_dest_rect.x;
       sub_dest_rect.w = screen_dest_rect.w;
       sub_dest_rect.y = screen_dest_rect.y + ((last_y - src_rect.y) * sr_y_scale); 
       sub_dest_rect.h = sub_src_rect.h * sr_y_scale;
                     
       //printf("sub_src_rect:    %dx%d %d,%d\n",sub_src_rect.w,sub_src_rect.h,sub_src_rect.x,sub_src_rect.y);       
       if (sub_src_rect.w  > (sub_dest_rect.w / sr_x_scale)) // horizontal crop to fit screen
       {
       	//psakhis horrible hack centering psx       
        //printf("Horizontal centering ON sub_src_rect.x = %d, (sub_src_rect.w  vs sub_dest_rect.w / sr_x_scale):%d vs %d\n",sub_src_rect.x,sub_src_rect.w, (sub_dest_rect.w / sr_x_scale));         
        //if (psx_native_resolution_bars && sub_src_rect.w > sub_dest_rect.w && sr_x_scale == 1) {        	        	        
        //  sub_src_rect.x = 32 + ((sub_src_rect.w - sub_dest_rect.w) / 2);           	
        //  sub_src_rect.w = sub_dest_rect.w / sr_x_scale;        
        //}
        //psakhis 
        //else { 
          //SLK
          // sub_src_rect.x = (sub_src_rect.w - (sub_dest_rect.w / sr_x_scale)) / 2;  
          //psakhis correct centering                       
          sub_src_rect.x = sub_src_rect.x + ((sub_src_rect.w - (sub_dest_rect.w / sr_x_scale)) / 2);
          sub_src_rect.w = sub_dest_rect.w / sr_x_scale;        
	//}
       } 
       
       // OLD 1.24.1 stretch ok       
       /*   if (sub_src_rect.w >= resolution_to_change_w) // horizontal crop to fit screen
       {
         sub_src_rect.x = sub_src_rect.x + ((sub_src_rect.w - resolution_to_change_w) / 2);
         sub_src_rect.w = resolution_to_change_w;
       }*/
           
       if (native_resolution_vcenter == true) // default vertical centering
       {
        //printf("    Vertical centering ON - src_rect.h:%d sub_dest_rect.y:%d screen_dest_rect.h:%d sr_y_scale:%d \n",src_rect.h,sub_dest_rect.y,screen_dest_rect.h,sr_y_scale);
        sub_dest_rect.y = sub_dest_rect.y + ((screen_dest_rect.h - (src_rect.h * sr_y_scale)) / 2);
       }
       else // fill screen (psx - half black screen fix)
       {
        //printf("    Vertical centering OFF (psx)\n");
        sub_dest_rect.h = sub_src_rect.h * (screen_dest_rect.h) / src_rect.h;
        // sub_dest_rect.h = (sub_src_rect.h + sub_src_rect.y) * sr_y_scale;
       }       
       //printf("      Output - SLK sub_src_rect:    %dx%d %d,%d\n",sub_src_rect.w,sub_src_rect.h,sub_src_rect.x,sub_src_rect.y);
       //printf("      Output - SLK sub_dest_rect:   %dx%d %d,%d\n",sub_dest_rect.w,sub_dest_rect.h,sub_dest_rect.x,sub_dest_rect.y);
       //printf("      Output - SLK sub_screen_rect: %dx%d %d,%d\n",screen_dest_rect.w,screen_dest_rect.h,screen_dest_rect.x,screen_dest_rect.y);

    }
    else
    {
    // printf("      src_rect:    %dx%d - X:%d, cmdY:%d rect.x %d\n",src_rect.w,src_rect.h,src_rect.x,src_rect.y,screen_dest_rect.x);
     sub_dest_rect.x = screen_dest_rect.x;
     sub_dest_rect.w = screen_dest_rect.w;
     sub_dest_rect.y = screen_dest_rect.y + (last_y - src_rect.y) * screen_dest_rect.h / src_rect.h;
     sub_dest_rect.h = sub_src_rect.h * screen_dest_rect.h / src_rect.h;
    }

    if(!sub_dest_rect.h) // May occur with small yscale values in certain cases, so prevent triggering an assert()
     sub_dest_rect.h = 1;

    // Blit here!    
    SubBlit(msurface, sub_src_rect, sub_dest_rect, InterlaceField);

    last_y = y;

    if(y != (src_rect.y + src_rect.h))
    {
     last_width = LineWidths[y];
    }
   } 
  }
 }
}

 if(take_ssnapshot)
 {
  try
  {
   std::unique_ptr<MDFN_Surface> ib;
   MDFN_Rect sr;
   MDFN_Rect tr;

   sr = screen_dest_rect;
   if(sr.x < 0) { sr.w += sr.x; sr.x = 0; }
   if(sr.y < 0) { sr.h += sr.y; sr.y = 0; }
   if(sr.w < 0) sr.w = 0;
   if(sr.h < 0) sr.h = 0;
   if(sr.w > screen_w) sr.w = screen_w;
   if(sr.h > screen_h) sr.h = screen_h;

   ib.reset(new MDFN_Surface(NULL, sr.w, sr.h, sr.w, osd_pf));

   if(ogl_blitter)
    ogl_blitter->ReadPixels(ib.get(), &sr);
   else
   {
    if(SDL_MUSTLOCK(screen))
     SDL_LockSurface(screen);

    for(int y = 0; y < sr.h; y++)
    {
     for(int x = 0; x < sr.w; x++)
     {
      ib->pixels[y * ib->pitchinpix + x] = ((uint32*)((uint8*)screen->pixels + (sr.y + y) * screen->pitch))[sr.x + x];
     }
    }

    if(SDL_MUSTLOCK(screen))
     SDL_UnlockSurface(screen);
   }


   tr.x = tr.y = 0;
   tr.w = ib->w;
   tr.h = ib->h;
   MDFNI_SaveSnapshot(ib.get(), &tr, NULL);
  }
  catch(std::exception &e)
  {
   MDFND_OutputNotice(MDFN_NOTICE_ERROR, e.what());
  }
 }


 Debugger_MT_DrawToScreen(osd_pf, screen_w, screen_h);

#if 0
 if(CKGUI_IsActive())
 {
  if(!CKGUISurface)
  {
   CKGUIRect.w = screen_w;
   CKGUIRect.h = screen_h;

   CKGUISurface = SDL_CreateRGBSurface(SDL_SWSURFACE | SDL_SRCALPHA, CKGUIRect.w, CKGUIRect.h, 32, 0xFF << real_rs, 0xFF << real_gs, 0xFF << real_bs, 0xFF << real_as);
   SDL_SetColorKey(CKGUISurface, SDL_SRCCOLORKEY, 0);
   SDL_SetAlpha(CKGUISurface, SDL_SRCALPHA, 0);
  }
  MDFN_Rect zederect = CKGUIRect;
  CKGUI_Draw(CKGUISurface, &CKGUIRect);
  BlitOSD(CKGUISurface, &CKGUIRect, &zederect);
 }
 else if(CKGUISurface)
 {
  SDL_FreeSurface(CKGUISurface);
  CKGUISurface = NULL;
 }
#endif

 try
 {
  DrawSaveStates(screen_w, screen_h, exs, eys, osd_pf);

  CheatIF_MT_Draw(osd_pf, screen_w, screen_h);
  Netplay_MT_Draw(osd_pf, screen_w, screen_h);

  if(Help_IsActive())
  {
   if(!HelpSurface)
   {
    HelpRect.x = 0;
    HelpRect.y = 0;
    HelpRect.w = std::min<int>(512, screen_w);
    HelpRect.h = std::min<int>(408, screen_h);

    HelpSurface = new MDFN_Surface(NULL, 512, 408, 512, osd_pf);
    Help_Draw(HelpSurface, HelpRect);
   }

   MDFN_Rect zederect;
   int32 sfx = screen_w / HelpRect.w;
   int32 sfy = screen_h / HelpRect.h;

   if(sfy > sfx)
    sfy = sfx + 1;

   if(sfx > sfy)
    sfx = sfy + 1;

   zederect.w = HelpRect.w * sfx;
   zederect.h = HelpRect.h * sfy;

   zederect.x = (screen_w - zederect.w) / 2;
   zederect.y = (screen_h - zederect.h) / 2;

   BlitOSD(HelpSurface, &HelpRect, &zederect, 0);
  }
  else if(HelpSurface)
  {
   delete HelpSurface;
   HelpSurface = NULL;
  }
 }
 catch(std::exception& e)
 {
  MDFND_OutputNotice(MDFN_NOTICE_ERROR, e.what());
 }

 BlitInternalMessage(curtime);

 //
 {
  int32 p[4] = { 0, 0, screen_w, screen_h };
  MDFN_Rect cr;

  // When using soft-SDL, position the FPS display so we won't incur a potentially large(on older/slower hardware) penalty due
  // to a requisite backbuffer clear(we could avoid this with some sort of dirty-rects system so only parts of the backbuffer are cleared,
  // but that gets awfully complicated and prone to bugs when dealing with double/triple-buffered video...).
  //
  // std::max so we don't position it offscreen if the user has selected xscalefs or yscalefs values that are too large.
  if(vdriver != VDRIVER_OPENGL)
  {
   p[0] = std::max<int32>(std::min<int32>(screen_w, screen_dest_rect.x), 0);
   p[1] = std::max<int32>(std::min<int32>(screen_h, screen_dest_rect.y), 0);

   p[2] = std::max<int32>(std::min<int32>(screen_w, screen_dest_rect.x + screen_dest_rect.w), 0);
   p[3] = std::max<int32>(std::min<int32>(screen_h, screen_dest_rect.y + screen_dest_rect.h), 0);
  }

  cr = { p[0], p[1], p[2] - p[0], p[3] - p[1] };
  FPS_DrawToScreen(osd_pf, cr, std::min(screen_w, screen_h));
 }
 //

 if(vdriver != VDRIVER_OPENGL)
  SDL_UpdateWindowSurface(window);
 else
 {
  PumpWrap();
  SDL_GL_SwapWindow(window);
  // Don't insert any GL calls after SDL_GL_SwapWindow() here that could block until the swap completes.
  //ogl_blitter->HardSync();
 }
}

void Video_Exposed(void)
{
 MarkNeedBBClear();
}

void Video_PtoV(const int in_x, const int in_y, float* out_x, float* out_y)
{
 assert(VideoGI);
 //
 int32 tmp_x = in_x - screen_dest_rect.x;
 int32 tmp_y = in_y - screen_dest_rect.y;
 int32 div_x = screen_dest_rect.w;
 int32 div_y = screen_dest_rect.h;
/*
 switch(rotated)
 {
  case MDFN_ROTATE0:
	break;

  case MDFN_ROTATE90:
	std::swap(tmp_x, tmp_y);
	std::swap(div_x, div_y);
	tmp_x = screen_dest_rect.h - 1 - tmp_x;
	break;

  case MDFN_ROTATE270:
	std::swap(tmp_x, tmp_y);
	std::swap(div_x, div_y);
	tmp_y = screen_dest_rect.w - 1 - tmp_y;
	break;
 }
*/
 *out_x = (float)tmp_x / div_x;
 *out_y = (float)tmp_y / div_y;
}

float Video_PtoV_J(const int32 inv, const bool axis, const bool scr_scale)
{
 assert(VideoGI);
 assert(window);

 if(!scr_scale)
  return 0.5 + (inv / 32768.0 - 0.5) * std::max<int32>(VideoGI->nominal_width, VideoGI->nominal_height) / (axis ? VideoGI->nominal_height : VideoGI->nominal_width);
 else
  return inv * JoyGunTranslate[axis].mul - JoyGunTranslate[axis].sub;
}
