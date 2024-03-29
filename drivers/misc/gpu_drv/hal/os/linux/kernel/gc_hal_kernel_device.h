/****************************************************************************
*
*    Copyright (c) 2005 - 2012 by Vivante Corp.  All rights reserved.
*
*    The material in this file is confidential and contains trade secrets
*    of Vivante Corporation. This is proprietary information owned by
*    Vivante Corporation. No part of this work may be disclosed,
*    reproduced, copied, transmitted, or used in any way for any purpose,
*    without the express written permission of Vivante Corporation.
*
*****************************************************************************/




#ifndef __gc_hal_kernel_device_h_
#define __gc_hal_kernel_device_h_

#ifdef ANDROID
#define gcdkREPORT_VIDMEM_LEAK      0
#else
#define gcdkREPORT_VIDMEM_LEAK      1
#endif

/******************************************************************************\
******************************* gckGALDEVICE Structure *******************************
\******************************************************************************/

typedef struct _gckGALDEVICE
{
    /* Objects. */
    gckOS               os;
    gckKERNEL           kernels[gcdCORE_COUNT];

    /* Attributes. */
    gctSIZE_T           internalSize;
    gctPHYS_ADDR        internalPhysical;
    gctPOINTER          internalLogical;
    gckVIDMEM           internalVidMem;
    gctSIZE_T           externalSize;
    gctPHYS_ADDR        externalPhysical;
    gctPOINTER          externalLogical;
    gckVIDMEM           externalVidMem;
    gckVIDMEM           contiguousVidMem;
    gctPOINTER          contiguousBase;
    gctPHYS_ADDR        contiguousPhysical;
    gctSIZE_T           contiguousSize;
    gctBOOL             contiguousMapped;
    gctPOINTER          contiguousMappedUser;
    gctSIZE_T           systemMemorySize;
    gctUINT32           systemMemoryBaseAddress;
    gctPOINTER          registerBases[gcdCORE_COUNT];
    gctSIZE_T           registerSizes[gcdCORE_COUNT];
    gctUINT32           baseAddress;
    gctUINT32           requestedRegisterMemBases[gcdCORE_COUNT];
    gctSIZE_T           requestedRegisterMemSizes[gcdCORE_COUNT];
    gctUINT32           requestedContiguousBase;
    gctSIZE_T           requestedContiguousSize;

    /* IRQ management. */
    gctINT              irqLines[gcdCORE_COUNT];
    gctBOOL             isrInitializeds[gcdCORE_COUNT];
    gctBOOL             dataReadys[gcdCORE_COUNT];

    /* Thread management. */
    struct task_struct  *threadCtxts[gcdCORE_COUNT];
    struct semaphore    semas[gcdCORE_COUNT];
    gctBOOL             threadInitializeds[gcdCORE_COUNT];
    gctBOOL             killThread;

    /* Signal management. */
    gctINT              signal;

    /* Core mapping */
    gceCORE             coreMapping[8];

    /* States before suspend. */
    gceCHIPPOWERSTATE   statesStored[gcdCORE_COUNT];

#if gcdPOWEROFF_TIMEOUT
    struct task_struct  *pmThreadCtxts[gcdCORE_COUNT];
    gctBOOL             pmThreadInitializeds[gcdCORE_COUNT];
#endif
}
* gckGALDEVICE;

typedef struct _gcsHAL_PRIVATE_DATA
{
    gckGALDEVICE        device;
    gctPOINTER          mappedMemory;
    gctPOINTER          contiguousLogical;
    /* The process opening the device may not be the same as the one that closes it. */
    gctUINT32           pidOpen;
}
gcsHAL_PRIVATE_DATA, * gcsHAL_PRIVATE_DATA_PTR;

gceSTATUS gckGALDEVICE_Setup_ISR(
    IN gckGALDEVICE Device
    );

gceSTATUS gckGALDEVICE_Setup_ISR_2D(
    IN gckGALDEVICE Device
    );

gceSTATUS gckGALDEVICE_Setup_ISR_VG(
    IN gckGALDEVICE Device
    );

gceSTATUS gckGALDEVICE_Release_ISR(
    IN gckGALDEVICE Device
    );

gceSTATUS gckGALDEVICE_Release_ISR_2D(
    IN gckGALDEVICE Device
    );

gceSTATUS gckGALDEVICE_Release_ISR_VG(
    IN gckGALDEVICE Device
    );

gceSTATUS gckGALDEVICE_Start_Threads(
    IN gckGALDEVICE Device
    );

gceSTATUS gckGALDEVICE_Stop_Threads(
    gckGALDEVICE Device
    );

gceSTATUS gckGALDEVICE_Start(
    IN gckGALDEVICE Device
    );

gceSTATUS gckGALDEVICE_Stop(
    gckGALDEVICE Device
    );

gceSTATUS gckGALDEVICE_Construct(
    IN gctINT IrqLine,
    IN gctUINT32 RegisterMemBase,
    IN gctSIZE_T RegisterMemSize,
    IN gctINT IrqLine2D,
    IN gctUINT32 RegisterMemBase2D,
    IN gctSIZE_T RegisterMemSize2D,
    IN gctINT IrqLineVG,
    IN gctUINT32 RegisterMemBaseVG,
    IN gctSIZE_T RegisterMemSizeVG,
    IN gctUINT32 ContiguousBase,
    IN gctSIZE_T ContiguousSize,
    IN gctSIZE_T BankSize,
    IN gctINT FastClear,
    IN gctINT Compression,
    IN gctUINT32 PhysBaseAddr,
    IN gctUINT32 PhysSize,
    IN gctINT Signal,
    OUT gckGALDEVICE *Device
    );

gceSTATUS gckGALDEVICE_Destroy(
    IN gckGALDEVICE Device
    );

#endif /* __gc_hal_kernel_device_h_ */
