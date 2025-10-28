@echo off
cd luajit\src
call msvcbuild.bat

copy /B lua.h ..\build\include
copy /B luaconf.h ..\build\include
copy /B lualib.h ..\build\include
copy /B lauxlib.h ..\build\include
copy /B lua.hpp ..\build\include

copy /B lua51.lib ..\build\lib
copy /B luajit.lib ..\build\lib
copy /B luajit.pdb ..\build\lib
