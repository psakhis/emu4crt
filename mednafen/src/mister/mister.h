#include <inttypes.h>

class MiSTer
{
 public:
 MiSTer();
 ~MiSTer(); 
 
 char* getPBufferBlit(void);    
 char* getPBufferAudio(void);    
 void Close(void);
 void Init(const char* mister_host, short mister_port, uint8_t lz4_frames, uint32_t sound_rate, uint8_t sound_chan);
 void Switchres(int w, int h, double vfreq, int orientation);
 void Blit(uint16_t vsync); 
 void Audio(uint16_t soundSize);
 void Sync(void); 
 
 int  getField(void);
 bool isInterlaced(void);
 bool is480p(void);
 bool isDownscaled(void);
 
 private:
  
 uint8_t  lz4_compress = 0;
 uint32_t frame = 0;
 uint8_t  frameField = 0;
 uint16_t width_core;
 uint16_t height_core;
 double   vfreq_core;
 
 uint8_t  interlaced = 0;
 uint8_t  buffer_prog = 0;
 uint8_t  downscaled = 0;                      
 uint8_t  is480 = 0;                      
};

