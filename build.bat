@echo off

clang code/build.c -std=c2x -march=native -Wall -Wextra -g -o build/build.exe && build\build.exe

fxc.exe /nologo /T vs_5_0 /E vs_sprite /Od /WX /Zi /Ges /Fo data/rorclone.hlsl_vs_sprite.bin code/rorclone.hlsl
fxc.exe /nologo /T vs_5_0 /E vs_screen /Od /WX /Zi /Ges /Fo data/rorclone.hlsl_vs_screen.bin code/rorclone.hlsl
fxc.exe /nologo /T ps_5_0 /E ps_sprite /Od /WX /Zi /Ges /Fo data/rorclone.hlsl_ps_sprite.bin code/rorclone.hlsl
fxc.exe /nologo /T ps_5_0 /E ps_screen /Od /WX /Zi /Ges /Fo data/rorclone.hlsl_ps_screen.bin code/rorclone.hlsl

clang code/rorclone.c -std=c2x -march=native -Wall -Wextra -g -o build/rorclone.exe

echo done