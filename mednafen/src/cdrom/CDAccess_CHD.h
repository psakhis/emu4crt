/******************************************************************************/
/* Mednafen - Multi-system Emulator                                           */
/******************************************************************************/
/* CDAccess_CHD.h:
**  Copyright (C) 2017 Romain Tisserand
**
** This program is free software; you can redistribute it and/or
** modify it under the terms of the GNU General Public License
** as published by the Free Software Foundation; either version 2
** of the License, or (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software Foundation, Inc.,
** 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

#include <mednafen/libchdr/include/libchdr/chd.h>
#include "CDAccess.h"


namespace Mednafen
{
struct CHDFILE_TRACK_INFO
{
   int32 LBA;

   uint32 DIFormat;
   uint8 subq_control;

   int32 pregap;
   int32 pregap_dv;

   int32 postgap;

   int32 index[100];

   int32 sectors; // Not including pregap sectors!
   bool FirstFileInstance;
   bool RawAudioMSBFirst;
   long FileOffset;
   unsigned int SubchannelMode;

   uint32 LastSamplePos;
   
   uint32_t fileOffset;
};

class CDAccess_CHD : public CDAccess
{
 public:

 CDAccess_CHD(VirtualFS* vfs, const std::string& path, bool image_memcache);
 virtual ~CDAccess_CHD();

 virtual void Read_Raw_Sector(uint8 *buf, int32 lba);

 virtual bool Fast_Read_Raw_PW_TSRE(uint8* pwbuf, int32 lba) const noexcept;

 virtual void Read_TOC(CDUtility::TOC *toc);

 private:

 void Load(VirtualFS* vfs, const std::string& path, bool image_memcache);
 void Cleanup(void);

  // MakeSubPQ will OR the simulated P and Q subchannel data into SubPWBuf.
  int32 MakeSubPQ(int32 lba, uint8 *SubPWBuf) const;

  bool Read_CHD_Hunk_RAW(uint8 *buf, int32 lba, CHDFILE_TRACK_INFO* track);
  bool Read_CHD_Hunk_M1(uint8 *buf, int32 lba, CHDFILE_TRACK_INFO* track);
  bool Read_CHD_Hunk_M2(uint8 *buf, int32 lba, CHDFILE_TRACK_INFO* track);
  void LoadSBI(VirtualFS* vfs, const std::string& sbi_path);
  int isPCECD(int32 lba);
  
  int32 NumTracks;
  int32 FirstTrack;
  int32 LastTrack;
  int32 total_sectors;
  uint8 disc_type;
  CDUtility::TOC tocd;
  
  std::map<uint32, std::array<uint8, 12>> SubQReplaceMap;
  std::string base_dir;
  
  CHDFILE_TRACK_INFO Tracks[100]; // Track #0(HMM?) through 99
     
  //struct disc;
  //struct session sessions[DISC_MAX_SESSIONS];
  int num_sessions;
  //struct track tracks[DISC_MAX_TRACKS];
  int num_tracks;

  chd_file *chd;
  /* hunk data cache */
  uint8 *hunkmem;
  /* last hunknum read */
  int oldhunk;
  
  /*
  int findPCECD;  
  int lbaPCECD_offset; //offset magicCD  
  */
    
};
}