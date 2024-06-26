/******************************************************************************/
/* Mednafen - Multi-system Emulator                                           */
/******************************************************************************/
/* CDAccess_CHD.cpp:
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

//#include <mednafen/git.h>
#include <mednafen/mednafen.h>
#include <mednafen/general.h>
#include <mednafen/string/string.h>
#include <mednafen/MemoryStream.h>
#include <mednafen/FileStream.h>

#include "CDAccess_CHD.h"
#include <trio/trio.h>

#include <map>

namespace Mednafen
{

using namespace CDUtility;

// Disk-image(rip) track/sector formats
enum
{
  DI_FORMAT_AUDIO = 0x00,
  DI_FORMAT_MODE1 = 0x01,
  DI_FORMAT_MODE1_RAW = 0x02,
  DI_FORMAT_MODE2 = 0x03,
  DI_FORMAT_MODE2_FORM1 = 0x04,
  DI_FORMAT_MODE2_FORM2 = 0x05,
  DI_FORMAT_MODE2_RAW = 0x06,
  DI_FORMAT_CDI_RAW = 0x07,
  _DI_FORMAT_COUNT
};

static const int32 DI_Size_Table[8] =
    {
        2352, // Audio
        2048, // MODE1
        2352, // MODE1 RAW
        2336, // MODE2
        2048, // MODE2 Form 1
        2324, // Mode 2 Form 2
        2352, // MODE2 RAW
        2352  // CD-I RAW
};

CDAccess_CHD::CDAccess_CHD(VirtualFS* vfs, const std::string& path, bool image_memcache) : NumTracks(0), total_sectors(0)
{
 /* findPCECD = 0; 
  lbaPCECD_offset = 0; */
  Load(vfs, path, image_memcache);
}

void CDAccess_CHD::Load(VirtualFS* vfs, const std::string& path, bool image_memcache)
{
  chd_error err = chd_open(path.c_str(), CHD_OPEN_READ, NULL, &chd); 
  if (err != CHDERR_NONE)
  {
    throw MDFN_Error(0, _("Failed to load CHD image: %s"), path.c_str());       
  }

  if (image_memcache)
  {
    err = chd_precache(chd);

    if (err != CHDERR_NONE)
    {
      throw MDFN_Error(0, _("Failed to pre-cache CHD image: %s"), path.c_str());              
    }
  }

  /* allocate storage for sector reads */
  const chd_header *head = chd_get_header(chd);
  hunkmem = (uint8 *)malloc(head->hunkbytes);
  oldhunk = -1;
 
  int plba = -150;
  int numsectors = 0;
  int fileOffset = 0;
  while (1)
  {
    int tkid = 0, frames = 0, pregap = 0, postgap = 0;
    char type[64], subtype[32], pgtype[32], pgsub[32];
    char tmp[512];

    err = chd_get_metadata(chd, CDROM_TRACK_METADATA2_TAG, NumTracks, tmp, sizeof(tmp), NULL, NULL, NULL);
    if (err == CHDERR_NONE)
    {
      sscanf(tmp, CDROM_TRACK_METADATA2_FORMAT, &tkid, type, subtype, &frames, &pregap, pgtype, pgsub, &postgap);
    }
    else
    {
      /* try to read the old v3/v4 metadata tag */
      err = chd_get_metadata(chd, CDROM_TRACK_METADATA_TAG,
                             NumTracks, tmp, sizeof(tmp), NULL, NULL,
                             NULL);
      if (err == CHDERR_NONE)
      {
        sscanf(tmp, CDROM_TRACK_METADATA_FORMAT, &tkid, type, subtype,
               &frames);
      }
      else
      {
        /* if there's no valid metadata, this is the end of the TOC */
        break;
      }
    }

    if (strcmp(type, "MODE1") && strcmp(type, "MODE1_RAW") && strcmp(type, "MODE2_RAW") &&
        strcmp(type, "AUDIO"))
    {
      throw MDFN_Error(0, _("chd_parse track type %s unsupported\n"), type);           
    }

    if (strcmp(subtype, "NONE"))
    {
      throw MDFN_Error(0, _("chd_parse track subtype %s unsupported\n"), subtype);                  
    }

    /* add track */
    NumTracks++;
    tocd.tracks[NumTracks].adr = 1;
    tocd.tracks[NumTracks].control = strcmp(type, "AUDIO") == 0 ? 0 : 4;
    tocd.tracks[NumTracks].valid = true;

    Tracks[NumTracks].pregap = (NumTracks == 1) ? 150 : (pgtype[0] == 'V') ? 0 : pregap;
    Tracks[NumTracks].pregap_dv = (pgtype[0] == 'V') ? pregap : 0;        
    plba += Tracks[NumTracks].pregap + Tracks[NumTracks].pregap_dv;
    Tracks[NumTracks].LBA = tocd.tracks[NumTracks].lba = plba;
    Tracks[NumTracks].postgap = postgap;
    Tracks[NumTracks].sectors = frames - Tracks[NumTracks].pregap_dv;
    Tracks[NumTracks].SubchannelMode = 0;
    Tracks[NumTracks].index[0] = -1;
    Tracks[NumTracks].index[1] = 0;
    for (int32 i = 2; i < 100; i++)
      Tracks[NumTracks].index[i] = -1;

    tocd.tracks[NumTracks].lba = plba;
    
    fileOffset += Tracks[NumTracks].pregap_dv;
    //printf("Tracks[%d].fileOffset=%d\n",NumTracks, fileOffset);
    Tracks[NumTracks].fileOffset = fileOffset;
    fileOffset += frames - Tracks[NumTracks].pregap_dv;
    fileOffset += Tracks[NumTracks].postgap;
    fileOffset += ((frames + 3) & ~3) - frames;

    if (strcmp(type, "AUDIO") == 0)
    {
      Tracks[NumTracks].DIFormat = DI_FORMAT_AUDIO;
      Tracks[NumTracks].RawAudioMSBFirst = 1;                       
    }
    else if (strcmp(type, "MODE1_RAW") == 0) {
      Tracks[NumTracks].DIFormat = DI_FORMAT_MODE1_RAW;
      /*
      if (!findPCECD) {
      	//MDFN_printf(_("Find offset ant PCE on lba %d, Max %d \n"), tocd.tracks[NumTracks].lba,frames + tocd.tracks[NumTracks].lba);
        lbaPCECD_offset = isPCECD(tocd.tracks[NumTracks].lba);
        //MDFN_printf(_("Offset pos PCE %d \n"), lbaPCECD_offset);
        findPCECD = 1;
      }  
      */
    }  
    else if (strcmp(type, "MODE2_RAW") == 0)
      Tracks[NumTracks].DIFormat = DI_FORMAT_MODE2_RAW;
    else if (strcmp(type, "MODE1") == 0)
      Tracks[NumTracks].DIFormat = DI_FORMAT_MODE1;

    Tracks[NumTracks].subq_control = strcmp(type, "AUDIO") == 0 ? 0 : 4;    

    plba += frames - Tracks[NumTracks].pregap_dv;
    plba += Tracks[NumTracks].postgap;
 
    numsectors += (NumTracks == 1) ? frames : frames + Tracks[NumTracks].pregap;
    
   /* if (NumTracks < FirstTrack)
       FirstTrack = NumTracks;
    if (NumTracks > LastTrack)
       LastTrack = NumTracks;*/
               
    tocd.first_track = 1;
    tocd.last_track = NumTracks;
  }

  FirstTrack = 1;
  LastTrack = NumTracks;
  total_sectors = numsectors;  

  /* add track */
  tocd.tracks[100].adr = 1;
  //tocd.tracks[100].control = 0;
  tocd.tracks[100].control = tocd.tracks[LastTrack].control;
  tocd.tracks[100].lba = numsectors; // HACK 
  tocd.tracks[100].valid = true;

  //
  // Adjust indexes for MakeSubPQ()
  //
  for (int x = FirstTrack; x < (FirstTrack + NumTracks); x++)
  {
    const int32 base = Tracks[x].index[1];
    for (int32 i = 0; i < 100; i++)
    {
      if (i == 0 || Tracks[x].index[i] == -1)
        Tracks[x].index[i] = INT32_MAX;
      else
        Tracks[x].index[i] = Tracks[x].LBA + (Tracks[x].index[i] - base);

      assert(Tracks[x].index[i] >= 0);
    }
  }
  
  if (!SubQReplaceMap.empty())
   SubQReplaceMap.clear();
   
  //
  // Load SBI file, if present
  // 
   std::string file_base, file_ext;
   vfs->get_file_path_components(path, &base_dir, &file_base, &file_ext);
   char sbi_ext[4] = { 's', 'b', 'i', 0 };

   if(file_ext.length() == 4 && file_ext[0] == '.')
   {
    for(unsigned i = 0; i < 3; i++)
    {
     if(file_ext[1 + i] >= 'A' && file_ext[1 + i] <= 'Z')
      sbi_ext[i] += 'A' - 'a';
    }
   }
   LoadSBI(vfs, vfs->eval_fip(base_dir, file_base + "." + sbi_ext, true));  
}

CDAccess_CHD::~CDAccess_CHD()
{
  if (chd != NULL)
    chd_close(chd);

  if (hunkmem)
    free(hunkmem);
}

bool CDAccess_CHD::Read_CHD_Hunk_RAW(uint8 *buf, int32 lba, CHDFILE_TRACK_INFO* track)
{  
  const chd_header *head = chd_get_header(chd);
  //int cad = lba;    
  int cad = lba - track->LBA + track->fileOffset;
  int sph = head->hunkbytes / (2352 + 96);
  int hunknum = cad / sph; //(cad * head->unitbytes) / head->hunkbytes;
  int hunkofs = cad % sph; //(cad * head->unitbytes) % head->hunkbytes;
  int err = CHDERR_NONE;
  // MDFN_printf(_("cad %d SPH %d hunknum %d hunkofs %d \n"),cad,sph,hunknum,hunkofs);   	                  
  /* each hunk holds ~8 sectors, optimize when reading contiguous sectors */
  if (hunknum != oldhunk)
  {
    err = chd_read(chd, hunknum, hunkmem);
    if (err != CHDERR_NONE)
      throw MDFN_Error(0, _("chd_read_sector failed lba=%d error=%d\n"), lba, err);       
    else
      oldhunk = hunknum;
  }  
  memcpy(buf, hunkmem + hunkofs * (2352 + 96), 2352);
  
  return err;
}

bool CDAccess_CHD::Read_CHD_Hunk_M1(uint8 *buf, int32 lba, CHDFILE_TRACK_INFO* track)
{
  const chd_header *head = chd_get_header(chd);
  //int cad = lba; 
  int cad = lba - track->LBA + track->fileOffset;
  int sph = head->hunkbytes / (2352 + 96);
  int hunknum = cad / sph; //(cad * head->unitbytes) / head->hunkbytes;
  int hunkofs = cad % sph; //(cad * head->unitbytes) % head->hunkbytes;
  int err = CHDERR_NONE;

  /* each hunk holds ~8 sectors, optimize when reading contiguous sectors */
  if (hunknum != oldhunk)
  {
    err = chd_read(chd, hunknum, hunkmem);
    if (err != CHDERR_NONE)
      throw MDFN_Error(0, _("chd_read_sector failed lba=%d error=%d\n"), lba, err);
    else
      oldhunk = hunknum;
  }

  memcpy(buf + 16, hunkmem + hunkofs * (2352 + 96), 2048);

  return err;
}

bool CDAccess_CHD::Read_CHD_Hunk_M2(uint8 *buf, int32 lba, CHDFILE_TRACK_INFO* track)
{
  const chd_header *head = chd_get_header(chd);
  //int cad = lba; 
  int cad = lba - track->LBA + track->fileOffset; 
  int sph = head->hunkbytes / (2352 + 96);
  int hunknum = cad / sph; //(cad * head->unitbytes) / head->hunkbytes;
  int hunkofs = cad % sph; //(cad * head->unitbytes) % head->hunkbytes;
  int err = CHDERR_NONE;

  /* each hunk holds ~8 sectors, optimize when reading contiguous sectors */
  if (hunknum != oldhunk)
  {
    err = chd_read(chd, hunknum, hunkmem);
    if (err != CHDERR_NONE)
      throw MDFN_Error(0, _("chd_read_sector failed lba=%d error=%d\n"), lba, err);
    else
      oldhunk = hunknum;
  }

  memcpy(buf + 16, hunkmem + hunkofs * (2352 + 96), 2336);

  return err;
}

void CDAccess_CHD::Read_Raw_Sector(uint8 *buf, int32 lba)
{
  uint8 SimuQ[0xC];
  int32 track;
  CHDFILE_TRACK_INFO *ct;  
  //
  // Leadout synthesis
  //
  if (lba >= total_sectors)
  {
    uint8 data_synth_mode = 0x01; // Default for DISC_TYPE_CDDA_OR_M1, would be 0x02 for DISC_TYPE_CD_XA

    switch (Tracks[LastTrack].DIFormat)
    {
    case DI_FORMAT_AUDIO:
      break;

    case DI_FORMAT_MODE1_RAW:
    case DI_FORMAT_MODE1:
      data_synth_mode = 0x01;
      break;

    case DI_FORMAT_MODE2_RAW:
    case DI_FORMAT_MODE2_FORM1:
    case DI_FORMAT_MODE2_FORM2:
    case DI_FORMAT_MODE2:
    case DI_FORMAT_CDI_RAW:
      data_synth_mode = 0x02;
      break;
    }

    synth_leadout_sector_lba(data_synth_mode, tocd, lba, buf);    
  }

  memset(buf + 2352, 0, 96);
  track = MakeSubPQ(lba, buf + 2352);
  subq_deinterleave(buf + 2352, SimuQ);

  ct = &Tracks[track];

  //
  // Handle pregap and postgap reading
  //  
  if (lba < (ct->LBA - ct->pregap_dv) || lba >= (ct->LBA + ct->sectors))
  {
    int32 pg_offset = lba - ct->LBA;
    CHDFILE_TRACK_INFO *et = ct;

    if (pg_offset < -150)
    {
      if ((Tracks[track].subq_control & SUBQ_CTRLF_DATA) && (FirstTrack < track) && !(Tracks[track - 1].subq_control & SUBQ_CTRLF_DATA))
        et = &Tracks[track - 1];
    }	
    memset(buf, 0, 2352);    
    
    switch (et->DIFormat)
    {
    case DI_FORMAT_AUDIO:
      break;

    case DI_FORMAT_MODE1_RAW:
    case DI_FORMAT_MODE1:    
      encode_mode1_sector(lba + 150, buf);      
      break;

    case DI_FORMAT_MODE2_RAW:
    case DI_FORMAT_MODE2_FORM1:
    case DI_FORMAT_MODE2_FORM2:
    case DI_FORMAT_MODE2:
    case DI_FORMAT_CDI_RAW:
      buf[12 + 6] = 0x20;
      buf[12 + 10] = 0x20;
      encode_mode2_form2_sector(lba + 150, buf);
      // TODO: Zero out optional(?) checksum bytes?
      break;
    }
    //printf("Pre/post-gap read, LBA=%d(LBA-track_start_LBA=%d)\n", lba, lba - ct->LBA);
  }
  else
  {
    {      
      switch (ct->DIFormat)
      {
      case DI_FORMAT_AUDIO:        
        Read_CHD_Hunk_RAW(buf, lba, ct);        
        if (ct->RawAudioMSBFirst)
          Endian_A16_Swap(buf, 588 * 2);
        break;

      case DI_FORMAT_MODE1:        
        Read_CHD_Hunk_M1(buf, lba, ct);        
        encode_mode1_sector(lba + 150, buf);
        break;

      case DI_FORMAT_MODE1_RAW:   
      case DI_FORMAT_MODE2_RAW:
      case DI_FORMAT_CDI_RAW:          
        Read_CHD_Hunk_RAW(buf, lba, ct);           
        //Read_CHD_Hunk_RAW(buf, lba + lbaPCECD_offset);           
        break;

      case DI_FORMAT_MODE2:        
        Read_CHD_Hunk_M2(buf, lba, ct);        
        encode_mode2_sector(lba + 150, buf);
        break;

      // FIXME: M2F1, M2F2, does sub-header come before or after user data(standards say before, but I wonder
      // about cdrdao...).
      case DI_FORMAT_MODE2_FORM1:
        // ct->fp->read(buf + 24, 2048);
        //encode_mode2_form1_sector(lba + 150, buf);
        break;

      case DI_FORMAT_MODE2_FORM2:
        //ct->fp->read(buf + 24, 2324);
        //encode_mode2_form2_sector(lba + 150, buf);
        break;
      }

      //if(ct->SubchannelMode)
      //   ct->fp->read(buf + 2352, 96);
    }
  } // end if audible part of audio track read.

}

//
// Note: this function makes use of the current contents(as in |=) in SubPWBuf.
//
int32 CDAccess_CHD::MakeSubPQ(int32 lba, uint8 *SubPWBuf) const
{
  uint8 buf[0xC];
  int32 track;
  uint32 lba_relative;
  uint32 ma, sa, fa;
  uint32 m, s, f;
  uint8 pause_or = 0x00;
  bool track_found = false;

  for (track = FirstTrack; track < (FirstTrack + NumTracks); track++)
  {
    if (lba >= (Tracks[track].LBA - Tracks[track].pregap_dv - Tracks[track].pregap) && lba < (Tracks[track].LBA + Tracks[track].sectors + Tracks[track].postgap))
    {
      track_found = true;
      break;
    }
  }

  if (!track_found)
    throw(MDFN_Error(0, "Could not find track for sector %u!", lba));

  if (lba < Tracks[track].LBA)
    lba_relative = Tracks[track].LBA - 1 - lba;
  else
    lba_relative = lba - Tracks[track].LBA;

  f = (lba_relative % 75);
  s = ((lba_relative / 75) % 60);
  m = (lba_relative / 75 / 60);

  fa = (lba + 150) % 75;
  sa = ((lba + 150) / 75) % 60;
  ma = ((lba + 150) / 75 / 60);

  uint8 adr = 0x1; // Q channel data encodes position
  uint8 control = Tracks[track].subq_control;

  // Handle pause(D7 of interleaved subchannel byte) bit, should be set to 1 when in pregap or postgap.
  if ((lba < Tracks[track].LBA) || (lba >= Tracks[track].LBA + Tracks[track].sectors))
  {
    //printf("pause_or = 0x80 --- %d\n", lba);
    pause_or = 0x80;
  }

  // Handle pregap between audio->data track
  {
    int32 pg_offset = (int32)lba - Tracks[track].LBA;

    // If we're more than 2 seconds(150 sectors) from the real "start" of the track/INDEX 01, and the track is a data track,
    // and the preceding track is an audio track, encode it as audio(by taking the SubQ control field from the preceding track).
    //
    // TODO: Look into how we're supposed to handle subq control field in the four combinations of track types(data/audio).
    //
    if (pg_offset < -150)
    {
      if ((Tracks[track].subq_control & SUBQ_CTRLF_DATA) && (FirstTrack < track) && !(Tracks[track - 1].subq_control & SUBQ_CTRLF_DATA))
      {
        //printf("Pregap part 1 audio->data: lba=%d track_lba=%d\n", lba, Tracks[track].LBA);
        control = Tracks[track - 1].subq_control;
      }
    }
  }

  memset(buf, 0, 0xC);
  buf[0] = (adr << 0) | (control << 4);
  buf[1] = U8_to_BCD(track);

  // Index
  //if(lba < Tracks[track].LBA) // Index is 00 in pregap
  // buf[2] = U8_to_BCD(0x00);
  //else
  // buf[2] = U8_to_BCD(0x01);
  {
    int index = 0;

    for (int32 i = 0; i < 100; i++)
    {
      if (lba >= Tracks[track].index[i])
        index = i;
    }
    buf[2] = U8_to_BCD(index);
  }

  // Track relative MSF address
  buf[3] = U8_to_BCD(m);
  buf[4] = U8_to_BCD(s);
  buf[5] = U8_to_BCD(f);

  buf[6] = 0; // Zerroooo

  // Absolute MSF address
  buf[7] = U8_to_BCD(ma);
  buf[8] = U8_to_BCD(sa);
  buf[9] = U8_to_BCD(fa);

  subq_generate_checksum(buf);
  
  //SBI
  if(!SubQReplaceMap.empty())
  {
   //printf("%d\n", lba);
   auto it = SubQReplaceMap.find(LBA_to_ABA(lba));

   if(it != SubQReplaceMap.end())
   {
    //printf("Replace: %d\n", lba);
    memcpy(buf, it->second.data(), 12);
   }
  }

  for (int i = 0; i < 96; i++)
    SubPWBuf[i] |= (((buf[i >> 3] >> (7 - (i & 0x7))) & 1) ? 0x40 : 0x00) | pause_or;

  return track;
}

bool CDAccess_CHD::Fast_Read_Raw_PW_TSRE(uint8 *pwbuf, int32 lba) const noexcept
{
  int32 track;

  if (lba >= total_sectors)
  {
    subpw_synth_leadout_lba(tocd, lba, pwbuf);
    return (true);
  }

  memset(pwbuf, 0, 96);
  track = MakeSubPQ(lba, pwbuf);

  //
  // If TOC+BIN has embedded subchannel data, we can't fast-read(synthesize) it...
  //
  if (Tracks[track].SubchannelMode && lba >= (Tracks[track].LBA - Tracks[track].pregap_dv) && (lba < Tracks[track].LBA + Tracks[track].sectors))
    return (false);

  return (true);
}

void CDAccess_CHD::Read_TOC(CDUtility::TOC *toc)
{ 
  *toc = tocd; 
}

void CDAccess_CHD::LoadSBI(VirtualFS* vfs, const std::string& sbi_path)
{
 MDFN_printf(_("Loading SBI file %s...\n"), vfs->get_human_path(sbi_path).c_str()); 
 {
  MDFN_AutoIndent aind(1);

  try
  {
   std::unique_ptr<Stream> sbis(vfs->open(sbi_path, VirtualFS::MODE_READ));
   uint8 header[4];
   uint8 ed[4 + 10];
   uint8 tmpq[12];

   sbis->read(header, 4);

   if(memcmp(header, "SBI\0", 4))
    throw MDFN_Error(0, _("Not recognized a valid SBI file."));

   while(sbis->read(ed, sizeof(ed), false) == sizeof(ed))
   {
    if(!BCD_is_valid(ed[0]) || !BCD_is_valid(ed[1]) || !BCD_is_valid(ed[2]))
     throw MDFN_Error(0, _("Bad BCD MSF offset in SBI file: %02x:%02x:%02x"), ed[0], ed[1], ed[2]);

    if(ed[3] != 0x01)
     throw MDFN_Error(0, _("Unrecognized boogly oogly in SBI file: %02x"), ed[3]);

    memcpy(tmpq, &ed[4], 10);

    //
    subq_generate_checksum(tmpq);
    tmpq[10] ^= 0xFF;
    tmpq[11] ^= 0xFF;
    //

    //printf("%02x:%02x:%02x --- ", ed[0], ed[1], ed[2]);
    //for(unsigned i = 0; i < 12; i++)
    // printf("%02x ", tmpq[i]);
    //printf("\n");

    uint32 aba = AMSF_to_ABA(BCD_to_U8(ed[0]), BCD_to_U8(ed[1]), BCD_to_U8(ed[2]));

    memcpy(SubQReplaceMap[aba].data(), tmpq, 12);
   }
   MDFN_printf(_("Loaded Q subchannel replacements for %zu sectors.\n"), SubQReplaceMap.size());
  }
  catch(MDFN_Error &e)
  {
   if(e.GetErrno() != ENOENT)
    throw;
   else
    MDFN_printf(_("Error: %s\n"), e.what());
  }
  catch(std::exception &e)
  {
   throw;
  }
 }
}
/*
int CDAccess_CHD::isPCECD(int32 lba) 
{    
  if (!lba) 
   return 0;
   	
  int32 lbaPCECD;
  lbaPCECD = lba;
  CHDFILE_TRACK_INFO *ct;
  ct = &Tracks[1];
  uint8 buf[2352];	
  static const uint8 magic_test[0x20] = { 0x82, 0xB1, 0x82, 0xCC, 0x83, 0x76, 0x83, 0x8D, 0x83, 0x4F, 0x83, 0x89, 0x83, 0x80, 0x82, 0xCC,  
 				   	  0x92, 0x98, 0x8D, 0xEC, 0x8C, 0xA0, 0x82, 0xCD, 0x8A, 0x94, 0x8E, 0xAE, 0x89, 0xEF, 0x8E, 0xD0
				        };
  int i;
  i=lba + 3; //most common pcecd
  while (i > 0) {
    memset(buf, 0, 2352);	
    Read_CHD_Hunk_RAW(buf, i, ct);  
    if(!memcmp((char*)buf+16, (char *)magic_test, 0x20)) {    
      lbaPCECD = i;          
      break;
    }
    if(!strncmp("PC-FX:Hu_CD-ROM", (char*)buf+16, strlen("PC-FX:Hu_CD-ROM"))) {      
      lbaPCECD = i;                 	
      break;
    }
    if(!strncmp((char *)buf + 64 + 16, "PPPPHHHHOOOOTTTTOOOO____CCCCDDDD", 32)) {      
      lbaPCECD = i;                 	   	
      break;
    }
    i--;
  } 
  return (lbaPCECD - lba);  
   
}
*/
}