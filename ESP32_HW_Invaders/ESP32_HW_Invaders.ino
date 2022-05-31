#pragma GCC optimize ("O3")

/*
 * Taito do Brasil Arcade Bootleg v1.2
 * Nino MegaDriver - nino@nino.com.br
 * License GPL v3.0
 * 
 * Maincode for the ESP32 Devkit V1 module
 * based on the Space Invaders arcade board
 * 
 * Most of this code was ported from the very fast and reliable
 * emulator "SIDE - Space Invaders Didactic Emulator" by Alessandro Scotti.
 * (https://walkofmind.com/programming/side/side.htm)
 * 
 * Libraries and Helpers used:
 * 
 * FabGL (http://www.fabglib.org/) for video generation
 * DacAudio by https://www.xtronical.com/
 * 
 */

// START: Initial Includes & Declares
#include "soc/timer_group_struct.h"
#include "soc/timer_group_reg.h"
int scanLineColor[260];
float fps = 59.541985;
long cycles_per_interrupt = (1996800 / (2*fps));
uint8_t pixel[6];
// END: Initial Includes & Declares

// START: JammaSerialController
uint8_t player1_ctl = 0x00;
uint8_t player2_ctl = 0x00;
bool getBit(unsigned char byte, int position) // position in range 0-7
{
    return (byte >> position) & 0x1;
}
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
    delayMicroseconds(10);
  }
  vTaskDelete(NULL);
}
// END: JammaSerialController

// START: Audio Libs & Routines
#include "XT_DAC_Audio.h"
XT_DAC_Audio_Class DacAudio(25,0);
void DACTask( void * pvParameters ){
  while(1){
    TIMERG0.wdt_wprotect=TIMG_WDT_WKEY_VALUE;
    TIMERG0.wdt_feed=1;
    TIMERG0.wdt_wprotect=0;
    DacAudio.FillBuffer();
    delayMicroseconds(1);
  }
  vTaskDelete(NULL);
}
#include "samples.hpp"
// END: // START: Audio Libs & Routines

// START: CPU Emulation Includes & Declares
extern "C" {
  #include "i8080.h"
}
uint8_t memory[65535];
i8080 cpu;
// END: CPU Emulation Includes & Declares


// START: Port & Button Events
static int      nShips  = 3;
static int      nEasy   = 0;
unsigned        sounds_;
unsigned char   port1_;
unsigned char   port2i_;    // Port 2 in
unsigned char   port2o_;    // Port 2 out
unsigned char   port3o_;    // Port 3 out
unsigned char   port4lo_;   // Port 4 out (lo)
unsigned char   port4hi_;   // Port 4 out (hi)
unsigned char   port5o_;    // Port 5 out
enum Events {
        KeyLeftDown     = 0,
        KeyLeftUp,
        KeyRightDown,
        KeyRightUp,
        KeyFireDown,
        KeyFireUp,
        KeyOnePlayerDown,
        KeyOnePlayerUp,
        KeyTwoPlayersDown,
        KeyTwoPlayersUp,
        CoinInserted
};
void fireEvent( int event )
{
    switch( event ) 
    {
    case KeyLeftDown:
        port1_ |= 0x20;
        break;
    case KeyLeftUp:
        port1_ &= ~0x20; 
        break;
    case KeyRightDown:
        port1_ |= 0x40; 
        break;
    case KeyRightUp:
        port1_ &= ~0x40; 
        break;
    case KeyFireDown:
        port1_ |= 0x10;
        break;
    case KeyFireUp:
        port1_ &= ~0x10;
        break;
    case KeyOnePlayerDown:
        port1_ |= 0x04;
        break;
    case KeyOnePlayerUp:
        port1_ &= ~0x04;
        break;
    case KeyTwoPlayersDown:
        port1_ |= 0x02;
        break;
    case KeyTwoPlayersUp:
        port1_ &= ~0x02;
        break;
    case CoinInserted:
        port1_ |= 0x01;
        break;
    }
}
static uint8_t generic_port_in(void* userdata, uint8_t port) {

  unsigned char   b = 0;

    switch( port ) {
    case 1: 
        b = port1_; 
        port1_ &= 0xFE; 
        break;
    case 2: 
        b = (port2i_ & 0x8F) | (port1_ & 0x70); // Player 1 keys are used for player 2 too
        break;
    case 3: 
        b = (unsigned char)((((port4hi_ << 8) | port4lo_) << port2o_) >> 8);
        break;
    }

    if( port == 1 || port == 2){
      if(getBit(player1_ctl,1) == false) fireEvent( KeyOnePlayerUp );  else fireEvent( KeyOnePlayerDown );
      if(getBit(player2_ctl,1) == false) fireEvent( KeyTwoPlayersUp ); else fireEvent( KeyTwoPlayersDown );
      if(getBit(player1_ctl,0) == false) fireEvent( CoinInserted );
      if(getBit(player1_ctl,4) == false) fireEvent( KeyLeftUp );       else fireEvent( KeyLeftDown );
      if(getBit(player1_ctl,5) == false) fireEvent( KeyRightUp );      else fireEvent( KeyRightDown );
      if(getBit(player1_ctl,6) == false) fireEvent( KeyFireUp );       else fireEvent( KeyFireDown );
    }

    return b;
  
}

static void generic_port_out(void* userdata, uint8_t addr, uint8_t b) {
    switch( addr ) {
    case 2:
        port2o_ = b;
        break;
    case 3:
        // Port 3 controls some sounds
        if( ! (b & 0x01) ) sounds_ &= ~SoundUfo;
        if( b & 0x01 ) {
          sounds_ |= SoundUfo;
          playSample(1);
        }
        if( (b & 0x02) && !(port3o_ & 0x02) ){
          sounds_ |= SoundShot;
          playSample(2);
        }
        if( (b & 0x04) && !(port3o_ & 0x04) ) {
          sounds_ |= SoundBaseHit;
          playSample(3);
        }
        if( (b & 0x08) && !(port3o_ & 0x08) ){
          sounds_ |= SoundInvaderHit;
          playSample(4);
        }
        port3o_ = b;
        break;
    case 4:
        port4lo_ = port4hi_;
        port4hi_ = b;
        break;
    case 5:
        // Port 5 controls some sounds
        
        if( (b & 0x01) && !(port5o_ & 0x01) ) { 
          sounds_ |= SoundWalk1;
          playSample(5);
        }
        if( (b & 0x02) && !(port5o_ & 0x02) ) {
          sounds_ |= SoundWalk2;
          playSample(6);
        }
        if( (b & 0x04) && !(port5o_ & 0x04) ) {
          sounds_ |= SoundWalk3;
          playSample(7);
        }
        if( (b & 0x08) && !(port5o_ & 0x08) ) {
          sounds_ |= SoundWalk4;
          playSample(8);
        }
        if( (b & 0x10) && !(port5o_ & 0x10) ) {
          sounds_ |= SoundUfoHit;
          playSample(9);
        }
        port5o_ = b;
        break;
    }
}
// END: Port & Button Events

// START: Game Includes
#include "galactic.hpp"
#include "invaders.hpp"
#include "polarisbr.hpp"
#include "invadpt2br.hpp"
#include "indianbtbr.hpp"
// END: Game Includes

uint8_t GAME = 0;

// START: Graphics Generic Screen & Declares
#include <fabgl.h>
#include "res.hpp"
fabgl::VGAController DisplayController;
fabgl::Canvas cv(&DisplayController);
int screen_width, screen_height;
void setpixel(int x, int y, int idx){
    int c = 0;
    if(idx > 0) c = scanLineColor[y];
    if(x <= 224 && y <=260 && x>= 0 && y >= 0){ // Primeira Verificação
        y+= 30;
        x+= 8;
        x = screen_height - x;
        y = screen_width - y;
        if(x<screen_height && y < screen_width && x>0 && y>0) DisplayController.setRawPixel(y, x, pixel[c]);
    }
}

// Menuing Vars
// Bitmaps for the selection page for each game
Bitmap *menu_bitmaps[] =       { &menu_galactica, &menu_indian, &menu_invaders, &menu_invaders2, &menu_polaris};
// Samples for each menu page
XT_Wav_Class *menu_samples[] = {&wav_galactica,   &wav_indian,  &wav_invaders,  &wav_invaders2,  &wav_polaris};
uint8_t menu_active = 1;                                                // Menu active?
int menu_item = GAME;                                                   // Default menu item pointing to default GAME
uint8_t menu_item_last = -1;                                            // Last selected item
uint8_t menu_item_max = 4;                                              // Max index for the menu items (Currently only Fantastic and Kong are playable

// Helper, prints a white square frame on the boundaries of the screen
// Used for debugging
void screen_bounds(){

  for(int x=0;x<screen_width;x++)  DisplayController.setRawPixel(x,0,pixel[1]);
  for(int x=0;x<screen_width;x++)  DisplayController.setRawPixel(x,1,pixel[1]);
  for(int x=0;x<screen_width;x++)  DisplayController.setRawPixel(x,screen_height-1,pixel[1]);
  for(int x=0;x<screen_width;x++)  DisplayController.setRawPixel(x,screen_height-2,pixel[1]);
  for(int y=0;y<screen_height;y++) DisplayController.setRawPixel(0,y,pixel[1]);
  for(int y=0;y<screen_height;y++) DisplayController.setRawPixel(1,y,pixel[1]);
  for(int y=0;y<screen_height;y++) DisplayController.setRawPixel(screen_width-1,y,pixel[1]);
  for(int y=0;y<screen_height;y++) DisplayController.setRawPixel(screen_width-2,y,pixel[1]);
  
}

static void videoram_w (int offset,int data)
{
  int i,x,y;
  y = offset / 32;
  x = 8 * (offset % 32);
  for (i = 0; i < 8; i++)
  {
    if(data & 0x01) setpixel(y, x, 1);
    else setpixel(y, x, 0);
    x ++;
    data >>= 1;
  }
}
// END: Graphics Generic Screen & Declares

// Generic Write RAM
static void wb(void* userdata, uint16_t addr, uint8_t val) {
  if( addr < 0x2000) return;
  if( addr > 0x4000 && addr< 0x5fff) return;
  memory[addr] = val;
  if( addr >= 0x2400 && addr <= 0x3fff) videoram_w(addr - 0x2400, val);
}

// Generic Read Ram
static uint8_t rb(void* userdata, uint16_t addr) {
  return memory[addr];
}


void screen_clear(){
  for(int x=0;x<screen_width;x++)
    for(int y=0;y<screen_height;y++)
       DisplayController.setRawPixel(x, y, pixel[0]);
}

void load_game(){
  memset(memory, 0x00, 0xffff);
  switch (GAME){
    case 0:
      galactic_layout();
      memcpy(memory, galactic, 65535);
      break;
    case 1:
      indianbtbr_layout();
      memcpy(memory, indianbtbr, 65535);
      break;
    case 2:
      invaders_layout();
      memcpy(memory, invaders, 65535);
      break;
    case 3:
      invadpt2br_layout();
      memcpy(memory, invadpt2br, 65535);
      break;
    case 4:
      polarisbr_layout();
      memcpy(memory, polarisbr, 65535);
    break;
  }
  
  i8080_init(&cpu);
  cpu.userdata = &cpu;
  cpu.read_byte = rb;
  cpu.write_byte = wb;
  cpu.port_in  = generic_port_in;
  cpu.port_out = generic_port_out;
  fps = 59.541985;
  cycles_per_interrupt = (1996800 / (2*fps));
  
}

void setup(){
  Serial.begin(115200);
  
  DisplayController.begin();
  // For legacy arcade Monitors:
  // DisplayController.setResolution("\"320x240_60.00\" 6.00 320 336 360 400 240 243 247 252 -hsync -vsync");

  // For CRT VGA monitors:
  DisplayController.setResolution("\"320x240_120.00\" 12.25  320 336 360 400 240 243 247 261 -hsync +vsync");

  screen_width  = DisplayController.getScreenWidth();
  screen_height = DisplayController.getScreenHeight();

  pixel[0] = DisplayController.createRawPixel(RGB888(0x00,0x00,0x00)); // preto
  pixel[1] = DisplayController.createRawPixel(RGB888(0xff,0xff,0xff)); // branco
  pixel[2] = DisplayController.createRawPixel(RGB888(0x00,0xfe,0x65)); // verde
  pixel[3] = DisplayController.createRawPixel(RGB888(0xfe,0x32,0x25)); // vermelho
  pixel[4] = DisplayController.createRawPixel(RGB888(0xfe,0xfe,0x32)); // amarelo;
  pixel[5] = DisplayController.createRawPixel(RGB888(0x32,0x29,0xfe)); // azul;


  screen_clear();
  screen_bounds();

  port1_ = 0;
  port2i_ = (3 - 3) & 0x03;   // DIP switches
  port2o_ = 0;
  port3o_ = 0;
  port4lo_ = 0;
  port4hi_ = 0;
  port5o_ = 0;
  sounds_ = 0;

  load_game();

  xTaskCreatePinnedToCore(SerialTASK, "SerialTASK", 8192, NULL, 1, NULL, 0);
  xTaskCreatePinnedToCore(DACTask, "DACTask", 8192, NULL, 1, NULL, 0);

}



long next_sleep  = 0;
long frame_time  = 0;
long last_millis = 0;

void loop(){

  if(getBit(player1_ctl,7) == true) {                                          // Check if Jamma P1 BTN 2 is presset
          while(getBit(player1_ctl,7) == true) delayMicroseconds(100); // debaunce
          if(menu_active == 0){                                                      // Ok, start menuing
            cv.clear();
            screen_clear();
            menu_item = GAME;
            menu_item_last = -1;
            menu_active = 1;
            screen_bounds();
            delay(100);
          }else{                                                                     // Stop menuing
            menu_active = 0;
            screen_clear();
            GAME = menu_item;
            load_game();
          }
  }
  
  if(menu_active ==0){ // In Game
    long cycles = 0;
    if(millis() - last_millis > next_sleep){
      int frame_start = millis();
        for( int i=0; i<2; i++ ) {
            while( cycles < cycles_per_interrupt ){
                i8080_step(&cpu);
                cycles++;
            }
            if(GAME == 2 || GAME == 3) invaders_irq(&cpu, i ? 0x10 : 0x08);
            else i8080_interrupt(&cpu, i ? 0xD7 : 0xCF);
        }
      frame_time = millis() - frame_start;
      next_sleep = (1000/fps)-frame_time;
      last_millis = millis();
    }
  }else{
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
            menu_active = 0;
            screen_clear();
            GAME = menu_item;
            load_game();
          }          
  }
}
