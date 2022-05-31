# Taito do Brasil Arcade Bootleg  
**Jamma arcade board based on the original "Fantastic" hardware by Taito do Brasil**  
Nino MegaDriver - nino@nino.com.br  
License GPL v3.0  
***************** 
## Main Features  

- ESP32 Wroom Devkit V1 which functions as CPU and graphics generator.  
- Two AY-3-9010 PSGs
- Serial data bus with three Atmega328p to handle controller inputs and driving the PSGs.  
- Audio mixing between PSGs and 2 DACs which can be activated via dip switches.    
  
**Actual Board Picture:**  
![acual board picture](https://github.com/ninomegadriver/TDBABootleg/blob/main/Actual-Board-Picture.jpg?raw=true)  
  
*****************
## PCB Gerbers  
**The board is released as "open hardware" under the GPL 3.0.  
[Gerbers for production is available here.](https://github.com/ninomegadriver/TDBABootleg/tree/main/PCB_Gerbers)**  
  
**PCB Footprints:**  
![PCB Top](https://github.com/ninomegadriver/TDBABootleg/blob/main/PCB-top.png?raw=true)  
![PCB Bottom](https://github.com/ninomegadriver/TDBABootleg/blob/main/PCB-bottom.png?raw=true)  
  
***************
## Serial Protocol  
  
This project uses standard UART serial protocol at 115200 baud rate. One Atmega328p is used to poll player inputs and sends the data over serial. Two Atmega328p listens on serial for instructions to drive its own AY-3-8910 PSG.  

The controllers MCU sends two bytes, one for each player as follows:  
```
  0b00000001 => COIN;
  0b00000010 =>  START;
  0b00000100 =>  UP;
  0b00001000 =>  DOWN;
  0b00010000 =>  LEFT;
  0b00100000 =>  RIGHT;
  0b01000000 =>  BT1;
  0b10000000 =>  BT2;
```  
  
Each AY-3-8910 Atmega328p drivers has it's own #id hardcoded, data is sent as three bytes structured as:  
```
struct YMREG{
  uint8_t chip;
  uint8_t reg;
  uint8_t data;
  bool dirty = false;
};
(...)
      buf[0] = YMBUFF[ym_flush_pos].chip;
      buf[1] = YMBUFF[ym_flush_pos].reg;
      buf[2] = YMBUFF[ym_flush_pos].data;
      Serial.write(buf, 3);
      Serial.flush();
(...)
```  
The data is always received by the two drivers, but it's is only processed if it's flagged with its #id.  
  
***************************
## Supported Games  
  
This project was initially focused on the "Fantastic", an arcade "Galaxian" licensed game clone, developed by Taito do Brasil in 1981. It all started as a couple of spare boards donated to me. Later on, an original rack came in my shop for repair, providing me a real hardware to use as source of actual information. So, the initial goal was to run a Z80 emulator on an ESP32 but using original hardware for audio, the same configuration used on the Fantastic racks.  
  
As of today, we have the following fully playable games:  
- Fantastic, 1981.  
- Gorila, 1981.  
  
**See it in action here:**  
[![Taito Do Brasil Bootleg v1.2 - Fantastic Hardware](https://img.youtube.com/vi/sqsus4Gnz_k/0.jpg)](https://www.youtube.com/watch?v=sqsus4Gnz_k)  
  
The ESP32 should be powerful enough to run many more classic and single processed arcade games. A work-in-progress is ongoing for supporting more Taito do Brasil games that run on Space Invaders hardware, such as:  
  
- Galactica - Batalha Espacial.
- Indian Battle.
- Space Invaders.
- Space Invaders Part II.
- Polaris.
  
**Work-in-progress of the Invaders hardware here:**  
[![Taito Do Brasil Bootleg v1.2 - Invaders Hardware (WIP)](https://img.youtube.com/vi/LioPVbTkof8/0.jpg)](https://www.youtube.com/watch?v=LioPVbTkof8)  

***************************
## Special Thanks  
  
- Tom Walker and his emulator YAAME (http://yaame.emuunlim.com/) which served as a base for the graphics routines used in the [ESP32_HW_Fantastic](https://github.com/ninomegadriver/TDBABootleg/tree/main/ESP32_HW_Fantastic) code.
- Alessandro Scotti for his fast an reliable [Space Invaders emulator](https://walkofmind.com/programming/side/side.htm) which provided most of the routines used in the Invaders Hardware code.
- All MAME developers for providing tons of references and info in the MAME sources.  
- Fabrizio Di Vittorio for his amazing FabGL graphics library which made the video implementation in project really easy.  
- Alexsander dos Santos, which kindly donated a few Fantastic PCBs that served as a sparkling for this project. 
- Luis Culik from the "Museu do Pinball de Itu" for entrusting me a very rare and unique original Fantastic rack for repair, which also served as source of information on the actual original hardware.  
- Emerson de Holanda from "Arcade Solutions" for overall supporting the project.    
  
***************************
Comments, suggestions, support and help are always welcome.  
Wanna join in or just chat?  
You can reach me at:  
nino@nino.com.br  
[Facebook "ninomegadriver"](https://facebook.com/ninomegadriver)  

