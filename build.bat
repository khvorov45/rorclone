@echo off

fxc.exe /nologo /T vs_5_0 /E vs /Od /WX /Zi /Ges /Fo data/rorclone_sprite.hlsl_vs.bin code/rorclone_sprite.hlsl
fxc.exe /nologo /T ps_5_0 /E ps /Od /WX /Zi /Ges /Fo data/rorclone_sprite.hlsl_ps.bin code/rorclone_sprite.hlsl

fxc.exe /nologo /T vs_5_0 /E vs /Od /WX /Zi /Ges /Fo data/rorclone_screen.hlsl_vs.bin code/rorclone_screen.hlsl
fxc.exe /nologo /T ps_5_0 /E ps /Od /WX /Zi /Ges /Fo data/rorclone_screen.hlsl_ps.bin code/rorclone_screen.hlsl

clang code/rorclone.c -std=c2x -march=native -Wall -Wextra -g -o build/rorclone.exe

echo done