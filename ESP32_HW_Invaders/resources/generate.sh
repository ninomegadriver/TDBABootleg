#!/bin/bash

rm -rf ../res.hpp 2>/dev/null >/dev/null
for i in menu*
do
#  convert $i -rotate -90 $i
  ./img2fabgl $i -d >>../res.hpp
done
