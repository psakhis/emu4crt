
#include "main.h"
#include "input.h"
#include "Joystick.h"
#include "Joystick_Mister.h"
#include <mednafen/hash/md5.h>

#include <mednafen/mister/groovymister_wrapper.h>

class Joystick_Mister : public Joystick
{
 public:

 Joystick_Mister(unsigned index) MDFN_COLD;
 ~Joystick_Mister() MDFN_COLD;

 void UpdateInternal(int joyn);

 virtual unsigned HatToButtonCompat(unsigned hat);

 private:
 unsigned mister_num_axes;
 unsigned mister_num_hats;
 unsigned mister_num_balls;
 unsigned mister_num_buttons;
};

unsigned Joystick_Mister::HatToButtonCompat(unsigned hat)
{
 return(9 + (hat * 4));	
}

Joystick_Mister::Joystick_Mister(unsigned index) 
{
 try
 {
  name = "MiSTer";

  mister_num_axes = 4;
  mister_num_balls = 0;
  mister_num_buttons = 10;
  mister_num_hats = 1;

  Calc09xID(mister_num_axes, mister_num_balls, mister_num_hats, mister_num_buttons);
  {
   md5_context h;
   uint8 d[16];

   h.starts();
   h.update((const uint8*)name.data(), name.size());
   h.finish(d);
   memcpy(&id[0], d, 8);

   MDFN_en16msb(&id[ 8], mister_num_axes);
   MDFN_en16msb(&id[10], mister_num_buttons);
   MDFN_en16msb(&id[12], mister_num_hats);
   MDFN_en16msb(&id[14], mister_num_balls);
  }
  num_axes = mister_num_axes;
  num_rel_axes = mister_num_balls * 2;
  num_buttons = mister_num_buttons + (mister_num_hats * 4);

  axis_state.resize(num_axes);
  rel_axis_state.resize(num_rel_axes);
  button_state.resize(num_buttons);
 }
 catch(...)
 {
  throw;
 }
}

Joystick_Mister::~Joystick_Mister()
{

}

void Joystick_Mister::UpdateInternal(int joyn)
{
 gmw_fpgaJoyInputs joyInputs;
 gmw_getJoyInputs(&joyInputs);
 int map = (joyn == 0) ? joyInputs.joy1 : joyInputs.joy2;
 //gmw_set_log_level(2);
 for(unsigned i = 0; i < mister_num_axes; i++)
 {
  if (i == 0)
  {
  	axis_state[i] = (joyn == 0) ? ((joyInputs.joy1LXAnalog << 8) + joyInputs.joy1LXAnalog) : ((joyInputs.joy2LXAnalog << 8) + joyInputs.joy2LXAnalog);
  }
  else if (i == 1)
  {
  	axis_state[i] = (joyn == 0) ? ((joyInputs.joy1LYAnalog << 8) + joyInputs.joy1LYAnalog) : ((joyInputs.joy2LYAnalog << 8) + joyInputs.joy2LYAnalog);
  }  
  else if (i == 2)
  {
  	axis_state[i] = (joyn == 0) ? ((joyInputs.joy1RXAnalog << 8) + joyInputs.joy1RXAnalog) : ((joyInputs.joy2RXAnalog << 8) + joyInputs.joy2RXAnalog);
  }  
  else
  {
  	axis_state[i] = (joyn == 0) ? ((joyInputs.joy1RYAnalog << 8) + joyInputs.joy1RYAnalog) : ((joyInputs.joy2RYAnalog << 8) + joyInputs.joy2RYAnalog);
  }  
  //Mednafen::MDFN_printf(_("AXIS %d %d...\n"),i,axis_state[i]);
  if(axis_state[i] < -32767)
   axis_state[i] = -32767;
  if(axis_state[i] > 32768)
   axis_state[i] = 32768; 
 }

 for(unsigned i = 0; i < mister_num_balls; i++)
 {
  int dx=0, dy=0;

  //SDL_JoystickGetBall(sdl_joy, i, &dx, &dy);

  rel_axis_state[i * 2 + 0] = dx;
  rel_axis_state[i * 2 + 1] = dy;
 }

 for(unsigned i = 0; i < mister_num_buttons; i++)
 {  	
   switch(i)
   {
   	case 0: button_state[0] = map & GMW_JOY_B1;
   	case 1: button_state[1] = map & GMW_JOY_B2;
   	case 2: button_state[2] = map & GMW_JOY_B3;
   	case 3: button_state[3] = map & GMW_JOY_B4;
   	case 4: button_state[4] = map & GMW_JOY_B5;
   	case 5: button_state[5] = map & GMW_JOY_B6;
   	case 6: button_state[6] = map & GMW_JOY_B7;
   	case 7: button_state[7] = map & GMW_JOY_B8;
   	case 8: button_state[8] = map & GMW_JOY_B9;   	
   	case 9: button_state[9] = map & GMW_JOY_B10;   	
   }	   
 }

 for(unsigned i = 0; i < mister_num_hats; i++)
 {    
  button_state[mister_num_buttons + (i * 4) + 0] = (bool)(map & GMW_JOY_UP);
  button_state[mister_num_buttons + (i * 4) + 1] = (bool)(map & GMW_JOY_RIGHT);
  button_state[mister_num_buttons + (i * 4) + 2] = (bool)(map & GMW_JOY_DOWN);
  button_state[mister_num_buttons + (i * 4) + 3] = (bool)(map & GMW_JOY_LEFT);
 }
}

class JoystickDriver_Mister : public JoystickDriver
{
 public:

 JoystickDriver_Mister();
 virtual ~JoystickDriver_Mister();

 virtual unsigned NumJoysticks();                       // Cached internally on JoystickDriver instantiation.
 virtual Joystick *GetJoystick(unsigned index);
 virtual void UpdateJoysticks(void);

 private:
 std::vector<Joystick_Mister *> joys;
};


JoystickDriver_Mister::JoystickDriver_Mister()
{  	
 gmw_bindInputs(MDFN_GetSettingS("mister.host").c_str());
 for(int n = 0; n < 2; n++)
 {
  try
  {
   Joystick_Mister *jmister = new Joystick_Mister(n);
   joys.push_back(jmister);
  }
  catch(std::exception &e)
  {
   MDFND_OutputNotice(MDFN_NOTICE_ERROR, e.what());
  }
 }
}

JoystickDriver_Mister::~JoystickDriver_Mister()
{
 for(unsigned int n = 0; n < joys.size(); n++)
 {
  delete joys[n];
 }

}

unsigned JoystickDriver_Mister::NumJoysticks(void)
{
 return joys.size();
}

Joystick *JoystickDriver_Mister::GetJoystick(unsigned index)
{
 return joys[index];
}

void JoystickDriver_Mister::UpdateJoysticks(void)
{ 
 gmw_pollInputs(); 
 for(unsigned int n = 0; n < joys.size(); n++)
 {
  joys[n]->UpdateInternal(n);
 }
}

JoystickDriver *JoystickDriver_Mister_New(void)
{
 return new JoystickDriver_Mister();
}
