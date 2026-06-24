#
# OpenBOR - https://www.chronocrash.com
# -----------------------------------------------------------------------
# Licensed under the BSD license, see LICENSE in OpenBOR root for details.
#
# Copyright (c)  OpenBOR Team
#

#!/bin/bash
# Environments for Specific HOST_PLATFORMs
# environ.sh by SX (SumolX@gmail.com)

export BUILDBATCH=1
export HOST_PLATFORM=$(uname -s)
export MACHINENAME=$(uname -m)
export TOOLS=../tools/bin:../tools/7-Zip:../tools/svn/bin

if [ `echo $MACHINENAME | grep -o "ppc64"` ]; then
  export MACHINE=__ppc__
elif [ `echo $MACHINENAME | grep -o "powerpc"` ]; then
  export MACHINE=__powerpc__
elif [ `echo $MACHINENAME | grep -o "M680*[0-9]0"` ]; then
  export MACHINE=__${MACHINENAME}__
elif [ `echo $MACHINENAME | grep -o "i*[0-9]86"` ]; then
  export MACHINE=__${MACHINENAME%%-*}__
elif [ `echo $MACHINENAME | grep -o "x86"` ]; then
  export MACHINE=__${MACHINENAME%%-*}__
fi

case $1 in

############################################################################
#                                                                          #
#                           Linux Environment                              #
#                                                                          #
############################################################################
4)
   if test -e "/usr/local/i386-linux-4.1.1"; then
     export LNXDEV=/usr/local/i386-linux-4.1.1/bin
     export SDKPATH=/usr/local/i386-linux-4.1.1
     export PREFIX=i386-linux-
     export PATH=$LNXDEV:$PATH
     export GCC_TARGET=`i386-linux-gcc -dumpmachine`
   elif [ -z "${GCC_TARGET+xxx}" ] || [ `gcc -dumpmachine | grep -o "$GCC_TARGET"` ]; then
     export GCC_TARGET=`gcc -dumpmachine`
     export LNXDEV=`dirname \`which gcc\``
     export PREFIX=
     export SDKPATH=$LNXDEV/..
   elif [ `ls \`echo $PATH | sed 'y/:/ /'\` | grep -o "$GCC_TARGET-gcc" | tail -n 1` ]; then
     export TARGET_CC_NAME=`ls \`echo $PATH | sed 'y/:/ /'\` | grep -o "$GCC_TARGET-gcc" | tail -n 1`
     export TARGET_CC=`which $TARGET_CC_NAME`
     export GCC_TARGET=`$TARGET_CC -dumpmachine`
     export LNXDEV=`dirname $TARGET_CC`
     export PREFIX=`echo $TARGET_CC_NAME | sed 's/gcc$//'`
     export SDKPATH=$LNXDEV/..
   fi
   if test $LNXDEV; then
     echo "-------------------------------------------------------"
     echo "   Linux $TARGET_ARCH SDK ($GCC_TARGET) Environment Loaded!"
     echo "-------------------------------------------------------"
   else
     echo "-------------------------------------------------------"
     echo "     ERROR - Linux $TARGET_ARCH Environment Failed"
     echo "                 SDK Installed?"
     echo "-------------------------------------------------------"
   fi
   ;;

############################################################################
#                                                                          #
#                           Windows Environment                            #
#                                                                          #
############################################################################
5)
   if test -e "/usr/bin/i686-w64-mingw32-gcc"; then
     export WINDEV=/usr/bin
     export SDKPATH=/usr/lib/gcc/i686-w64-mingw32
     export PREFIX=i686-w64-mingw32-
     export PATH=$WINDEV:$PATH
     export CROSSCOMPILE_LINUX_WIN=1
   elif test -e "/usr/i586-mingw32msvc"; then
     export WINDEV=/usr/bin
     export SDKPATH=/usr/i586-mingw32msvc
     export PREFIX=i586-mingw32msvc-
     export PATH=$WINDEV:$PATH
   elif test -e "/usr/i686-w64-mingw32"; then
     export WINDEV=/usr/bin
     export SDKPATH=/usr/i686-w64-mingw32
     export PREFIX=i686-w64-mingw32-
     export PATH=$WINDEV:$PATH
   elif test -e "/usr/local/i386-mingw32-3.4.5"; then
     export WINDEV=/usr/local/i386-mingw32-3.4.5/bin
     export SDKPATH=/usr/local/i386-mingw32-3.4.5
     export PREFIX=i386-mingw32-
     export PATH=$WINDEV:$PATH
   elif test -e "/usr/local/i386-mingw32-4.3.0"; then
     export WINDEV=/usr/local/i386-mingw32-4.3.0/bin
     export SDKPATH=/usr/local/i386-mingw32-4.3.0
     export PREFIX=i386-mingw32-
     export PATH=$WINDEV:$PATH
   elif test -e "c:/mingw"; then
     export WINDEV=c:/mingw/bin
     export SDKPATH=c:/mingw
     export EXTENSION=.exe
     export PATH=$TOOLS:$WINDEV:$PATH
   elif [ `echo $HOST_PLATFORM | grep -E "windows|CYGWIN"` ]; then
   
     if [ ! -f "../tools/win-sdk/win-sdk.7z" ]; then
		echo "-------------------------------------------------------"
		echo "      Windows SDK File - Not Found, Downloading SDK!"
		echo "-------------------------------------------------------"
		wget https://github.com/DCurrent/openbor/raw/ecce29b95700468aa3401915625dac2d56e4ca60/tools/win-sdk/win-sdk.7z -O ../tools/win-sdk/win-sdk.7z
		echo
		echo "-------------------------------------------------------"
		echo "      Windows SDK File - Downloading Has Completed!"
		echo "-------------------------------------------------------"
     fi
   
     if [ ! -d "../tools/win-sdk/bin" ]; then
       echo "-------------------------------------------------------"
       echo "      Windows SDK - Not Found, Installing SDK!"
       echo "-------------------------------------------------------"
       ../tools/7-Zip/7za.exe x -y ../tools/win-sdk/win-sdk.7z -o../tools/win-sdk/
       echo
       echo "-------------------------------------------------------"
       echo "      Windows SDK - Installation Has Completed!"
       echo "-------------------------------------------------------"
     fi
     export WINDEV=../tools/win-sdk/bin
     export SDKPATH=../tools/win-sdk
     export EXTENSION=.exe
     export PATH=$TOOLS:$WINDEV
     HOST_PLATFORM="SVN";
   fi
   if test $WINDEV; then
       echo "-------------------------------------------------------"
       echo "     Windows SDK ($HOST_PLATFORM) $MACHINENAME Environment Loaded!"
       echo "-------------------------------------------------------"
   else
       echo "-------------------------------------------------------"
       echo "          ERROR - Windows Environment Failed"
       echo "                   SDK Installed?"
       echo "-------------------------------------------------------"
   fi
   ;;

############################################################################
#                                                                          #
#                          Darwin Environment                              #
#                                                                          #
############################################################################
10)
   if test -e "/opt/mac"; then
     export DWNDEV=/opt/mac
     export SDKPATH=$DWNDEV/SDKs/MacOSX10.4u.sdk
     export PREFIX=i686-apple-darwin8-
     export PATH=$PATH:$DWNDEV/bin
   elif test -e "/sw/bin"; then
     export DWNDEV=/sw
     export SDKPATH=/Developer/SDKs/MacOSX10.6.sdk
     export PATH=$PATH:$DWNDEV/bin
   elif test -e "/opt/local/bin"; then
     export DWNDEV=/opt/local
     if test -e "/Applications/Xcode.app/Contents/Developer"; then
       export SDKPATH=/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX10.8.sdk
     else
       export SDKPATH=/Developer/SDKs/MacOSX10.6.sdk
     fi
     export PATH=$PATH:DWNDEV/bin
   fi
   if test $DWNDEV; then
     echo "-------------------------------------------------------"
     echo "        Darwin SDK $MACHINENAME Environment Loaded!"
     echo "-------------------------------------------------------"
   fi
   ;;

############################################################################
#                                                                          #
#                             Wrong value?                                 #
#                                                                          #
############################################################################
*)
   echo
   echo "-------------------------------------------------------"
   echo "   2 = (Not Used)"
   echo "   4 = Linux"
   echo "   5 = Windows"
   echo "  10 = Darwin"
   echo "-------------------------------------------------------"
   echo
   ;;

esac
