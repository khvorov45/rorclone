@echo off

fxc.exe /nologo /T vs_5_0 /E vs /Od /WX /Zi /Ges /Fh code/rorclone.hlsl_vs.h /Vn globalShader_vs code/rorclone.hlsl
fxc.exe /nologo /T ps_5_0 /E ps /Od /WX /Zi /Ges /Fh code/rorclone.hlsl_ps.h /Vn globalShader_ps code/rorclone.hlsl

aseprite -b data\commando.aseprite --save-as data\commando.png
aseprite -b data\lemurian.aseprite --save-as data\lemurian.png

clang code/rorclone.c -march=native -Wall -Wextra -g -o build/rorclone.exe

echo done