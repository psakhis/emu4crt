#include <stdio.h>
#include "mister.h"

#ifdef _WIN32 
 #define SR_WIN32_STATIC 
#endif 
 
#include <mednafen/switchres/switchres_wrapper.h>
#include "groovymister_wrapper.h"
    
MiSTer::MiSTer()
{
   sr_init();       
}

MiSTer::~MiSTer()
{
   sr_deinit();   
}

char* MiSTer::getPBufferBlit(void)
{
   return gmw_get_pBufferBlit();     
}

char* MiSTer::getPBufferAudio(void)
{
   return gmw_get_pBufferAudio();     	
}

void MiSTer::Close(void)
{   	 
   gmw_close();
}

void MiSTer::Init(const char* mister_host, short mister_port, uint8_t lz4_frames, uint32_t sound_rate, uint8_t sound_chan)
{   	
   gmw_init(mister_host, mister_port, lz4_frames, sound_rate, sound_chan, 0);

   lz4_compress = lz4_frames;
   width_core = 0;	   	   
   height_core = 0;
   vfreq_core = 0; 
   interlaced = 0;
   buffer_prog = 1; 
   downscaled = 0;  
   is480 = 0;      
}

void MiSTer::Switchres(int w, int h, double vfreq, int orientation)
{
   //printf("  VIDEO - Video_SetSwitchres - called for %dx%d@%f (%d) \n",w,h,vfreq,orientation);     
  
   if (w < 200 || h < 160)
     return;
     
   if (w == width_core && h == height_core && vfreq == vfreq_core)   
     return;     
   
   width_core = w;	   	   
   height_core = h;	   
   vfreq_core = vfreq;	
   
   unsigned char retSR;  
   sr_mode swres_result;  
   int sr_mode_flags = 0; 
    
   if (h > 288) 
    sr_mode_flags = SR_MODE_INTERLACED;
 
   if (orientation)
    sr_mode_flags = sr_mode_flags | SR_MODE_ROTATED;  
   
   printf("[DEBUG] Video_SetSwitchres - (in %dx%d@%f)\n", w, h, vfreq);		   
              
   retSR = sr_add_mode(w, h, vfreq, sr_mode_flags, &swres_result);  
              
   if (retSR) 
   {   	     	
   	   printf("  VIDEO - Video_SetSwitchres - result %dx%d@%f - x=%.4f y=%.4f stretched(%d)\n", swres_result.width, swres_result.height,swres_result.vfreq, swres_result.x_scale, swres_result.y_scale, swres_result.is_stretched);		      	   	  
	   
	   double px = double(swres_result.pclock) / 1000000.0;
	   uint16_t hactive = swres_result.width;			      
	   uint16_t hbegin = swres_result.hbegin;
	   uint16_t hend = swres_result.hend;
	   uint16_t htotal = swres_result.htotal;
	   uint16_t vactive = swres_result.height;
	   uint16_t vbegin = swres_result.vbegin;
	   uint16_t vend = swres_result.vend;
	   uint16_t vtotal = swres_result.vtotal;
	   uint8_t  interlace = swres_result.interlace;  
       
	   interlaced = interlace;	   
           frameField = 0;        
           downscaled = (h > vactive) ? 1 : 0;	   	      	
           is480 = (!interlaced && (vactive > 288 || h == vactive >> 1)) ? 1 : 0;  
   	      		   
	   buffer_prog = (interlaced && !lz4_compress) ? 0 : 1;	   	 
	   gmw_switchres(px, hactive, hbegin, hend, htotal, vactive, vbegin, vend, vtotal, (interlaced && buffer_prog) ? 2 : interlace);
   }   
     
}

void MiSTer::Blit(uint16_t vsync)
{    
   frame++;      
   gmw_fpgaStatus status;
   gmw_refreshStatus(&status);
   if (status.frame > frame) frame = status.frame;  
   gmw_blit(frame, vsync, 0);
}


void MiSTer::Audio(uint16_t soundSize)
{
   gmw_audio(soundSize);	
}


void MiSTer::Sync()
{  	
   gmw_waitSync();
}


int MiSTer::getField(void)
{	
   int field = 0;
   if (!buffer_prog)
   {
   	gmw_fpgaStatus status;
        gmw_refreshStatus(&status);
        frameField = (status.frame > frame) ? status.vgaF1 : !frameField;         
	field = frameField;		
   }
  
   return field;
}

bool MiSTer::isInterlaced(void)
{
   return (!buffer_prog);
}

bool MiSTer::is480p(void)
{
   return is480;
}	

bool MiSTer::isDownscaled(void)
{
   return downscaled;
}




