ReuseDistancePintool
====================

Pintool to collect reuse distance profiles for multi-threaded programs.

Primary author: Derek Schuff, derek.schuff@gmail.com  
Maintainer: Abhisek Pan, abhisekpan@gmail.com.

Publications:
+ Derek L. Schuff, Milind Kulkarni, and Vijay S. Pai. 2010. Accelerating multicore reuse distance analysis with sampling and parallelization. 
  In Proceedings of the 19th international conference on Parallel architectures and compilation techniques (PACT '10). 
  ACM, New York, NY, USA, 53-64.
+ Schuff, D.L.; Parsons, B.S.; Pai, V.S., "Multicore-aware reuse distance analysis," 
  Parallel & Distributed Processing, Workshops and Phd Forum (IPDPSW), 2010 IEEE International Symposium on , pp.1,8, 19-23 April 2010.

Prerequisites
-------------
The following software is needed (in parentheses are the names of the Debian/Ubuntu packages which contain them).
+ GCC with a recent g++, and make (gcc, g++, make)
+ Several of the boost libraries (libboost-dev)

Building
--------
Executing 'make' in the PinReuseDistance directory will also call make in the rda_stacks directory. 
make -j<number> will speed up the build. 
Aside from just 'make' (which will build the pintool), you can cd into rda_stacks and build the 'unittests' target, 
which has several unit tests. You need to make clean before making the unittests, because some defines are different.

Running
-------

From the PinReuseDistance directory, do e.g. the following:

    ../../../pin -t obj-intel64/reusedistance.so -o output.txt -s shared -- ~/binaries/ft.A
where:
+ '-t' is the pintool to run (reusedistance.so is full analysis), 
+ '-o' is the output file name, '-s' is the stack type (shared or private are the options), 
+ the '--' separates the pin and pintool options  from the target binary (ft.A). Any options after the target binary will go to the binary and not to pin or the pintool.
