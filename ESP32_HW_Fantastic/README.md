# TDBABootleg: Main code for the "FANTASTIC" Hardware  
  
Taito do Brasil Arcade Bootleg v1.2  
Nino MegaDriver - nino@nino.com.br  
License GPL v3.0  
  
Maincode for the ESP32 Devkit V1 module. Code based on the "FANTASTIC" arcade board manufactured by Taito do Brasil, which was itself based on the original Galaxian rack by Namco.  
  
Currently fully playable games:  
  
- Fantastic
- Gorila

Libraries used:  
  
- FabGL (http://www.fabglib.org/) for video generation.  
- DacAudio by https://www.xtronical.com/  
- Z80 emulator from Lin Ke-Fong.  
  
Special Thanks:  
  
- Tom Walker and his emulator YAAME (http://yaame.emuunlim.com/) which served as a base for the graphics routines used in this code.
- All MAME developers for providing tons of references and info in the MAME sources.  
- Fabrizio Di Vittorio for his amazing FabGL graphics library which made the video implementation in project really easy.  
- Alexsander dos Santos, which kindly donated a few Fantastic PCBs that served as a sparkling for this project. 
- Luis Culik from the "Museu do Pinball de Itu" for entrusting me a very rare and unique original Fantastic rack for repair, which also served as source of information on the actual original hardware.  
- Emerson de Holanda from "Arcade Solutions" for overall testing and supporting the project.  
  
Building Instructions:  
  
Download and install FabGL into your Arduino IDE and load the project. I strongly recommend modifying all FabGL sources to use the GCC "-O3" optimization flag as it will result on a huge increase of performance. On Linuxes, one suggestion is to enter the $/Arduino/libraries/FabGL folder and use "sed" to replace "-O2" with "-O3" like this:   
  
```
for i in `find -name "*.cpp"`; do sed -i 's/"O2"/"O3"/g' $i; done;
```  
  
I also recommend using this [sdkconfig](https://raw.githubusercontent.com/ninomegadriver/TDBABootleg/main/ESP32_HW_Fantastic/sdkconfig) with optimal settings for this project.  
  
  
See it in action here:  
[![Taito Do Brasil Bootleg v1.2 - Fantastic Hardware](https://img.youtube.com/vi/sqsus4Gnz_k/0.jpg)](https://www.youtube.com/watch?v=sqsus4Gnz_k)  
  
  
