#!/bin/bash

if [ ! -e CMakeLists.txt ]; then
	echo "Run this script from main project directory: ./tests/do_tests.sh"
	exit 2
fi

echo "This command can modify your state! We will move your"
echo '~/.otpasswd file into ~/.otpasswd_copy and then move it back.'
echo "But we can't help if you have configured global state."
echo "Also testing coverage is best done with generic config file."
echo 'We will also remove two directories: ./lcov and ./gcov.'
echo
echo 'Starting in 10 seconds'
sleep 9
echo Starting...
sleep 1

mv ~/.otpasswd ~/.otpasswd_copy
rm -rf lcov gcov

make clean
rm -rf CMakeFiles CMakeCache.txt 
cmake -DDEBUG=1 -DPROFILE=1 . || (echo "Config failed"; exit 1)
make || (echo "Build failed"; exit 1)

#echo "Failed create testcase...\n"
#yes no | ./otpasswd -v -k && (echo "*** WARNING Test which should failed succedded!")
#yes yes | ./otpasswd -v -k || (echo "*** KEY CREATE TEST FAILED")

# This should run --check atleast once and create new state.
make test 

# GCOV version:

# mkdir gcov; cd gcov
#echo "Generating .gcov files"
# Generate .gcov files for all .c files in project (except for pam)
#for i in $(find '../CMakeFiles' -iname "*.gcda"); do b=$(basename $i); echo $b; d=$(dirname $i); gcov -o $d $i; done 

# LCOV version:
mkdir lcov
lcov --directory . --capture --output-file otpasswd.info --test-name OTPasswdCoverage
genhtml --prefix . --output-directory lcov/ --title "OTPasswd coverage test" --show-details otpasswd.info



# Restore state
mv ~/.otpasswd_copy ~/.otpasswd
