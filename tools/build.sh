#!/bin/sh
echo Cleaning...
make clean
rm -rf ./CMakeFiles ./CMakeCache.txt CTestTestfile.cmake Makefile
echo OK
sleep 1
echo Configuring...
cmake -DDEBUG=1 .
make 