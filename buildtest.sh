#!/bin/bash
#gcc -Wall -o tltest src/tltest.c -Iinc -Lsrc/.libs -ltasklist -lm -ldl
gcc -g -Wall -o test src/tltest.c -I../out/include -L../out/lib -llog -Iinc -Lsrc/.libs -ltasklist -lm -lpthread -ldl -Wl,-rpath,src/.libs -Wl,-rpath,../out/lib
