# TDBABootleg: Main code for the "INVADERS" Hardware  
  
Taito do Brasil Arcade Bootleg v1.2  
Nino MegaDriver - nino@nino.com.br  
License GPL v3.0  
  
Maincode for the ESP32 Devkit V1 module. Code based on the "Space Invaders" arcade board manufactured by Taito do Brasil  
  
Most of this code was ported from the very fast and reliable emulator
["SIDE - Space Invaders Didactic Emulator"](https://walkofmind.com/programming/side/side.htm) by Alessandro Scotti.  
  
This is still in a work-in-progress state. However we have this currently playable games:  
  
- Galactica - Batalha Espacial.
- Indian Battle.
- Space Invaders.
- Space Invaders Part II.
- Polaris
  
__
Libraries used:  
  
- FabGL (http://www.fabglib.org/) for video generation.  
- DacAudio by https://www.xtronical.com/  
  
Special Thanks:  
  
- Alessandro Scotti for his fast an reliable Space Invaders emulator which provided most of the routines used here.
- All MAME developers for providing tons of references and info in the MAME sources.  
- Fabrizio Di Vittorio for his amazing FabGL graphics library which made the video implementation in project really easy.  
- Emerson de Holanda from "Arcade Solutions" for overall testing and supporting the project.  
  
Building Instructions:  
  
I strogly recommend using this [sdkconfig](https://raw.githubusercontent.com/ninomegadriver/TDBABootleg/main/ESP32_HW_Fantastic/sdkconfig) with optimal settings for this project.  
  
  
See a WIP video here:  
[![Taito Do Brasil Bootleg v1.2 - Invaders Hardware (WIP)](https://img.youtube.com/vi/LioPVbTkof8/0.jpg)](https://www.youtube.com/watch?v=LioPVbTkof8)  
  
  
