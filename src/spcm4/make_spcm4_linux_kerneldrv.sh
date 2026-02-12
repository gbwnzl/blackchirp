#!/bin/bash

# *************************************
# Generation of kernel driver
# module for spcm4 Spectrum cards
#                     (c) Spectrum GmbH
# *************************************



# ***** error check function *****
error_exit()
    {
    echo
    echo "************************************************************"
    echo Error: $1
    echo "************************************************************"
    echo
    exit 1
    }

check_error()
    {
    if [ $? != 0 ]; then
        error_exit "$1"
    fi
    }



# *************************************
# ***** main function starts here *****
# *************************************

# this script requires root privileges
if [ ! `id -u` -eq 0 ]; then
    error_exit "This script requires root privileges."
fi

# ***** spaces in path can cause errors during build *****
case `pwd` in
    *\ * )
        error_exit "Spaces in path can cause build errors"
        ;;
esac

# compilation requires a gcc
command -v gcc >/dev/null
if [ ! $?  -eq 0 ]; then
    error_exit "gcc not found. It can normally be installed using the package management software of your distribution."
fi

echo
echo

KERNEL_VERSION=`uname -r`
KERNEL_MODULE_NAME=spcm4
KERNEL_NAME=${KERNEL_MODULE_NAME}.ko
CPU_ARCH=$(uname -p)

pushd m4i_krnl_linux >/dev/null

# ----- compile and copy the driver -----
make clean
if [ ${CPU_ARCH} = "aarch64" ]; then
    if ! command -v nvidia-smi 2>&1 >/dev/null; then
        echo -e "\nBuilding for Jetson Xavier\n"
        make jetson
    else
        PRODUCT_NAME=$(nvidia-smi -q | grep "Product Name" | cut -d ":" -f 2)
        if [[ ${PRODUCT_NAME} = *"Orin"* ]]; then
            echo -e "\nBuilding for Jetson Orin\n"
            make orin
        elif [[ ${PRODUCT_NAME} = *"Quadro"* ]]; then
            echo -e "\nBuilding for Clara\n"
            make clara
        elif [[ ${PRODUCT_NAME} = *"Xavier"* ]]; then
            echo -e "\nBuilding for Jetson Xavier\n"
            make jetson
        fi
    fi
else
    make
fi
check_error "compilation of kernel driver module failed. Please check whether kernel source are installed!"

# ----- copy driver module to kernel directory to enable automatic loading at system start -----
echo
echo -n "Copying kernel module... "
cp $KERNEL_NAME /lib/modules/$KERNEL_VERSION/kernel/drivers/
check_error "copy of kernel driver module failed"
echo "Done."
popd >/dev/null

# ----- update kernel module dependencies to allow usage of modprobe to load module -----
echo -n "Updating kernel module dependencies... "
depmod -a $KERNEL_VERSION
check_error "update of kernel driver module dependencies failed"
echo "Done."

# ----- udev rules -----
echo -n "Creating udev rules file... "
cp scripts/99-spcm4.rules /etc/udev/rules.d/
echo "Done"

# ----- copy library -----
echo -n "Installing library... "

if [ ${CPU_ARCH} = "aarch64" ]; then
    # we support only Ubuntu on Jetson, so we can use the package
    dpkg -i ./libs/libspcm-linux*.arm64.deb
    check_error "installation of driver library package failed"
else

    # ----- see which system is used 32 or 64 bit -----
    CPU_TYPE=`uname -m`
    case ${CPU_TYPE} in
        i386)   SYSTEM_WIDTH="32bit";;
        i486)   SYSTEM_WIDTH="32bit";;
        i586)   SYSTEM_WIDTH="32bit";;
        i686)   SYSTEM_WIDTH="32bit";;
        x86_64) SYSTEM_WIDTH="64bit";;
        amd64)  SYSTEM_WIDTH="64bit";;
        *) error_exit "CPU type "${CPU_TYPE}" not supported yet";;
    esac

    # ----- examine which stdc++ version is installed to select the correct library -----
    if [ ${SYSTEM_WIDTH} = "32bit" ]; then
        for p in /usr/lib/i386-linux-gnu /usr/lib; do
            FOUND=$(find $p -name "libstdc++.so.6" 2>/dev/null | wc -l)
            if [ $FOUND -gt 0 ]; then
                LIBPATH=$p
                STDLIB_VERS="stdc++6"
                break;
            fi
            FOUND=$(find $p -name "libstdc++.so.5" 2>/dev/null | wc -l)
            if [ $FOUND -gt 0 ]; then
                LIBPATH=$p
                STDLIB_VERS="stdc++5"
                break;
            fi
        done
    else
        for p in /usr/lib/x86_64-linux-gnu /usr/lib64 /usr/lib; do
            FOUND=$(find $p -name "libstdc++.so.6" 2>/dev/null | wc -l)
            if [ $FOUND -gt 0 ]; then
                LIBPATH=$p
                STDLIB_VERS="stdc++6"
                break;
            fi
            FOUND=$(find $p -name "libstdc++.so.5" 2>/dev/null | wc -l)
            if [ $FOUND -gt 0 ]; then
                LIBPATH=$p
                STDLIB_VERS="stdc++5"
                break;
            fi
        done
    fi
    if [ "$LIBPATH" = "" ]; then
        error_exit "Path of libstdc++.so not found"
    fi

    LIB_NAME="spcm_linux_"${SYSTEM_WIDTH}"_"${STDLIB_VERS}".so"

    # copy library
    cp -f ./libs/${LIB_NAME} ${LIBPATH}/libspcm_linux.so 2> /dev/null
    check_error "copy of library "${LIB_NAME}" failed"
fi
echo "Done."

# ----- load module -----
echo -n "Loading module... "
modprobe $KERNEL_MODULE_NAME
if [ $? != 0 ]; then
    echo "Failed."
    echo
    echo "Output of dmesg:"
    dmesg | tail -n 10
    echo
    echo "WARNING: Installation of kernel driver module was successful but loading failed."
    echo
    echo "This is normal if card is not yet installed in the system."
    echo "The driver will be loaded automatically at each system start when the card has been installed."
    exit 1
fi
echo "Done."

echo
echo "Kernel driver module successfully installed and loaded."
echo "The driver will be loaded automatically at each system start."
echo

exit 0

