@echo off

fxc.exe /nologo /T vs_5_0 /E vs_sprite /Od /WX /Zi /Ges /Fo data/rorclone.hlsl_vs_sprite.bin code/rorclone.hlsl
fxc.exe /nologo /T vs_5_0 /E vs_screen /Od /WX /Zi /Ges /Fo data/rorclone.hlsl_vs_screen.bin code/rorclone.hlsl
fxc.exe /nologo /T ps_5_0 /E ps /Od /WX /Zi /Ges /Fo data/rorclone.hlsl_ps.bin code/rorclone.hlsl

clang code/rorclone.c -std=c2x -march=native -Wall -Wextra -g -o build/rorclone.exe

echo done