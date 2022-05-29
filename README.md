# TDBABootleg
Taito do Brasil Arcade Bootleg  
  
I strongly suggest compilling the FabGL library used in this project with the GCC optimization flag "-O3"
  
To do so, one suggestion is to enter the $/Arduino/libraries/FabGL folder an use awk to replace "-O2" with "-O3"  
  
```
for i in `find -name "*.cpp"`; do sed -i 's/"O2"/"O3"/g' $i; done;
```
  

