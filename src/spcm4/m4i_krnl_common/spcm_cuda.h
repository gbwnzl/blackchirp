#ifndef SPCM_CUDA_H
#define SPCM_CUDA_H

#include <linux/types.h>

#ifdef JETSON_INTEGRATED_GPU
    // NVidia Jetson
#   include <linux/nv-p2p.h>
#   define GPU_BOUND_SHIFT  12
#else
#   include <nvidia/nv-p2p.h>
#   define GPU_BOUND_SHIFT  16
#endif

#define GPU_BOUND_SIZE   ((u64)1 << GPU_BOUND_SHIFT)
#define GPU_BOUND_OFFSET (GPU_BOUND_SIZE - 1)
#define GPU_BOUND_MASK   (~GPU_BOUND_OFFSET)

void vCudaCallback (void* pvData);

#endif // SPCM_CUDA_H

