#pragma GCC optimize ("O3")

/*
 * Taito do Brasil Arcade Bootleg v1.2
 * Nino MegaDriver - nino@nino.com.br
 * License GPL v3.0
 * 
 * Maincode for the ESP32 Devkit V1 module
 * Code based on the "FANTASTIC" arcade board
 * 
 * Libraries and Helpers used:
 * 
 * FabGL (http://www.fabglib.org/) for video generation
 * DacAudio by https://www.xtronical.com/
 * Z80 emulator code derived from Lin Ke-Fong
 * Graphics routines derived from YAAME by Tom Walker (http://yaame.emuunlim.com/)
 * +Tons of references and info taken from MAME sources
 * 
 */

// TIMER Helpers - used in Serial Tasks
#include "soc/timer_group_struct.h"
#include "soc/timer_group_reg.h"

// Usually sprite priority would not be needed as long as we
// clear the screen prior to start drawing on it. However we're not
// fast enought to fill the whole screen each frame, nor do we
// have enough RAM to use double buffering. So to avoid
// blinking sprites, we need to implement a priority system so
// not to draw the same pixel twice.
//
// Althought a boolean variable suggests that only one bit is
// necessary to store it, it is surprisingly stored as one whole byte!
//
// Then, if you think of a 256x256 pixels wide screen, it would
// take 65kb of RAM to store it into a variable and we obviously can't
// afford that... 
//
// However, if we treat each pixel as a single bit instead
// of one byte, the amount of RAM would go down to just 8k!
// 

byte objdirty[8192]; // 8192 bytes = 256*256/8, more then enought for us...
uint8_t busy = 0; // Semaphore for Video Drawing

// Core Configuration
int cpuCore  = 1; // Core # for CPU
int gfxCore  = 0; // Core # for GPU

// Bitwise heler functions defines
#define BITS_PER_BYTE (8)
#define GET_BIT(x, n) ((((x)[(n) / BITS_PER_BYTE]) & (0x1 << ((n) % BITS_PER_BYTE))) != 0)
#define SET_BIT(x, n) ((x)[(n) / BITS_PER_BYTE]) |= (0x1 << ((n) % BITS_PER_BYTE))
#define RESET_BIT(x, n) ((x)[(n) / BITS_PER_BYTE]) &= ~(0x1 << ((n) % BITS_PER_BYTE))

// Set a pixel at given position dirty
void set_dirty(int x, int y){
  int pixel = x * 256 + y;
  SET_BIT(objdirty,pixel);
}

// Reset dirty state of a pixel at given position
void reset_dirty(int x, int y){
  int pixel = x * 256 + y;
  RESET_BIT(objdirty,pixel);
}


// Return if pixel at given position is dirty
int get_dirty(int x, int y){
  int pixel = x * 256 + y;
  return GET_BIT(objdirty,pixel);
}

// Basic structure for transfering AY-3-8910
// register values over Serial
struct YMREG{
  uint8_t chip;
  uint8_t reg;
  uint8_t data;
  bool dirty = false;
};
#define YMBUFFSIZE 200                            // AY-3-8910 serial buffer transfer value
YMREG YMBUFF[YMBUFFSIZE];                         // AY-3-8910 buffer
uint8_t ym_flush_pos = 0;                         // AY-3-8910 flush position
uint8_t ym_write_pos = 0;                         // AY-3-8910 write position

#include "fabgl.h"                                // Main FabGL include
#include "machine.h"                              // Z80 Machine object
#include "res.hpp"

uint8_t player1_ctl = 0x00;                       // Byte containing the players buffer
uint8_t player2_ctl = 0x00;                       // One bit for each input

int GAME = 7;                                     // What game are we running today? See numbering bellow...


// INPUT BITS for each game supported
// Currently only Fantastic and Kong/Gorila are fully playable
//
// Game                            [Fantastic], [Kong], [Time Fighters], [Super Star Crest], [Zig Zag], [Galaxian Taito], [Munch Man], [Galaxy Empire]
// Index                                0         1            2                3                 4            5               6              7
uint8_t IPT_COIN1[] =           {     0x02,     0x02,        0x02,             0x01,            0x01,         0x02,           0x02,         0x02,  };
uint8_t IPT_JOYSTICK_LEFT[] =   {     0x04,     0x04,        0x04,             0x04,            0x04,         0x04,           0x04,         0x02,  };
uint8_t IPT_JOYSTICK_RIGHT[] =  {     0x08,     0x08,        0x08,             0x08,            0x08,         0x08,           0x08,         0x02,  };
uint8_t IPT_JOYSTICK_UP[] =     {     0x07,     0x80,        0x80,             0x07,            0x20,         0x07,           0x40,         0x02,  };
uint8_t IPT_JOYSTICK_DOWN[] =   {     0x07,     0x20,        0x20,             0x07,            0x40,         0x07,           0x80,         0x02,  };
uint8_t IPT_BUTTON1[] =         {     0x10,     0x07,        0x10,             0x10,            0x10,         0x10,           0x10,         0x02,  };
uint8_t IPT_START1[] =          {     0x01,     0x01,        0x01,             0x01,            0x01,         0x01,           0x01,         0x02,  };
uint8_t IPT_START2[] =          {     0x02,     0x02,        0x02,             0x02,            0x02,         0x02,           0x02,         0x02,  };

// Frame Duration time.
// Tune in if you want more fps
uint64_t frame_time[] =         {     14000,     140000,       14000,            14000,           14000,         14000,        14000,       14000   };

int nmi = 0;                                      // MNI Helper, 1 when MNI is pending

fabgl::VGAController  DisplayController;          // Main Video Object
fabgl::Canvas cv(&DisplayController);

Machine              fantastc;                    // Main CPU machine object

#include "XT_DAC_Audio.h"                         // DacAudio main include
XT_DAC_Audio_Class DacAudio(25,0);                // Main Class, using GPIO25 as PCM generator
int voice_playing[] = {0, 0, 0, 0, 0, 0 , 0};     // Helper for controlling voice samples
long voice_latch = 0;                             // Latch for voice samples

int screen_width   = 0;                           // Main output screen width, overwrited on setup();
int screen_height  = 0;                           // Main output screen height, overwrited on setup();

bool wireframe     = false;                       // Draw wireframe to display screen bounds?

uint8_t pixel[32];                                // R/W array of "pixels with defined color", used as the color palette
#include "fantastc.hpp"                           // "FANTASTIC" game includes
#include "kong.hpp"                               // "GORILA" (a.k.a "kong") game includes
#include "timefgtr.hpp"                           // "TIME FIGHTER" game includes
#include "sstarcrs.hpp"                           // "SUPER STAR CREST" game includes
#include "zigzag.hpp"                             // "ZIG ZAG" game includes
#include "samples.hpp"                            // PCM samples used by "GORILA"
#include "munch.hpp"                              // "Munch Man" game includes
#include "galaxiant.hpp"                          // "Galaxian Title" game includes"
#include "galemp.hpp"

// Menuing Vars
Bitmap *menu_bitmaps[] = { &menu_fantastc, &menu_kong};                 // Bitmaps for the selection page for each game
XT_Wav_Class *menu_samples[] = {&wav_menu_fantastic, &wav_menu_gorila}; // Samples for each menu page
uint8_t menu_active = 0;                                                // Menu active?
int menu_item = GAME;                                                   // Default menu item pointing to default GAME
uint8_t menu_item_last = -1;                                            // Last selected item
uint8_t menu_item_max = 1;                                              // Max index for the menu items (Currently only Fantastic and Kong are playable


// Direct sets a pixel on the output screen buffer
bool setpixel(int x=0, int y=0, int idx=0 , int force = 0){

    if(get_dirty(x, y) && force == 0) {
      reset_dirty(x, y);
      return false; // If pixel is dirty/already drawn, skip it!
    }

    if(idx>31) idx=0;
    int dx = screen_width-y-1;
    int dy = x;

    dx-=32;
    dy-=16;
    if(dx<32 || dx > 288 || dy<3 || dy > screen_height-2) return false;

    // Let's fix the orientation again to match original hardware
    dx = screen_width  - dx;
    dy = screen_height - dy;
    

    if(dx < screen_width && dy <screen_height && dx>= 0 && dy >= 0){
        DisplayController.setRawPixel(dx,dy,pixel[idx]);
        return true;
    }else{
       return false;
    }
}

// Very ugly but somehow needled.
// Using pointers to complex arrays panics or slows the esp32 core
int tileval(int c, int x, int y){
  if(GAME == 0) return fantastc_tiles[c][x][y];
  if(GAME == 1) return kong_tiles[c][x][y];
  if(GAME == 2) return timefgtr_tiles[c][x][y];
  if(GAME == 3) return crest_tiles[c][x][y];
  if(GAME == 4) return zigzag_tiles[c][x][y];
  if(GAME == 5) return galaxiant_tiles[c][x][y];
  if(GAME == 6) return munch_tiles[c][x][y];
  if(GAME == 7) return galemp_tiles[c][x][y];
  return 0;
}

// Helper, prints a tile on screen
// Using for debugging
void tilep(int t, int xx, int yy, int tint=0, int xflip=0, int yflip=0, int bits=0){
  int idx;
  for(int x=0;x<8;x++){
    for(int y=0;y<8;y++){
      idx =  tileval(t,x,y);
      setpixel(x+xx, y+yy, idx);
    }
  }
}

// Helper, prints all tiles on screen
// Using for debugging
void dumptiles(){
  int xx = 0;
  int yy = 0;
  for(int c=0; c<512; c++)
  {
    tilep(c, xx, yy);
    xx+=10;
    if(xx>256){
      xx = 0;
      yy+=10;
      if(yy>256){
        yy = 0;
      }
    }
  }  
}


// Code derived from YAAME by Tom Walker (http://yaame.emuunlim.com/)
// Draws a decoded tile on screen
void drawtilenew(int tile, int x, int y, int col, int xflip, int yflip, int bits)
{

        int xx,yy, idx;
        if (!yflip)
        {
               if (!xflip)
               {
                        for (yy=0;yy<8;yy++)
                            for (xx=0;xx<8;xx++){
                                  idx = tileval(tile,xx,yy)|(col<<bits);
                                  setpixel(x+xx, y+yy, idx);
                            }
                                
               }
               else
               {
                        for (yy=0;yy<8;yy++)
                            for (xx=0;xx<8;xx++){
                                  idx = tileval(tile,7-xx,yy)|(col<<bits);
                                  setpixel(x+xx, y+yy, idx);
                            }
               
               }
        }
        else
        {
               if (!xflip)
               {
                        for (yy=0;yy<8;yy++)
                            for (xx=0;xx<8;xx++){
                                  idx = tileval(tile,xx,7-yy)|(col<<bits);
                                  setpixel(x+xx, y+yy, idx);
                            }
                                
               }
               else
               {
                        for (yy=0;yy<8;yy++)
                            for (xx=0;xx<8;xx++){
                                  idx = tileval(tile,7-xx,7-yy)|(col<<bits);
                                  setpixel(x+xx, y+yy, idx);
                            }
                                
               }
        }
}

// Code derived from YAAME by Tom Walker (http://yaame.emuunlim.com/)
// Draws a decoded tile on screen with masked background
void drawtilemasknew(int tile, int x, int y, int col, int xflip, int yflip, int bits)
{
  
        int xx,yy, idx;


        // Clean my spot!
        for (yy=y;yy<y+8;yy++)
          for (xx=x;xx<x+8;xx++)
            setpixel(xx, yy, 0);
            
        
        if (!yflip)
        {
               if (!xflip)
               {
                        for (yy=0;yy<8;yy++)
                            for (xx=0;xx<8;xx++)
                                if (tileval(tile,xx,yy)){
                                  idx = tileval(tile,xx,yy)|(col<<bits);
                                  setpixel(x+xx, y+yy, idx);
                                  set_dirty(x+xx, y+yy);
                                }
                                   
               }
               else
               {
                        for (yy=0;yy<8;yy++)
                            for (xx=0;xx<8;xx++)
                                if (tileval(tile,7-xx,yy))
                                {
                                  idx = tileval(tile,7-xx,yy)|(col<<bits);
                                  setpixel(x+xx, y+yy, idx);
                                  set_dirty(x+xx, y+yy);
                                }
                              
                                   
               }
        }
        else
        {
               if (!xflip)
               {
                        for (yy=0;yy<8;yy++)
                            for (xx=0;xx<8;xx++)
                                if (tileval(tile,xx,7-yy))
                                {
                                  idx = tileval(tile,xx,7-yy)|(col<<bits);
                                  setpixel(x+xx, y+yy, idx);
                                  set_dirty(x+xx, y+yy);
                                }
                                   
               }
               else
               {
                        for (yy=0;yy<8;yy++)
                            for (xx=0;xx<8;xx++)
                                if (tileval(tile,7-xx,7-yy)){
                                  idx = tileval(tile,7-xx,7-yy)|(col<<bits);
                                  setpixel(x+xx, y+yy, idx);
                                  set_dirty(x+xx, y+yy);
                                }
               }
        }
}

// Helper, fill entire screen with a color
// Used for debugging
void fill_screen(int idx){
  auto width = DisplayController.getScreenWidth();
  auto height = DisplayController.getScreenHeight();
  for(int x=0;x<width; x++)
    for(int y=0;y<height; y++)
      DisplayController.setRawPixel(x,y,pixel[idx]);
}

// Helper, sets an initial color palette
void setpal(){
   pixel[0] = DisplayController.createRawPixel(RGB888(0,0,0));
   pixel[1] = DisplayController.createRawPixel(RGB888(224,0,0));
   pixel[2] = DisplayController.createRawPixel(RGB888(0,0,217));
   pixel[3] = DisplayController.createRawPixel(RGB888(224,224,217));
   pixel[4] = DisplayController.createRawPixel(RGB888(0,0,0));
   pixel[5] = DisplayController.createRawPixel(RGB888(224,0,0));
   pixel[6] = DisplayController.createRawPixel(RGB888(0,0,217));
   pixel[7] = DisplayController.createRawPixel(RGB888(224,224,0));
   pixel[8] = DisplayController.createRawPixel(RGB888(0,0,0));
   pixel[9] = DisplayController.createRawPixel(RGB888(224,0,0));
   pixel[10] = DisplayController.createRawPixel(RGB888(0,0,217));
   pixel[11] = DisplayController.createRawPixel(RGB888(224,224,217));
   pixel[12] = DisplayController.createRawPixel(RGB888(0,0,0));
   pixel[13] = DisplayController.createRawPixel(RGB888(224,0,0));
   pixel[14] = DisplayController.createRawPixel(RGB888(0,0,217));
   pixel[15] = DisplayController.createRawPixel(RGB888(224,224,217));
   pixel[16] = DisplayController.createRawPixel(RGB888(0,0,0));
   pixel[17] = DisplayController.createRawPixel(RGB888(224,0,0));
   pixel[18] = DisplayController.createRawPixel(RGB888(0,0,217));
   pixel[19] = DisplayController.createRawPixel(RGB888(224,224,217));
   pixel[20] = DisplayController.createRawPixel(RGB888(0,0,0));
   pixel[21] = DisplayController.createRawPixel(RGB888(224,0,0));
   pixel[22] = DisplayController.createRawPixel(RGB888(0,0,217));
   pixel[23] = DisplayController.createRawPixel(RGB888(224,224,217));
   pixel[24] = DisplayController.createRawPixel(RGB888(0,0,0));
   pixel[25] = DisplayController.createRawPixel(RGB888(224,0,0));
   pixel[26] = DisplayController.createRawPixel(RGB888(0,0,217));
   pixel[27] = DisplayController.createRawPixel(RGB888(224,224,217));
   pixel[28] = DisplayController.createRawPixel(RGB888(0,0,0));
   pixel[29] = DisplayController.createRawPixel(RGB888(224,0,0));
   pixel[30] = DisplayController.createRawPixel(RGB888(0,0,217));
   pixel[31] = DisplayController.createRawPixel(RGB888(224,224,217));
}

void clear_screen(){
   for(int x=0;x<screen_width; x++)
    for(int y=0;y<screen_height; y++)
        DisplayController.setRawPixel(x,y,pixel[0]);
  
}

// Our all beloved Arduino setup(), the first code to ever run on startup
void setup() {

  // The hardware uses one Serial stream at 115200bps
  // TX => Sends regs to the AY-3-8910 MCU drivers
  // RX => Receives two bytes, one for each player inputs, from the controllers MCU
  Serial.begin(115200);

  freeheap();

  // Initialization of the FabGL Library
  DisplayController.begin();

  // Although the games run at a 256x224@60 resolution, for this projected
  // 320x240@60 was adopted to narrow down configuration issues with different arcade monitors
  // The image is centered on screen, leaving enough room to ajust the monitor but also not
  // losing that lovely scanline definition

  // For Arcade Monitors:
  // DisplayController.setResolution("\"320x240_60.00\" 6.00 320 336 360 400 240 243 247 252 -hsync -vsync");

  // For CRT VGA Monitors
  DisplayController.setResolution("\"320x240_120.00\" 12.25  320 336 360 400 240 243 247 261 -hsync +vsync");
  //DisplayController.setResolution("\"320x224_130.00\"   12.50  320 336 360 400  224 227 237 245 -hsync +vsync");

  //DisplayController.setResolution(QVGA_320x240_60Hz);
  
  // sets an initial color palette
  setpal();

  // Let's clean the screen
  auto width = DisplayController.getScreenWidth();
  auto height = DisplayController.getScreenHeight();
  screen_height = (int)height;
  screen_width  = (int)width;
  clear_screen();

  delay(1000);

  // Starts a parallel task on core 1 to handle the Serial communication
  xTaskCreatePinnedToCore(SerialTASK, "SerialTASK", 8192, NULL, 1, NULL, cpuCore);

}

// Helper, prints the free heap
// Used for debugging
void freeheap(){
  Serial.println("----");
  Serial.print("Total heap: ");  Serial.println(ESP.getHeapSize());
  Serial.print("Free heap: ");   Serial.println(ESP.getFreeHeap());
  Serial.print("Total PSRAM: "); Serial.println(ESP.getPsramSize());
  Serial.print("Free PSRAM:: "); Serial.println(ESP.getFreePsram());
}

// Helper, prints a white square frame on the boundaries of the screen
void screen_bounds(){

  for(int x=0;x<screen_width;x++)  DisplayController.setRawPixel(x,0,pixel[3]);
  for(int x=0;x<screen_width;x++)  DisplayController.setRawPixel(x,1,pixel[3]);
  for(int x=0;x<screen_width;x++)  DisplayController.setRawPixel(x,screen_height-1,pixel[3]);
  for(int x=0;x<screen_width;x++)  DisplayController.setRawPixel(x,screen_height-2,pixel[3]);
  for(int y=0;y<screen_height;y++) DisplayController.setRawPixel(0,y,pixel[3]);
  for(int y=0;y<screen_height;y++) DisplayController.setRawPixel(1,y,pixel[3]);
  for(int y=0;y<screen_height;y++) DisplayController.setRawPixel(screen_width-1,y,pixel[3]);
  for(int y=0;y<screen_height;y++) DisplayController.setRawPixel(screen_width-2,y,pixel[3]);
  
}

// Loads and starts a game
void load_game(int idx){

  fantastc.attachRAM(0xffff);

  switch (idx){
    case 0:
      fantastc_pal();
      fantastc.load(0, fantastc_cpu, 0x8000+1);
    break;
    case 1:
      kong_pal();
      fantastc.load(0, kong_cpu, 0x8000+1);
    break;
    case 2:
      timefgtr_pal();
      fantastc.load(0, timefgtr_cpu, 65535);
    break;
    case 3:
      crest_pal();
      fantastc.load(0, crest_cpu, 65535);
    break;
    case 4:
      zigzag_pal();
      fantastc.load(0, zigzag_cpu, 65535);
    break;
    case 5:
      galaxiant_pal();
      fantastc.load(0, galaxiant_cpu, 65535);
    break;
    case 6:
      munch_pal();
      fantastc.load(0, munch_cpu, 65535);
    break;
    case 7:
      galemp_pal();
      fantastc.load(0, galemp_cpu, 65535);
    break;
    default:
      fantastc_pal();
      fantastc.load(0, fantastc_cpu, 0x8000+1);
    break;   
  }

  fantastc.run(0);
}

// Main loop
// The chosen game is started and the core0 is "locked" to the main CPU process
void loop() {
  cpuCore = xPortGetCoreID();
  if(cpuCore == 1) gfxCore = 0;
  else gfxCore = 1;
  load_game(GAME);
}


/*
 * Z80 Machine Declarations
 */
Machine::Machine()
  : m_realSpeed(false)
{
  m_Z80.setCallbacks(this, readByte, writeByte, readWord, writeWord, readIO, writeIO);
}

Machine::~Machine()
{
  delete [] m_RAM;
}

// Title screen includes substitution for "GORILLA"
#include "gorila_title.hpp"                       

bool chkSprite(int x, int y){
  int spritex,spritey,cc,ccc;
  for (cc=14;cc>-1;cc--){
    ccc=cc<<2;
    spritex=fantastc.m_RAM[0x9840+ccc]+8;
    if (spritex>8 && spritex<0xF8){
      spritey=fantastc.m_RAM[0x9843+ccc]+1;
      if(x == spritex && y == spritey) return false;
      else return true;
    }
  }
}

void drawBullets(const uint8_t *base){
  int dx, dy;
  int m_flipscreen_y = 0;
  /* iterate over scanlines */
  for (int y = 0; y <= 224; y++)
  {
    uint8_t shell = 0xff, missile = 0xff;
    uint8_t effy;

    /* the first 3 entries match Y-1 */
    effy = m_flipscreen_y ? ((y - 1) ^ 255) : (y - 1);
    for (int which = 0; which < 3; which++)
      if (uint8_t(base[which*4+1] + effy) == 0xff)
        shell = which;

    /* remaining entries match Y */
    effy = m_flipscreen_y ? (y ^ 255) : y;
    for (int which = 3; which < 8; which++)
      if (uint8_t(base[which*4+1] + effy) == 0xff)
      {
        if (which != 7)
          shell = which;
        else
          missile = which;
      }
    
    /* draw the shell */
    if (shell != 0xff) {
      dy = 224 - base[shell*4+3] + 24 ;
      dx = 255 -y + 8;
      setpixel(dx, dy,   3, 32);
      setpixel(dx, dy+1, 3, 32);
      setpixel(dx, dy+2, 3, 32);
    }

    if (missile != 0xff){
      dy = 224 - base[missile*4+3] + 24;
      dx = 255 -y + 8;
      setpixel(dx, dy,   3, 1);
      setpixel(dx, dy+1, 3, 1);
      setpixel(dx, dy+2, 3, 1);
    }
    
  }
}

// Main game graphics processing routine
void Machine::draw(){
  busy = 1; // Take the drawing slot
  int y,x, destx, desty, cc, ccc, spritebase, videobase;
  int pos=0;
  unsigned char c;
  signed char scroll;
  int kong_title=0;
  int kong_tente=0;

  if(wireframe == true) screen_bounds(); // Draw screen boundaries frame
  if(GAME == 4 || GAME == 5 || GAME == 6 || GAME == 7){
    spritebase = 0x5800;
    videobase  = 0x5000;
  }else{
    spritebase = 0x9800;
    videobase  = 0x9000;
  }

  // Sprites processing, for all games
  int spritex,spritey,spritecode,xflip,yflip,spritecol;
        for (cc=14;cc>-1;cc--)
        {
          ccc=cc<<2;
          spritex=fantastc.m_RAM[spritebase+0x40+ccc]+8;
          
          if (spritex>8 && spritex<0xF8){
            spritey=fantastc.m_RAM[spritebase+0x43+ccc]+1;
            spritecol=fantastc.m_RAM[spritebase+0x42+ccc]&7;
            spritecode=fantastc.m_RAM[spritebase+0x41+ccc]&0x3F;
            spritecode<<=2;
            if(GAME == 4 || GAME == 6) spritecode+=256;
            if(GAME == 0 || GAME == 1) spritecode += 0x100;
            if(spritecode == 408) kong_tente++;                    // THIS IS A HACK! Condition to invoke "Tente outra vez" sample for "GORILA"
            xflip=fantastc.m_RAM[spritebase+0x41+ccc]&0x80;
            yflip=fantastc.m_RAM[spritebase+0x41+ccc]&0x40;
                        if (!xflip)
                        {
                                if (!yflip)
                                {
                                        drawtilemasknew(spritecode+2,spritex,spritey,spritecol,0,0,2);
                                        drawtilemasknew(spritecode,spritex+8,spritey,spritecol,0,0,2);
                                        drawtilemasknew(spritecode+3,spritex,spritey+8,spritecol,0,0,2);
                                        drawtilemasknew(spritecode+1,spritex+8,spritey+8,spritecol,0,0,2);
                                }
                                else
                                {
                                        drawtilemasknew(spritecode+2,spritex,spritey+8,spritecol,0,1,2);
                                        drawtilemasknew(spritecode,spritex+8,spritey+8,spritecol,0,1,2);
                                        drawtilemasknew(spritecode+3,spritex,spritey,spritecol,0,1,2);
                                        drawtilemasknew(spritecode+1,spritex+8,spritey,spritecol,0,1,2);
                                }
                        }
                        else
                        {
                                if (!yflip)
                                {
                                        drawtilemasknew(spritecode+2,spritex+8,spritey,spritecol,1,0,2);
                                        drawtilemasknew(spritecode,spritex,spritey,spritecol,1,0,2);
                                        drawtilemasknew(spritecode+3,spritex+8,spritey+8,spritecol,1,0,2);
                                        drawtilemasknew(spritecode+1,spritex,spritey+8,spritecol,1,0,2);
                                }
                                else
                                {
                                        drawtilemasknew(spritecode+2,spritex+8,spritey+8,spritecol,1,1,2);
                                        drawtilemasknew(spritecode,spritex,spritey+8,spritecol,1,1,2);
                                        drawtilemasknew(spritecode+3,spritex+8,spritey,spritecol,1,1,2);
                                        drawtilemasknew(spritecode+1,spritex,spritey,spritecol,1,1,2);
                                }
                        }
          }
          
        }


  // Background Processing, for all games
  for (x = 32; x>0; x--)
  {
    for (y = 0; y < 32; y++)
    {
        c = fantastc.m_RAM[videobase+(pos++)];
        //if(c == 0x10) continue;
        if(c == 176)  kong_title++;
        scroll=(signed char)fantastc.m_RAM[spritebase+(y<<1)];
        destx = ((x<<3)+scroll)&255;
        desty = y<<3;
        // THIS IS A HACK!
        // Specific conditions for detecting the title screen of "GORILA"
        if(GAME == 1 && fantastc.m_Z80.readRegByte(18) == 0 && fantastc.m_Z80.readRegByte(8) == 167 && desty >= 80 && desty <= 176) {
          // Ignore these tiles, this space will be filled bellow
        } else drawtilenew(c,destx,desty,fantastc.m_RAM[spritebase+0x01+(y<<1)],0,0,2); // Otherwise just draw those tiles
    }
  }


  // THIS IS A HACK!
  // Ok, so it's the title screen of "GORILA", let's replace that logo with the correct one!
  if(GAME == 1 && fantastc.m_Z80.readRegByte(18) == 0 && fantastc.m_Z80.readRegByte(8) == 167 && kong_title == 65) gorila_title();


  if(GAME == 7){
    drawBullets(&fantastc.m_RAM[0x5800+96]);
  }

  // bullets processing, used by "FANTASTIC" and "TIME FIGHTERS"
  if(GAME == 0 || GAME == 2 || GAME == 5){
    for (cc=0;cc<32;cc+=4)
    {
      if(GAME == 1){
        x=fantastc.m_RAM[spritebase+(0xc1+cc)]+7;
        y=256-fantastc.m_RAM[spritebase+(0xc3+cc)]-4;
      }else{
        x=fantastc.m_RAM[0x5861+cc];
        y=256-fantastc.m_RAM[0x5863+cc]-4;
      }
      if (x>0&&x<256&&y>-1&&y<253)
      {
        if (cc!=28)
        {
            setpixel(x, y, 3);
            setpixel(x, y+1, 3);
            setpixel(x, y+2, 3);
         }
         else
         {
            setpixel(x+8, y, 7);
            setpixel(x+8, y+1, 7);
            setpixel(x+8, y+2, 7);
          }
      }
    }
  }



  // PCM Samples processings for "GORILA"
  if(GAME == 1){
    if(kong_tente > 0)                                     // Should we play "Tente outra vez"?
    {
      if(voice_playing[3] == 0){
        DacAudio.Play(&wav_tente);
        voice_playing[3] = 1;
      }
    }else if(kong_tente == 0){
      voice_playing[3] = 0;
    }
    if(kong_title == 65 && voice_playing[0] == 0){         // Sould we play "Eu sou o GORILA"?
      DacAudio.StopAllSounds();
      DacAudio.Play(&wav_gorila);
      voice_playing[0] = 1;
    }else if(kong_title == 0){
      voice_playing[0] = 0;
    }
    if(fantastc.m_RAM[videobase+(160)] == 0x60){
      if(voice_playing[1] == 0){
        DacAudio.Play(&wav_venha);
        voice_playing[1] = 1;
      }
    }else{
      voice_playing[1] = 0;
    }
    DacAudio.FillBuffer();
  }

  busy = 0; // Free drawing slot
  
}

// Paralell task for the graphics generation on core1
void drawTASK( void * pvParameters ){
  fantastc.draw();
  vTaskDelete(NULL);
}

// Loads a buffer into the Z80 emulator RAM
void Machine::load(int address, uint8_t const * data, int length)
{
  for (int i = 0; i < length; ++i)
    m_RAM[address + i] = data[i];
}

// Creates a RAM object for the Z80 emulator
void Machine::attachRAM(int RAMSize)
{
  m_RAM = new uint8_t[RAMSize];
}

// Steps the Z80 CPU
int Machine::nextStep()
{
  return m_Z80.step();
}

// Serial bus task
// TX => AY-3-8910 regis for the MCU drivers
// RX => INPUTS, one byte for each player
void SerialTASK( void * pvParameters ){
  uint8_t ctl[2];
  while(1){
    TIMERG0.wdt_wprotect=TIMG_WDT_WKEY_VALUE;
    TIMERG0.wdt_feed=1;
    TIMERG0.wdt_wprotect=0;
    if (Serial.available() > 0) {
      Serial.readBytes(ctl, 2);
      player1_ctl = ctl[0];
      player2_ctl = ctl[1];
    }
    uint8_t buf[3];
    if(YMBUFF[ym_flush_pos].dirty == true){
      buf[0] = YMBUFF[ym_flush_pos].chip;
      buf[1] = YMBUFF[ym_flush_pos].reg;
      buf[2] = YMBUFF[ym_flush_pos].data;
      Serial.write(buf, 3);
      Serial.flush();
      YMBUFF[ym_flush_pos].dirty = false;
      ym_flush_pos++;
      if(ym_flush_pos >=YMBUFFSIZE) ym_flush_pos = 0;
    }
    delayMicroseconds(10);
  }
  vTaskDelete(NULL);
}

// Helper, get the specific bit
// Used to check player inputs
bool getBit(unsigned char byte, int position) // position in range 0-7
{
    return (byte >> position) & 0x1;
}

// Z80 Ram reads
int      aux=0;
uint64_t aux_t  = 0;
uint64_t aux_t2 = 0;
int Machine::readByte(void * context, int address)
{

  byte            rv = 0;

  if(GAME == 4 || GAME == 5 || GAME == 6 || GAME == 7){
    switch (address){
      case 0x6000: // PORT IN0
          if(getBit(player1_ctl,0) == true) rv |= IPT_COIN1[GAME];
          if(getBit(player1_ctl,4) == true) rv |= IPT_JOYSTICK_LEFT[GAME];
          if(getBit(player1_ctl,5) == true) rv |= IPT_JOYSTICK_RIGHT[GAME];
          if(getBit(player1_ctl,6) == true && GAME != 1) rv |= IPT_BUTTON1[GAME];
          if(getBit(player1_ctl,2) == true && GAME > 0) rv |= IPT_JOYSTICK_UP[GAME];
          break;
      case  0x6800: // PORT IN1
          if(getBit(player1_ctl,1) == true && GAME != 1) rv |= IPT_START1[GAME];
          if(getBit(player2_ctl,1) == true) rv |= IPT_START2[GAME];
          if(getBit(player1_ctl,3) == true) rv |= IPT_JOYSTICK_DOWN[GAME];
          if(getBit(player1_ctl,6) == true && GAME == 1) rv |= IPT_START1[GAME]; // Kong START=>Jump
          break;
      case 0x7000: // PORT IN2
          return 0x00;
          break;
      case 0x7800: // Watchdog
          return 0xff;
          break;
      default:
          rv = ((Machine*)context)->m_RAM[address];    
    }
  }else{ 
    switch (address){
        case 0xa000: // PORT IN0
          //   0b00S00000
          //rv = 0b11000000;
          if(GAME == 1) rv = 0b00000000;
          else rv = 0b01000000;
          //if(esp_timer_get_time() - aux_t > 1000000){
          //  aux_t = esp_timer_get_time();
          //  rv |= IPT_COIN1;
          //}
          //getControllers();
          if(getBit(player1_ctl,0) == true) rv |= IPT_COIN1[GAME];
          if(getBit(player1_ctl,4) == true) rv |= IPT_JOYSTICK_LEFT[GAME];
          if(getBit(player1_ctl,5) == true) rv |= IPT_JOYSTICK_RIGHT[GAME];
          if(getBit(player1_ctl,6) == true && GAME != 1) rv |= IPT_BUTTON1[GAME];
          if(getBit(player1_ctl,2) == true && GAME > 0) rv |= IPT_JOYSTICK_UP[GAME];
          break;
        case 0xa800: // PORT IN1
          //0x[Bases][Coins Per Play]
          rv = 0b00000000;
          if(getBit(player1_ctl,1) == true && GAME != 1) rv |= IPT_START1[GAME];
          if(getBit(player2_ctl,1) == true) rv |= IPT_START2[GAME];
          if(getBit(player1_ctl,3) == true) rv |= IPT_JOYSTICK_DOWN[GAME];
          if(getBit(player1_ctl,6) == true && GAME == 1) rv |= IPT_START1[GAME]; // Kong START=>Jump
          break;
        case 0xb000: // PORT IN2
          //0xb000000[2 bits level];
          //rv = 0b00000001;
          //rv |= 1 >> 2;
          rv = 0b00000000;
          break;
        case 0xb800: // whatchdog
          rv = 0xff;
          break;
        case 0x8807:
          //Serial.println("0x8807: ay8910_device::data_r");
          break;
        case 0x880d:
          //Serial.println("0x880d: ay8910_device::data_r");
          break;
        default:
          rv = ((Machine*)context)->m_RAM[address];
    }
  }
  return rv;
}

// AY-3-8910 address write
// Derived from MAME sources
void ay8910_device_address_w(int chip, int data){
  ay8910_write_ym(chip, 0, data);
}

// AY-3-8910 data write
// Derived from MAME sources
void ay8910_device_data_w(int chip, int data){
  ay8910_write_ym(chip, 1, data);
}

// AY-3-8910 register writes
// updates the buffer to be send to the MCU drivers
void ay8910_write_reg(int chip, int reg, int data){
  YMBUFF[ym_write_pos].chip = chip;
  YMBUFF[ym_write_pos].reg  = reg;
  YMBUFF[ym_write_pos].data = data;
  YMBUFF[ym_write_pos].dirty = true;
  ym_write_pos++;
  if(ym_write_pos>=YMBUFFSIZE) ym_write_pos = 0;
}

// AY-3-8910 write ym
// Derived from MAME sources
bool m_active[2];
uint32_t m_register_latch[2];
uint8_t  m_regs[2][16 * 2];
void ay8910_write_ym(int chip, int addr, int data){
  if (addr & 1)
  {
    if (m_active[chip])
    {
      const uint8_t register_latch = m_register_latch[chip] + 0;
      /* Data port */
      if (m_register_latch[chip] == 0x0d || m_regs[chip][register_latch] != data) // 0x0d => AY_EASHAPE
      {
        /* update the output buffer before changing the register */
        //m_channel->update();
      }
      ay8910_write_reg(chip, register_latch, data);
    }
  }
  else
  {
    m_active[chip] = (data >> 4) == 0; // mask programmed 4-bit code
    if (m_active[chip])
    {
      /* Register port */
      m_register_latch[chip] = data & 0x0f;
    }
  }  
}

// Z80 CPU RAM write
void Machine::writeByte(void * context, int address, int value)
{

  ((Machine*)context)->m_RAM[address] = value;

  if(GAME == 4 || GAME == 5 || GAME == 6 || GAME == 7){
    switch (address){
      case 0x6803:
        // Noise
        break;
      case 0x7001: // NMI
        nmi = value;
        break;
      case 0x7800: // Sound Write
        break;
    }
  }
  

  // Hardware properties common for some games
    switch (address){
        case 0x9000 ... 0x9bff: // 9800 VBLANK
          //xTaskCreatePinnedToCore(drawTASK, "drawTASK", 8192*2, NULL, 1, NULL, gfxCore);
          break;
        case 0x8803: // FX Addr
          ay8910_device_address_w(0, value);
          break;
        case 0x880b: // FX Value
          ay8910_device_data_w(0, value);
          break;
        case 0x880c: // FM Addr
          ay8910_device_address_w(1, value);
          break;
        case 0x880e: // FM Value
          ay8910_device_data_w(1, value);
          break;
        case 0xb000: // IRQ NMI
          nmi = value;
          break;
    }

   // Hardware properties for "GORILA"
   if(GAME == 1){
    switch(address){
      case 0xa004: // galaxian_sound_device::lfo_freq_w
        //Serial.print("galaxian_sound_device::lfo_freq_w:"); Serial.println(value);
        break;
      case 0xa800 ... 0xa807: // galaxian_sound_device::sound_w
        //Serial.print("galaxian_sound_device::sound_w:"); Serial.println(value);
        if(value == 0){
          ay8910_write_reg(1, 0x07, 0b11111111); // Mute
        }
        else{
          if(address == 0xa805){
            ay8910_write_reg(1, 8, 0b00001111);
            ay8910_write_reg(1, 6, 0b00000001);
            ay8910_write_reg(1, 7, 0b00000000);
          }else{
            DacAudio.Play(&wav_noise);
          }
        }
        break;
      case 0xb800: // galaxian_sound_device::pitch_w
        //Serial.print("galaxian_sound_device::pitch_w:"); Serial.println(value);
        if(value == 255){
          ay8910_write_reg(0, 0x07, 0b11111111); // Mute
        }else{
          if(value == 202 && voice_playing[2] == 0){
            DacAudio.Play(&wav_nao);
            voice_playing[2] = 1;
          }
          if(value == 184) voice_playing[2] = 0;
          int nval = 255-value;
          ay8910_write_reg(0, 0x08, 0b00001111);
          ay8910_write_reg(0, 0x00, nval);
          ay8910_write_reg(0, 0x01, 0x01);
          ay8910_write_reg(0, 0x01, 0b00000000);
          ay8910_write_reg(0, 0x07, 0b00111110);
        }
        break;
    }
   }
}

// Z80 Read I/O (not used)
int Machine::readIO(void * context, int address)
{
  return 0xff;
}

// Z80 Write I/O (not used)
void Machine::writeIO(void * context, int address, int value)
{
  //
}

// Helper, dumps all Z80 regs
// Used only for debugging and triggers "aggressive loop warning" if uncommented
void Machine::dumpRegs(){
  /* 
  char msg[100];
  for(int reg=0; reg<20; reg++){
    sprintf(msg, "%02d:%03d | ", reg, m_Z80.readRegByte(reg));
    Serial.print(msg);
  }
  Serial.println(" ");
  */
}

// Main loop of the emulated Z80, where the magic happens!
void Machine::run(int address)
{
    m_Z80.reset();                                                                   // Reset the Z80 to clear all regs
    m_Z80.setPC(address);                                                            // Points the program counter to the first address
    int cycles = 0;
    int64_t tfps = esp_timer_get_time();                                             // Initialize our timer vector (16000us => frame duration, 60fps)
    while (true) {
        if(getBit(player1_ctl,7) == true) {                                          // Check if Jamma P1 BTN 2 is presset
          while(getBit(player1_ctl,7) == true) delayMicroseconds(100); // debaunce
          if(menu_active == 0){                                                      // Ok, start menuing
            clear_screen();
            menu_item = GAME;
            menu_item_last = -1;
            menu_active = 1;
            cv.clear();
            screen_bounds();
            ay8910_write_reg(0, 0x07, 0b11111111); // Mute AY0
            ay8910_write_reg(1, 0x07, 0b11111111); // Mute AY1
            delay(100);
          }else{                                                                     // Stop menuing
            menu_active = 0;
            clear_screen();
          }
        }
        
        if(menu_active) {                                                            // Menuing
          DacAudio.FillBuffer();
          if(getBit(player1_ctl,4) == true) {
            menu_item--;
            while(getBit(player1_ctl,4) == true) delayMicroseconds(100); // debounce
          }
          if(getBit(player1_ctl,5) == true){
            menu_item++;
            while(getBit(player1_ctl,5) == true) delayMicroseconds(100); // debounce
          }
          
          if(menu_item < 0) menu_item = menu_item_max;
          if(menu_item > menu_item_max) menu_item = 0;
          
          if(menu_item != menu_item_last){
            DacAudio.Play(menu_samples[menu_item]);
            cv.drawBitmap(31,8,menu_bitmaps[menu_item]);
            menu_item_last = menu_item;
          }
          
          if(getBit(player1_ctl,6) == true) {
            while(getBit(player1_ctl,6) == true) delayMicroseconds(100); // debounce
            DacAudio.StopAllSounds(); // Stop Playing any Sample
            memset(m_RAM, 0x00, 0xffff);
            GAME = menu_item;
            switch(GAME){                                                                  // Reload CPU with the newly chosen game
              case 0:
                fantastc_pal();
                for (int i = 0; i < 0xffff; ++i) m_RAM[address + i] = fantastc_cpu[i];
              break;
              case 1:
                kong_pal();
                for (int i = 0; i < 0xffff; ++i) m_RAM[address + i] = kong_cpu[i];
              break;
            }
            m_Z80.reset();                                                                   // Reset the Z80 to clear all regs
            m_Z80.setPC(0);
            menu_active = 0;
            clear_screen();
            delay(500);
          }
          delay(8); // Let's not exaust the core
        }
        else {                                                                               // Normal CPU processing
          cycles += nextStep();
          if(nmi && (esp_timer_get_time() > tfps + frame_time[GAME])){                       // If NMI is pending and we're just in the right time, INTERRUPT!
             if(busy == 0)                                                                   // If we're not busy drawing past frame, starts a new task
             xTaskCreatePinnedToCore(drawTASK, "drawTASK", 8192*2, NULL, 1, NULL, gfxCore);  // otherwise skip the current frame
             tfps=esp_timer_get_time();                                                      // Updates the timer vector
             nmi = 0;                                                                        // Clears the NMI trigger
             m_Z80.NMI();                                                                    // Do a NON MASKABLE INTERRUPT!
             cycles = 0;
             if(GAME == 1) DacAudio.FillBuffer();                                            // Fill samples buffer for GORILA
          }
        }
    }
}
