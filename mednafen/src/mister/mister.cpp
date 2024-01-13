#include "mister.h"

MiSTer::MiSTer()
{
  sr_init(); 
  printf("sr_get_version %s\n", sr_get_version());  
}

MiSTer::~MiSTer()
{
  sr_deinit();   
}

void MiSTer::CmdClose(void)
{        
   char buffer[1];        
   buffer[0] = CMD_CLOSE;
   Send((char*)&buffer[0], 1);     		
}

void MiSTer::CmdInit(const char* mister_host, short mister_port, bool lz4_frames, uint32_t sound_rate, uint8_t sound_chan)
{ 
   char buffer[4];
   
#ifdef WIN32

   WSADATA wsd;                                           
   uint16_t rc;
   
   printf("[DEBUG] Initialising Winsock...\n");   	
   // Load Winsock
   rc = WSAStartup(MAKEWORD(2, 2), &wsd);
   if (rc != 0) 
   {
	printf("Unable to load Winsock: %d\n", rc);       
   }
   
   if (USE_OVERLAPPED)
   {       
   	sockfdOV = INVALID_SOCKET;  
	printf("[DEBUG] Initialising socket overlapped...\n");                
	sockfdOV = WSASocket(AF_INET, SOCK_DGRAM, IPPROTO_UDP, NULL, 0, WSA_FLAG_OVERLAPPED);
	          
	if (sockfdOV == INVALID_SOCKET)
	{
	  	printf("Could not create socket : %d", WSAGetLastError());        
	}    
	
	// Set server          	                       
	ServerAddr.sin_family = AF_INET;
	ServerAddr.sin_port = htons(mister_port);
	ServerAddr.sin_addr.s_addr = inet_addr(mister_host);        		                     
	
	sizeof_ServerAddr = sizeof(ServerAddr);
	
	// Make sure the Overlapped struct is zeroed out
	SecureZeroMemory((PVOID) &Overlapped, sizeof (WSAOVERLAPPED));       
	
	// Create a event handler setup the overlapped structure.
	Overlapped.hEvent = WSACreateEvent();              
	
	if (Overlapped.hEvent == NULL) 
	{
		printf("WSACreateEvent failed with error: %d\n", WSAGetLastError());              
	}   
	
	SecureZeroMemory((PVOID) &OverlappedRecv, sizeof (WSAOVERLAPPED));         
	OverlappedRecv.hEvent = WSACreateEvent();  
	
	if (OverlappedRecv.hEvent == NULL) 
	{
		printf("WSACreateEvent failed with error: %d\n", WSAGetLastError());              
	} 
	
	printf("[DEBUG] Setting send buffer to 2097152 bytes...\n");	    
	int optVal = 2097152;  
	int optLen = sizeof(int);
	rc = setsockopt(sockfdOV, SOL_SOCKET, SO_SNDBUF, (char*)&optVal, optLen);
	if (rc != 0) 
	{
		printf("Unable to set send buffer: %d\n", rc);       
	}   
	ACKComplete = 0;
     }
     else
     {
	printf("[DEBUG] Initialising socket...\n");  
  	sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  	if (sockfd < 0)
  	{
    		printf("socket error\n");       		
  	}  
  	
  	memset(&ServerAddr, 0, sizeof(ServerAddr));
  	ServerAddr.sin_family = AF_INET;
  	ServerAddr.sin_port = htons(mister_port);
  	ServerAddr.sin_addr.s_addr = inet_addr(mister_host);		
  	
  	printf("[DEBUG] Setting socket async...\n"); 
  	u_long iMode=1;
   	rc = ioctlsocket(sockfd, FIONBIO, &iMode); 
   	if (rc < 0)
   	{
   		printf("set nonblock fail\n");
   	}
   	
   	printf("[DEBUG] Setting send buffer to 2097152 bytes...\n");     
   	int optVal = 2097152;  
   	int optLen = sizeof(int);
   	rc = setsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, (char*)&optVal, optLen);
   	if (rc < 0)
   	{
   		printf("set so_sndbuff fail\n");
   	}
     }	
                
#else

  printf("[DEBUG] Initialising socket...\n");  
  sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);				
  if (sockfd < 0)
  {
    	printf("socket error\n");       		
  }        
  
  memset(&ServerAddr, 0, sizeof(ServerAddr));
  ServerAddr.sin_family = AF_INET;
  ServerAddr.sin_port = htons(mister_port);
  ServerAddr.sin_addr.s_addr = inet_addr(mister_host);
    
  printf("[DEBUG] Setting socket async...\n");  
  // Non blocking socket                                                                                                     
  int flags;
  flags = fcntl(sockfd, F_GETFD, 0);    	
  if (flags < 0) 
  {
  	printf("get falg error\n");       		
  }
  flags |= O_NONBLOCK;
  if (fcntl(sockfd, F_SETFL, flags) < 0) 
  {
  	printf("set nonblock fail\n");       		
  }   	
  
  printf("[DEBUG] Setting send buffer to 2097152 bytes...\n");    		     	    	        	 	    		    	    		
  // Settings	
  int size = 2 * 1024 * 1024;	
  if (setsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, (void*)&size, sizeof(size)) < 0)     
  {
  	printf("Error so_sndbuff\n");        	
  }  
         
#endif  
   
   printf("[DEBUG] Sending CMD_INIT...\n");
   
   buffer[0] = CMD_INIT;  
   buffer[1] = (lz4_frames) ? 1 : 0; //0-RAW or 1-LZ4 ;
   buffer[2] = (sound_rate == 22050) ? 1 : (sound_rate == 44100) ? 2 : (sound_rate == 48000) ? 3 : 0;  
   buffer[3] = sound_chan;         
   
   Send(&buffer[0], 4);     
   
   lz4_compress = lz4_frames;
   width = 0;	   	   
   height = 0;
   lines = 0;	  	   
   widthTime = 0;
   frameTime = 0;  	  
   avgEmulationTime = 0;    
   vsync_auto = 120; 
   blitTime = 0;   
   frameEcho = 0;
   vcountEcho = 0;
   frameGPU = 0;
   vcountGPU = 0;  
   interlaced = 0;
   fpga_audio = 0;
   firstField = false;
}

void MiSTer::CmdSwitchres(int w, int h, double vfreq, int orientation)
{
   //printf("  VIDEO - Video_SetSwitchres - called for %dx%d@%f (%d) \n",w,h,vfreq,orientation);     
  
   unsigned char retSR;
   sr_mode swres_result;
   int sr_mode_flags = 0; 
    
   if (h > 288) 
    sr_mode_flags = SR_MODE_INTERLACED;
 
   if (orientation)
    sr_mode_flags = sr_mode_flags | SR_MODE_ROTATED;  
   
   if (w == 341)
    sr_set_user_mode(w, h, 0); //pce fix 341x240 
   retSR = sr_add_mode(w, h, vfreq, sr_mode_flags, &swres_result);  
   
   printf("  VIDEO - Video_SetSwitchres - result %dx%d@%f - x=%.4f y=%.4f stretched(%d)\n", swres_result.width, swres_result.height,swres_result.vfreq, swres_result.x_scale, swres_result.y_scale, swres_result.is_stretched);		
   //printf("  VIDEO - Video_SetSwitchres - sr_switch_to_mode return: %u\n", (unsigned int)retSR);          
   
   if (retSR && swres_result.width >= w && swres_result.height >= h) 
   {   	     	
	   char buffer[27];   
	   
	   double px = double(swres_result.pclock) / 1000000.0;
	   uint16_t udp_hactive = swres_result.width;;			      
	   uint16_t udp_hbegin = swres_result.hbegin;
	   uint16_t udp_hend = swres_result.hend;
	   uint16_t udp_htotal = swres_result.htotal;
	   uint16_t udp_vactive = swres_result.height;
	   uint16_t udp_vbegin = swres_result.vbegin;
	   uint16_t udp_vend = swres_result.vend;
	   uint16_t udp_vtotal = swres_result.vtotal;
	   uint8_t  udp_interlace = swres_result.interlace;  
   
    	   width = udp_hactive;	   	   
	   height = udp_vactive;
	   lines = udp_vtotal;	  
	   interlaced = udp_interlace;
	   
	   if (interlaced)
	   {
	   	firstField = true;	   	  
	   } 
	   
	   widthTime = round((double) udp_htotal * (1 / px)); //in usec, time to raster 1 line
           frameTime = widthTime * udp_vtotal;
           
           if (interlaced)
           { 
           	frameTime = frameTime >> 1;
           }	
   
	   //printf("[DEBUG] Sending CMD_SWITCHRES...\n"); 
	   buffer[0] = CMD_SWITCHRES;             
	   memcpy(&buffer[1],&px,sizeof(px));
	   memcpy(&buffer[9],&udp_hactive,sizeof(udp_hactive));
	   memcpy(&buffer[11],&udp_hbegin,sizeof(udp_hbegin));
	   memcpy(&buffer[13],&udp_hend,sizeof(udp_hend));
	   memcpy(&buffer[15],&udp_htotal,sizeof(udp_htotal));
	   memcpy(&buffer[17],&udp_vactive,sizeof(udp_vactive));
	   memcpy(&buffer[19],&udp_vbegin,sizeof(udp_vbegin));
	   memcpy(&buffer[21],&udp_vend,sizeof(udp_vend));
	   memcpy(&buffer[23],&udp_vtotal,sizeof(udp_vtotal));
	   memcpy(&buffer[25],&udp_interlace,sizeof(udp_interlace));    
	   Send(&buffer[0], 26);  
   }
   
     
}

void MiSTer::CmdBlit(char *bufferFrame, uint16_t vsync)
{          	 	     
   char buffer[9];   
   
   frame++;
   
   // 16 or 32 lines blockSize
   uint8_t blockLinesFactor = (width > 384) ? 5	: 4;
   
   // Compressed blocks are 16/32 lines long
   uint32_t blockSize = (lz4_compress) ? (width << blockLinesFactor) * 3 : 0;     
   
   if (blockSize > MAX_LZ4_BLOCK)
     blockSize = MAX_LZ4_BLOCK;
   
   // Manual vsync
   if (vsync != 0)
   {
   	vsync_auto = vsync;     
   }
         
   buffer[0] = CMD_BLIT_VSYNC;
   memcpy(&buffer[1], &frame, sizeof(frame));    
   memcpy(&buffer[5], &vsync_auto, sizeof(vsync_auto));      
   buffer[7] = (uint16_t) blockSize & 0xff;
   buffer[8] = (uint16_t) blockSize >> 8;       
                 	   	    
   Send(&buffer[0], 9);               
   
   uint32_t bufferSize = (interlaced == 0) ? width * height * 3 : width * (height >> 1) * 3;
                    	   
   if (lz4_compress == false)
    SendMTU(&bufferFrame[0], bufferSize, 1470);
   else
    SendLZ4(&bufferFrame[0], bufferSize, blockSize); 	       
       
}

void MiSTer::CmdAudio(const void *bufferFrame, uint32_t sizeSound, uint8_t soundchan)
{
   if (fpga_audio == 0)
    return;
    	
   char buffer[3];      
   buffer[0] = CMD_AUDIO;
     
   uint16_t bytesSound = sizeSound * soundchan * 2;
        
   memcpy(&buffer[1], &bytesSound, sizeof(bytesSound));      
                         	   	    
   Send(&buffer[0], 3);                   
   const uint8_t *data_in = (const uint8_t *)bufferFrame;         
   
   SendMTU((char *) &data_in[0], bytesSound, 1472); 
 	              	
}

void MiSTer::SetStartEmulate(void)
{  	        
#ifdef WIN32	 
  QueryPerformanceCounter(&tickStartEmulate); 
#else  
  clock_gettime(CLOCK_MONOTONIC, &tickStartEmulate);   
#endif   
}

void MiSTer::SetEndEmulate(void)
{	
#ifdef WIN32
  QueryPerformanceCounter(&tickEndEmulate);   
  emulationTime = (tickEndEmulate.QuadPart - tickStartEmulate.QuadPart) / 10;  
#else
  clock_gettime(CLOCK_MONOTONIC, &tickEndEmulate);   
  emulationTime = DiffTimespec(tickStartEmulate, tickEndEmulate) / 10;
#endif      

  if (frame > 10) //first frames spends more time as usual
  {
   	avgEmulationTime = (avgEmulationTime == 0) ? emulationTime + blitTime: (avgEmulationTime + emulationTime + blitTime) / 2;
   	vsync_auto = height - round(lines * avgEmulationTime) / frameTime; //vblank for desviation  
   	if (vsync_auto > 480) vsync_auto = 1;	   	  	   	    
  } 
  else
  {
  	avgEmulationTime = 0;
  	vsync_auto = 120;     
  }
}

void MiSTer::SetStartBlit(void)
{  	        
#ifdef WIN32	 
  QueryPerformanceCounter(&tickStartBlit); 
#else  
  clock_gettime(CLOCK_MONOTONIC, &tickStartBlit);   
#endif   
}

void MiSTer::SetEndBlit(void)
{  	        
#ifdef WIN32	 
  QueryPerformanceCounter(&tickEndBlit); 
  blitTime = (tickEndBlit.QuadPart - tickStartBlit.QuadPart) / 10;  
#else  
  clock_gettime(CLOCK_MONOTONIC, &tickEndBlit);   
  blitTime = DiffTimespec(tickStartBlit, tickEndBlit) / 10;
#endif   
}


void MiSTer::Sync()
{  	
  int prevSleepTime = 0;  
  int sleepTime = 0;
  int realSleepTime = 0;
  int elapsedTime = 0;  
  int diffTime = GetVSyncDif();  //adjusting time with raster
  
#ifdef WIN32
  LARGE_INTEGER tick;      
  LARGE_INTEGER tick2;    
  
  QueryPerformanceCounter(&tick);
  elapsedTime = (frame == 1) ? (frameTime / 2) : (tick.QuadPart - tickLastSync.QuadPart) / 10;       
#else
  timespec tick;   
  timespec tick2; 
  
  clock_gettime(CLOCK_MONOTONIC, &tick);
  elapsedTime = (frame == 1) ? (frameTime / 2) : DiffTimespec(tickLastSync, tick) / 10;       
#endif  
         	    	    
  sleepTime = (frameTime - elapsedTime + diffTime) * 10; //nano
  prevSleepTime = sleepTime;         
  tick2 = tick; //while
  
  if (sleepTime > 0)
  {  		  	  	  	 	  		  	  	
  	do
  	{  	  		  		  		
  		if (frame != frameEcho)
  		{  	
  			{
  				diffTime = GetVSyncDif(); 
  				sleepTime = sleepTime + (diffTime * 10);  				  				
  			}   					  		
  		}
  		
  		#ifdef WIN32  		  		  		
  		 QueryPerformanceCounter(&tick2);	
  		 realSleepTime = (tick2.QuadPart - tick.QuadPart);
  		#else
  		 clock_gettime(CLOCK_MONOTONIC, &tick2);
  		 realSleepTime = DiffTimespec(tick, tick2);   
  		#endif 
  		  		
  	} while (realSleepTime < sleepTime); 	
  }       		   
    
  tickLastSync = tick2;     
  
 // if (sleepTime < 0 || (sleepTime + 10000 < realSleepTime)) //something it's wrong
   printf("Frame %d Sleep prev=%d/final=%d/real=%d (frameTime=%d ellapsed=%d blitTime=%d difGPU=%d emulationTime=%d) vsync=%d/%d\n", frame, prevSleepTime, sleepTime, realSleepTime, frameTime, elapsedTime, blitTime, diffTime, avgEmulationTime, vsync_auto, vcountGPU);	      

}

int MiSTer::GetVSyncDif(void)
{
  uint32_t prevFrameEcho = frameEcho;	    
  int diffTime = 0;  
  
  if (frame != frameEcho) //some ack is pending
  {
  	ReceiveBlitACK();   
  }
         
  if (prevFrameEcho != frameEcho) //if ack is updated, check raster difference
  {	  	
  	//horror patch if emulator freezes to align frame counter
  	if ((frameEcho + 1) < frameGPU) 
  	{
  	 	frame = frameGPU + 1;
  	}
  	
   	uint32_t vcount1 = ((frameEcho - 1) * lines + vcountEcho) >> interlaced;
	uint32_t vcount2 = (frameGPU * lines + vcountGPU) >> interlaced;
	int dif = (int) (vcount1 - vcount2) / 2;	//dicotomic
	  	 
	diffTime = (int) (widthTime * dif);  	
	
	//printf("echo %d %d / %d %d \n", frameEcho, vcountEcho, frameGPU, vcountGPU);	        	  	  	  	  		
  }
  
  return diffTime;     	
}

int MiSTer::GetField(void)
{	
	int field = 0;
	if (interlaced)
	{
		if (firstField)
		{
			field = (width > 500) ? 1 : 0; //psx dif mega??
		}
		else if (vcountGPU % 2 != 0)
		{
			field = (width > 500) ? 0 : 1;		
		}	
	}
	firstField = false;	   	  
	return field;
}

//Private
void MiSTer::Send(void *cmd, int cmdSize)
{
#ifdef WIN32  
   if (USE_OVERLAPPED)
   {
	   int err = 0;
	   uint16_t rc;	
	   DWORD SendBytes;     
	   DWORD Flags = 0;
	   
	   DataBuf.len = cmdSize;
	   DataBuf.buf = (char *) cmd;
	       
	   rc = WSASendTo(sockfdOV, &DataBuf, 1, &SendBytes, 0, (SOCKADDR*) &ServerAddr, sizeof(ServerAddr), &Overlapped, NULL);
	   if (rc != 0)
	   {
		   if ((rc == SOCKET_ERROR) && (WSA_IO_PENDING != (err = WSAGetLastError()))) 
		   {
		   	printf("WSASend failed with error: %d\n", err);   	
		   }
		   
		   rc = WSAWaitForMultipleEvents(1, &Overlapped.hEvent, TRUE, WSA_INFINITE, TRUE);
		   if (rc == WSA_WAIT_FAILED) 
		   {
		        printf("WSAWaitForMultipleEvents failed with error: %d\n", WSAGetLastError());      
		   }
		   
		   rc = WSAGetOverlappedResult(sockfd, &Overlapped, &SendBytes, FALSE, &Flags);	
		   if (rc == FALSE) 
		   {
		        printf("WSAGetOverlapped failed with error: %d\n", WSAGetLastError());      
		   }
		             
		   WSAResetEvent(Overlapped.hEvent);
	   }
	   
	   return;	
  }

#endif
 
  sendto(sockfd, (char *) cmd, cmdSize, 0, (struct sockaddr *)&ServerAddr, sizeof(ServerAddr));	   
    
}


void MiSTer::SendMTU(char *buffer, int bytes_to_send, int chunk_max_size)
{
   int bytes_this_chunk = 0;
   int chunk_size = 0;
   uint32_t offset = 0;
	
   do
   {
	chunk_size = bytes_to_send > chunk_max_size? chunk_max_size : bytes_to_send;
	bytes_to_send -= chunk_size;
	bytes_this_chunk = chunk_size;

	Send(buffer + offset, bytes_this_chunk);
	offset += chunk_size;

   } while (bytes_to_send > 0);
}

void MiSTer::SendLZ4(char *buffer, int bytes_to_send, int block_size)
{
   LZ4_stream_t lz4_stream_body;
   LZ4_stream_t* lz4_stream = &lz4_stream_body;   
   LZ4_initStream(lz4_stream, sizeof(*lz4_stream));   
/*
   LZ4_streamHC_t lz4_streamHC_body;
   LZ4_streamHC_t* lz4_streamHC = &lz4_streamHC_body;   
   LZ4_initStreamHC(lz4_streamHC, sizeof(*lz4_streamHC));   
*/
   int inp_buf_index = 0;
   int bytes_this_chunk = 0;
   int chunk_size = 0;
   uint32_t offset = 0;
     
   do
   {
	chunk_size = bytes_to_send > block_size? block_size : bytes_to_send;
	bytes_to_send -= chunk_size;
	bytes_this_chunk = chunk_size;

	char* const inp_ptr = inp_buf[inp_buf_index];
	memcpy((char *)&inp_ptr[0], buffer + offset, chunk_size);
		 
	const uint16_t c_size = LZ4_compress_fast_continue(lz4_stream, inp_ptr, (char *)&m_fb_compressed[2], bytes_this_chunk, sizeof(m_fb_compressed), 1);
	//const uint16_t c_size = LZ4_compress_HC_continue(lz4_streamHC, inp_ptr, (char *)&m_fb_compressed[2], bytes_this_chunk, sizeof(m_fb_compressed));	
	uint16_t *c_size_ptr = (uint16_t *)&m_fb_compressed[0];
	*c_size_ptr = c_size;

	SendMTU((char *) &m_fb_compressed[0], c_size + 2, 1472);
	offset += chunk_size;
	inp_buf_index ^= 1;

   } while (bytes_to_send > 0);
      
}

void MiSTer::ReceiveBlitACK(void)
{	   
   uint32_t frameUDP = frameEcho;	
      	
#ifdef WIN32  
   if (USE_OVERLAPPED)
   {   	   
	   int err = 0;
	   uint16_t rc = 0;       	     	 		 	
	   DWORD Flags = 0;   
	   BytesRecv = 0;
	                    	              	                   	                                         	  
	   do
	   {
	   	if (ACKComplete == 0)
	   	{				     
		   	DataBufRecv.len = 13;
		   	DataBufRecv.buf = bufferRecv;   		   		   				
				       
		   	rc = WSARecvFrom(sockfdOV, &DataBufRecv, 1, &BytesRecv, &Flags, (SOCKADDR *) &ServerAddr, &sizeof_ServerAddr, &OverlappedRecv, NULL);        
		   	if (rc != 0)
		   	{
				if ((rc == SOCKET_ERROR) && (WSA_IO_PENDING != (err = WSAGetLastError()))) 
		   		{
		   			printf("WSARecvFrom failed with error: %d\n", err);   	
		   		}      
		   	}   	
		   	ACKComplete = 1;			   	
	   	}
	   	
	   	if (ACKComplete == 1)
	   	{	   		  
	   		rc = WSAWaitForMultipleEvents(1, &OverlappedRecv.hEvent, FALSE, 0, FALSE);  		
	
   			if (rc == WSA_WAIT_FAILED) 	
   			{
	        		printf("WSAWaitForMultipleEvents failed with error: %d\n", WSAGetLastError());      
   			}	    			
   			if (rc != WSA_WAIT_TIMEOUT) 
   			{
				rc = WSAGetOverlappedResult(sockfdOV, &OverlappedRecv, &BytesRecv, FALSE, &Flags);	 		
				if (rc == FALSE) 
				{
		        		printf("WSAGetOverlapped failed with error: %d\n", WSAGetLastError());      
				}
				else
				{
					ACKComplete = 0;
					memcpy(&frameUDP, &bufferRecv[0],4);					
					if (frameUDP > frameEcho)
  					{
  						frameEcho = frameUDP;
  						memcpy(&vcountEcho, &bufferRecv[4],2);
						memcpy(&frameGPU, &bufferRecv[6],4);
						memcpy(&vcountGPU, &bufferRecv[10],2);  
						memcpy(&fpga_debug_bits, &bufferRecv[12],1); 
			
						bitByte bits;  
						bits.byte = fpga_debug_bits;
						fpga_vram_ready     = bits.u.bit0;
						fpga_vram_end_frame = bits.u.bit1;
						fpga_vram_synced    = bits.u.bit2;   
						fpga_vga_frameskip  = bits.u.bit3;   
						fpga_vga_vblank     = bits.u.bit4;   		
						fpga_vga_f1         = bits.u.bit5;   
						fpga_audio          = bits.u.bit6;
			 			fpga_vram_queue     = bits.u.bit7;		  	
						//printf("ReceiveBlitACK %d %d / %d %d \n", frameEcho, vcountEcho, frameGPU, vcountGPU);
					}	
  				 				
					WSAResetEvent(OverlappedRecv.hEvent);								
				}			        		        		       		    		   
   			} 	    		   	
	   	}
	   	
	   } while (BytesRecv > 0 && frame != frameEcho);
	   	   	   
	   return;
    }   
#endif    	         
  
  socklen_t sServerAddr = sizeof(struct sockaddr);  
  int len = 0; 
  do
  {
  	len = recvfrom(sockfd, bufferRecv, sizeof(bufferRecv), 0, (struct sockaddr *)&ServerAddr, &sServerAddr);
  	if (len > 0)
  	{
  		memcpy(&frameUDP, &bufferRecv[0],4);	
  		if (frameUDP > frameEcho)
  		{
  			frameEcho = frameUDP;
  			memcpy(&vcountEcho, &bufferRecv[4],2);
			memcpy(&frameGPU, &bufferRecv[6],4);
			memcpy(&vcountGPU, &bufferRecv[10],2);  		  	
			memcpy(&fpga_debug_bits, &bufferRecv[12],1); 
			
			bitByte bits;  
			bits.byte = fpga_debug_bits;
			fpga_vram_ready     = bits.u.bit0;
			fpga_vram_end_frame = bits.u.bit1;
			fpga_vram_synced    = bits.u.bit2;   
			fpga_vga_frameskip  = bits.u.bit3;   
			fpga_vga_vblank     = bits.u.bit4;   		
			fpga_vga_f1         = bits.u.bit5;   
			fpga_audio          = bits.u.bit6;
 			fpga_vram_queue     = bits.u.bit7;
		 		  	
			//printf("ReceiveBlitACK %d %d / %d %d / bits(%d%d%d%d%d%d%d%d)\n", frameEcho, vcountEcho, frameGPU, vcountGPU, fpga_vram_ready, fpga_vram_end_frame, fpga_vram_synced, fpga_vga_frameskip, fpga_vga_vblank, fpga_vga_f1, fpga_vram_pixels, fpga_vram_queue);
  		} 
  	}  		  	
  } while (len > 0 && frame != frameEcho);
  
}
         

#ifndef WIN32
uint32_t MiSTer::DiffTimespec(timespec start, timespec end)
{
	uint32_t diffTime = 0;
        timespec temp;
        if ((end.tv_nsec - start.tv_nsec) < 0) 
        {
                temp.tv_sec = end.tv_sec - start.tv_sec - 1;
                temp.tv_nsec = 1000000000 + end.tv_nsec - start.tv_nsec;
        } 
        else 
        {
                temp.tv_sec = end.tv_sec - start.tv_sec;
                temp.tv_nsec = end.tv_nsec - start.tv_nsec;
        }
        diffTime = (temp.tv_sec * 1000000000) + temp.tv_nsec;
        return diffTime / 100;
}
#endif