/*
 * nvLegacy.h
 *
 * Copyright (c) 2005, Nvidia Corporation.  All rights reserved.
 *
 * THE INFORMATION CONTAINED HEREIN IS PROPRIETARY AND CONFIDENTIAL TO
 * NVIDIA, CORPORATION.  USE, REPRODUCTION OR DISCLOSURE TO ANY THIRD PARTY
 * IS SUBJECT TO WRITTEN PRE-APPROVAL BY NVIDIA, CORPORATION.
 */

#ifndef __NV_LEGACY_H
#define __NV_LEGACY_H

typedef struct _LEGACY_INFO {
  unsigned int  uiDevId;
  char*         AdapterString;
} LEGACY_INFO;

// This is the list of the legacy GPUs
static const LEGACY_INFO LegacyList[] = {
//    PCI-ID    Marketing name
    { 0x0020,   "RIVA TNT"                        },
    { 0x0028,   "RIVA TNT2/TNT2 Pro"              },
    { 0x00A0,   "Aladdin TNT2"                    },
    { 0x002C,   "Vanta/Vanta LT"                  },
    { 0x0029,   "RIVA TNT2 Ultra"                 },
    { 0x002D,   "RIVA TNT2 Model 64/Model 64 Pro" },
    { 0x0100,   "GeForce 256"                     },
    { 0x0101,   "GeForce DDR"                     },
    { 0x0103,   "Quadro"                          },
    { 0x0150,   "GeForce2 GTS/GeForce2 Pro"       },
    { 0x0151,   "GeForce2 Ti"                     },
    { 0x0152,   "GeForce2 Ultra"                  },
    { 0x0153,   "Quadro2 Pro"                     } 
};

#endif /* __NV_LEGACY_H */
