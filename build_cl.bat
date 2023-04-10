@echo off

rd /S /Q out
mkdir out
pushd out

cl /nologo /W4 /std:c++20 /EHsc /O2 ..\main.cc

popd
