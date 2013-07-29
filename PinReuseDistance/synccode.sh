#!/bin/bash

#PINTOOLPATH=~/pin-2.8-33586-gcc.3.4.6-ia32_intel64-linux/source/tools/PinReuseDistance/
PINTOOLPATH=~/pin-2.8-36111-gcc.3.4.6-ia32_intel64-linux/source/tools/PinReuseDistance/
#PINTOOLPATH=~/pin-2.7-31933-gcc.3.4.6-ia32_intel64-linux/source/tools/PinReuseDistance/

PVERSION=$(git show --pretty=oneline --quiet)
#version.h:
echo "#define PINRD_GIT_VERSION \"$PVERSION\"" > version.h
cd rda_stacks
RVERSION=$(git show --pretty=oneline --quiet)
cd -
echo "#define LIBRDA_GIT_VERSION \"$RVERSION\"" > rda_stacks/src/version.h

#machines given on command line
if [[ $# -gt 0 ]]; then
    #machine=$1
    for machine in $*; do
	echo $machine
	rsync -av makefile *.c *.cc *.h *.sh $machine:$PINTOOLPATH
	rsync -av rda_stacks/src rda_stacks/Makefile $machine:$PINTOOLPATH/rda_stacks/
    done
    exit 0
fi

#machines={sdcranch06,sdcranch13,sdcranch12,sdcranch11,sdcranch10}
for machine in {sdcranch06,sdcranch13,sdcranch12,sdcranch11,sdcranch10,sdcranch09,sdcranch08}; do
    echo $machine
    rsync -av makefile *.cc *.h *.sh $machine:$PINTOOLPATH
    rsync -av rda_stacks/src rda_stacks/Makefile $machine:$PINTOOLPATH/rda_stacks/
done

exit 0

#stuff from first synccode
if [[ $# -gt 0 ]]; then
    rsync -va modules/ $1:~/csm-simics/modules/
    exit 0
fi

rsync -av modules/memstat/ sdcranch08:~/csm-simics/modules/memstat/
rsync -av modules/memstat/ sdcranch09:~/csm-simics/modules/memstat/
rsync -av modules/memstat/ sdcranch10:~/csm-simics/modules/memstat/
rsync -av modules/memstat/ sdcranch11:~/csm-simics/modules/memstat/
rsync -av modules/memstat/ sdcranch12:~/csm-simics/modules/memstat/
rsync -av modules/memstat/ sdcranch13:~/csm-simics/modules/memstat/
rsync -av modules/memstat/ sherpa10:~/csm/csm-simics/modules/memstat/

exit 0

rsync -av memtrace.cc memtracemake sdcranch06:~/csm-simics
rsync -av memtrace.cc memtracemake sdcranch09:~/csm-simics
rsync -av memtrace.cc memtracemake sdcranch08:~/csm-simics
rsync -av memtrace.cc memtracemake sdcranch10:~/csm-simics
rsync -av memtrace.cc memtracemake sdcranch11:~/csm-simics
rsync -av memtrace.cc memtracemake sdcranch12:~/csm-simics
rsync -av memtrace.cc memtracemake sdcranch13:~/csm-simics
rsync -av memtrace.cc memtracemake sherpa10:~/csm/csm-simics