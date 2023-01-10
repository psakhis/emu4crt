/* Mednafen - Multi-system Emulator
 *
 * Copyright notice for this file:
 *  Copyright (C) 2002 Xodnizel
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

#include        "share.h"
#include <mednafen/mednafen.h>   // PSAKHIS TODO: should do better than this!

namespace MDFN_IEN_NES
{

typedef struct {
        uint32 mzx,mzy;
	uint8 mzb;
        int bogo;        
	uint64 zaphit;
	uint8 zaplatch;
	int gunlight_frames; //psakhis		
} ZAPPER;

static ZAPPER ZD[2];

static void ZapperFrapper(int w, uint8 *bg, uint32 linets, int final)
{
 int xs,xe;
 int zx,zy;

 xs=linets;
 xe=final;

 if(xe > 256)
  xe = 256;

 zx=ZD[w].mzx;
 zy=ZD[w].mzy;

 if(scanline>=(zy-4) && scanline<=(zy+4))
 {
  while(xs<xe)
  {
    uint8 a1;
    uint32 sum;
    if(xs<=(zx+4) && xs>=(zx-4))
    {
     a1 = bg[xs] & 0x3F;
     sum = ActiveNESPalette[a1].r + ActiveNESPalette[a1].g + ActiveNESPalette[a1].b;

     if(sum>=100*3)
     {
      ZD[w].zaphit = timestampbase + timestamp;
     // printf("Hit: %d %d %ld\n", scanline, timestamp, timestampbase + timestamp);
      goto endo;
     }
    }
   xs++;
  }
 }
 
 endo:;
}

static int CheckColor(int w)
{
 MDFNPPU_LineUpdate();

 if((ZD[w].zaphit+100)>=(timestampbase+timestamp) && !(ZD[w].mzb&2))
 {
  return(0);
 }
 return(1);
}

static uint8 ReadZapperVS(int w)
{
                uint8 ret = ZD[w].zaplatch & 1;

		if(!fceuindbg)
		 ZD[w].zaplatch>>=1;
                return ret;
}

static void StrobeZapperVS(int w)
{                        
	ZD[w].zaplatch = (1 << 4) | (ZD[w].bogo ? 0x80 : 0x00) | (CheckColor(w) ? 0x00 : 0x40);
//        printf("Strobe: %d %ld %02x\n", timestamp, timestampbase + timestamp, ZD[w].zaplatch);
}

static uint8 ReadZapper(int w)
{
                uint8 ret=0;

                if(ZD[w].bogo)
                 ret|=0x10;
                if(CheckColor(w))
                 ret|=0x8;
                                 	 
                return ret;
}

static void DrawZapper(int w, uint8* pix, int pix_y)
{
 if (!MDFN_GetSettingB("nes.input.zapper.crosshair"))	//psakhis
 	NESCURSOR_DrawGunSight(w, pix, pix_y, ZD[w].mzx, ZD[w].mzy);
}

static void UpdateZapper(int w, void *data)
{
 uint8 *data_8 = (uint8 *)data;
 uint32 new_x = (int16)MDFN_de16lsb(data_8 + 0);
 uint32 new_y = (int16)MDFN_de16lsb(data_8 + 2);
 uint8 new_b = *(uint8 *)(data_8 + 4);

 if(ZD[w].bogo)
  ZD[w].bogo--;
 
 //PSAKHIS   
 //https://www.nesdev.org/wiki/Zapper
 //Like the Tomee Zapp Gun, no 100ms wait for pull (simple switch)
 //This works with most existing zapper games which usually fire on a transition from 1 to 0. 
 if (MDFN_GetSettingB("nes.input.zapper.clone")) 
 { 	 		 
  	if(new_b&3) //  simpler switch that returns 1 while the trigger is not pulled, and 0 when it is pulled		  
  	 ZD[w].bogo = 0;  	   	
  	else
  	 ZD[w].bogo = 1;   	 	  	  		
 }  // END PSAKHIS	
 else
  if((new_b&3) && (!(ZD[w].mzb&3))) 
   ZD[w].bogo=5; 		
   
 //PSAKHIS  
  if(ZD[w].gunlight_frames) 
  ZD[w].gunlight_frames--;

  if((!(ZD[w].bogo)) && (!(ZD[w].mzb&3))) 
   ZD[w].gunlight_frames = gunlight_frames; //psakhis gunlight  		
  
  if(ZD[w].gunlight_frames)  
   gunlight_apply = true; 
  else
   gunlight_apply = false;
//END PSAKHIS  

 ZD[w].mzx = new_x;
 ZD[w].mzy = new_y;
 ZD[w].mzb = new_b;
 
 //printf("La: %08x %08x %08x %08x %08x\n", new_x, new_y, new_b, ZD[w].bogo, ZD[w].gunlight_frames);
}

static void StateAction(int w, StateMem *sm, const unsigned load, const bool data_only)
{
 SFORMAT StateRegs[] =
 {
  SFVAR(ZD[w].mzx),
  SFVAR(ZD[w].mzy),
  SFVAR(ZD[w].mzb),
  SFVAR(ZD[w].bogo),
  SFVAR(ZD[w].zaphit),
  SFVAR(ZD[w].zaplatch),
  SFEND
 };

 MDFNSS_StateAction(sm, load, data_only, StateRegs, w ? "INP1" : "INP0", true);

 if(load)
 {

 }
}

static INPUTC ZAPC={ReadZapper,0,0,UpdateZapper,ZapperFrapper,DrawZapper, StateAction };
static INPUTC ZAPVSC={ReadZapperVS,0,StrobeZapperVS,UpdateZapper,ZapperFrapper,DrawZapper, StateAction };

INPUTC *MDFN_InitZapper(int w)
{
  memset(&ZD[w],0,sizeof(ZAPPER));

  if(NESIsVSUni)
   return(&ZAPVSC);
  else
   return(&ZAPC);
   
}

}
