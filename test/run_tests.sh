#!/bin/bash -v
set -e

if [ "$#" -ne 2 ]; then
    echo "Usage: run_tests.sh <installation directory> <platform string, e.g. Linux, Android, RaspberryPi, Windows>"
    exit -1
fi

. $1/env.sh

# Set to a semicolon-separated list of tests to be skipped
skipped_tests="android"
# Set to test in order to run just that one
only_test=""
if [ "$2" == "Android" ] 
then
  skipped_tests=""
  only_test="android"
fi

if [ "$2" == "RaspberryPi" ] 
then
  skipped_tests=""
  only_test="dummy"
fi

for reg_test in *; do
    if [ ! -d $reg_test ]; then
      continue;
    fi
    if [ ! -z $only_test ] && [ $only_test != "$reg_test" ]; then
      continue;
    fi

    if [[ $skipped_tests == *"$reg_test"* ]]; then
      continue;
    fi
    cd $reg_test
    echo "############################ Starting $reg_test ##########################"
    time ./$reg_test.test
    if [ $? -ne 0 ]
    then
        echo "############################ Failed $reg_test ##########################"
        exit 1
    fi
    echo "############################ Completed $reg_test ##########################"
    cd ..
done
