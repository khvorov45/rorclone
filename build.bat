@echo off

clang code/build.c -std=c2x -march=native -Wall -Wextra -g -o build/build.exe && build\build.exe

clang code/rorclone.c -std=c2x -march=native -Wall -Wextra -g -o build/rorclone.exe

echo done