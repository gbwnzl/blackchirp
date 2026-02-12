ifneq ($(KERNELRELEASE),)

# uncomment this line (and on Jetson Xavier change to correct path) if you want to build the kernel module with NVidia CUDA RDMA support
#NVIDIA_DRV_SRC :=/usr/src/nvidia-$(shell modinfo $$(find /lib/modules/$$(uname -r) -name nvidia.ko*) | grep ^version | awk '{print $$2}')/ # uncomment for RDMA on Intel/AMD x86 or NVidia Clara with dGPU. The path is something like /usr/src/nvidia-560.35.03/
#NVIDIA_DRV_SRC :=/usr/src/linux-headers-5.15.148-tegra-ubuntu22.04_aarch64/nvgpu/ # uncomment for RDMA on NVidia Jetson Xavier with iGPU
#NVIDIA_DRV_SRC :=/usr/src/nvidia/nvidia-oot/ # uncomment for RDMA on NVidia Jetson Orin with iGPU

# strip() removes leading and trailing spaces. The latter can occur because the space between -375.66/ and # is added to the variable
NVIDIA_DRV_SRC:=$(strip $(NVIDIA_DRV_SRC))

DRVNAME := spcm4
obj-m	:= $(DRVNAME).o

$(DRVNAME)-objs :=  spcm_linux_main.o\
                    spcm_linux_card.o\
                    spcm_linux_isr.o\
                    spcm_linux_debug.o\
                    ../m4i_krnl_common/sglistmem.o \
                    ../m4i_krnl_common/nwdcore.o \
                    ../m4i_krnl_common/dma-nwc.o \
                    ../m4i_krnl_common/dma-xdma.o \
                    ../m4i_krnl_common/xdmacore.o \
                    ../m2i_tools/spcm_eeprom_codes.o

ifneq (,$(wildcard /etc/rpi-issue))
    # the RPi CM5 does not report aarch64 for uname -r :(
    EXTRA_CFLAGS += -DDMA_BOUNCE_BUFFER
else
    UNAME_P := $(shell uname -p)
    ifneq ($(UNAME_P),aarch64)
        # Jetson-GCC does not support this 
        EXTRA_CFLAGS := -mno-soft-float

        # CMA not tested on Jetson
        # uncomment this line to use large continuous buffers allocated by cma.
        # usage will require root privileges for calling process!
        #EXTRA_CFLAGS += -DSPCM4_USE_CMA

        # we may need to care for Suse's API backports...
        ifneq (,$(wildcard /etc/SUSE-brand))
            EXTRA_CFLAGS += -DSUSE_VERSION=$(shell lsb-release -rs | tr -d .)
        else
            EXTRA_CFLAGS += -DSUSE_VERSION=0
        endif
    endif
endif


ifdef NVIDIA_DRV_SRC
    # for debugging purposes
    $(info Using NVidia driver from [$(NVIDIA_DRV_SRC)])

    # add object files for CUDA support
    $(DRVNAME)-objs += ../m4i_krnl_common/spcm_cuda.o

    # add include directory and flags
    ccflags-y += -I$(NVIDIA_DRV_SRC) -DUSE_CUDA_RDMA

    # we need to link against nvidia.ko on Intel/AMD or nvgpu.ko on NVidia Jetson
    ifneq ($(UNAME_P),aarch64)
        # if Module.symvers does not exist then user has to build the NVidia driver module to generate it
        ifeq (,$(wildcard $(NVIDIA_DRV_SRC)/Module.symvers))
            $(error ERROR: Module.symvers not found in $(NVIDIA_DRV_SRC). Go to $(NVIDIA_DRV_SRC) and execute make there)
        endif

        KBUILD_EXTRA_SYMBOLS := $(NVIDIA_DRV_SRC)/Module.symvers
    else
        # the iGPU driver (nvgpu) of the Jetson uses a different interface for RDMA than the "normal" nvidia driver
        ifdef JETSON_IGPU
            EXTRA_CFLAGS := -DJETSON_INTEGRATED_GPU
        endif
        ifdef JETSON_ORIN
            # the Orin places its includes in a subfolder
            ccflags-y += -I$(NVIDIA_DRV_SRC)/include/
        endif

        # use symvers file to link against nvidia driver
        ifneq (,$(wildcard $(src)/Module-nv.symvers))
		    KBUILD_EXTRA_SYMBOLS := $(src)/Module-nv.symvers
        endif
    endif

endif #NVIDIA_DRV_SRC

else
    KDIR := /lib/modules/$(shell uname -r)/build
    KDIRUSR := /usr/lib/modules/$(shell uname -r)/build
    CUR_PWD  := $(shell pwd)
    ifndef CUR_PWD
        CUR_PWD = $(PWD)
    endif

default:
	if [ -d /lib/modules/$(shell uname -r)/build ]; then \
		$(MAKE) -C $(KDIR) M=$(CUR_PWD) modules; \
	else \
		$(MAKE) -C $(KDIRUSR) M=$(CUR_PWD) modules; \
	fi

jetson:
	# older Jetson AGX Xavier use nvgpu.ko
	#bash nvgpu-ko-to-module-symvers.sh /lib/modules/$(shell uname -r)/kernel/drivers/gpu/nvgpu/nvgpu.ko Module-nv.symvers;
	# newer Jetson AGX Xavier use nvidia-p2p.ko
	bash nvgpu-ko-to-module-symvers.sh /lib/modules/$(shell uname -r)/kernel/drivers/nv-p2p/nvidia-p2p.ko Module-nv.symvers;
	$(MAKE) -C $(KDIR) M=$(CUR_PWD) JETSON_IGPU=1 modules;

clara:
	bash nvgpu-ko-to-module-symvers.sh /lib/modules/$(shell uname -r)/updates/dkms/nvidia.ko Module-nv.symvers;
	$(MAKE) -C $(KDIR) M=$(CUR_PWD) modules;

orin:
	# Jetson Orin uses nvidia-p2p.ko, but in different location than Xavier
	bash nvgpu-ko-to-module-symvers.sh /lib/modules/$(shell uname -r)/updates/drivers/nv-p2p/nvidia-p2p.ko Module-nv.symvers;
	$(MAKE) -C $(KDIR) M=$(CUR_PWD) JETSON_IGPU=1 JETSON_ORIN=1 modules;

endif

install:
	cp spcm4.ko /lib/modules/$(shell uname -r)/kernel/drivers/
	depmod -a

clean:
	rm -rf *.o *~ core .depend .*.cmd *.ko *.mod.c .tmp_versions *.symvers *.order *.markers *.mod
	rm -f ../m2i_tools/*.o ../m2i_tools/.*.cmd
	rm -f ../m4i_krnl_common/*.o ../m4i_krnl_common/.*.cmd
	rm -f Module.symvers Module-nv.symvers
