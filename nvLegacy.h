/*
 * nvLegacy.h
 *
 * Copyright (c) 2006, Nvidia Corporation.  All rights reserved.
 *
 * THE INFORMATION CONTAINED HEREIN IS PROPRIETARY AND CONFIDENTIAL TO
 * NVIDIA, CORPORATION.  USE, REPRODUCTION OR DISCLOSURE TO ANY THIRD PARTY
 * IS SUBJECT TO WRITTEN PRE-APPROVAL BY NVIDIA, CORPORATION.
 */

#ifndef __NV_LEGACY_H
#define __NV_LEGACY_H

typedef struct _LEGACY_INFO {
    unsigned int  uiDevId;
    unsigned int  branch;
    char*         AdapterString;
} LEGACY_INFO;

typedef struct _LEGACY_STRINGS {
    unsigned int    branch;
    char*           description;
} LEGACY_STRINGS;

/*
 * This table describes how we should refer to legacy branches.
 */
static const LEGACY_STRINGS LegacyStrings[] = {
    { 2, "1.0-96xx" },
    { 1, "1.0-71xx" }
};

// This is the list of the legacy GPUs
static const LEGACY_INFO LegacyList[] = {
//    PCI-ID    Branch  Marketing name
    { 0x0020,   1,      "RIVA TNT" },
    { 0x0028,   1,      "RIVA TNT2/TNT2 Pro" },
    { 0x0029,   1,      "RIVA TNT2 Ultra" },
    { 0x002C,   1,      "Vanta/Vanta LT" },
    { 0x002D,   1,      "RIVA TNT2 Model 64/Model 64 Pro" },
    { 0x00A0,   1,      "Aladdin TNT2" },
    { 0x0100,   1,      "GeForce 256" },
    { 0x0101,   1,      "GeForce DDR" },
    { 0x0103,   1,      "Quadro" },
    { 0x0110,   2,      "GeForce2 MX/MX 400" },
    { 0x0111,   2,      "GeForce2 MX 100/200" },
    { 0x0112,   2,      "GeForce2 Go" },
    { 0x0113,   2,      "Quadro2 MXR/EX/Go" },
    { 0x0150,   1,      "GeForce2 GTS/GeForce2 Pro" },
    { 0x0151,   1,      "GeForce2 Ti" },
    { 0x0152,   1,      "GeForce2 Ultra" },
    { 0x0153,   1,      "Quadro2 Pro" },
    { 0x0170,   2,      "GeForce4 MX 460" },
    { 0x0171,   2,      "GeForce4 MX 440" },
    { 0x0172,   2,      "GeForce4 MX 420" },
    { 0x0173,   2,      "GeForce4 MX 440-SE" },
    { 0x0174,   2,      "GeForce4 440 Go" },
    { 0x0175,   2,      "GeForce4 420 Go" },
    { 0x0176,   2,      "GeForce4 420 Go 32M" },
    { 0x0177,   2,      "GeForce4 460 Go" },
    { 0x0178,   2,      "Quadro4 550 XGL" },
    { 0x0179,   2,      "GeForce4 440 Go 64M" },
    { 0x017A,   2,      "Quadro NVS" },
    { 0x017C,   2,      "Quadro4 500 GoGL" },
    { 0x017D,   2,      "GeForce4 410 Go 16M" },
    { 0x0181,   2,      "GeForce4 MX 440 with AGP8X" },
    { 0x0182,   2,      "GeForce4 MX 440SE with AGP8X" },
    { 0x0183,   2,      "GeForce4 MX 420 with AGP8X" },
    { 0x0185,   2,      "GeForce4 MX 4000" },
    { 0x0188,   2,      "Quadro4 580 XGL" },
    { 0x018A,   2,      "Quadro NVS 280 SD" },
    { 0x018B,   2,      "Quadro4 380 XGL" },
    { 0x018C,   2,      "Quadro NVS 50 PCI" },
    { 0x01A0,   2,      "GeForce2 Integrated GPU" },
    { 0x01F0,   2,      "GeForce4 MX Integrated GPU" },
    { 0x0200,   2,      "GeForce3" },
    { 0x0201,   2,      "GeForce3 Ti 200" },
    { 0x0202,   2,      "GeForce3 Ti 500" },
    { 0x0203,   2,      "Quadro DCC" },
    { 0x0250,   2,      "GeForce4 Ti 4600" },
    { 0x0251,   2,      "GeForce4 Ti 4400" },
    { 0x0253,   2,      "GeForce4 Ti 4200" },
    { 0x0258,   2,      "Quadro4 900 XGL" },
    { 0x0259,   2,      "Quadro4 750 XGL" },
    { 0x025B,   2,      "Quadro4 700 XGL" },
    { 0x0280,   2,      "GeForce4 Ti 4800" },
    { 0x0281,   2,      "GeForce4 Ti 4200 with AGP8X" },
    { 0x0282,   2,      "GeForce4 Ti 4800 SE" },
    { 0x0286,   2,      "GeForce4 4200 Go" },
    { 0x0288,   2,      "Quadro4 980 XGL" },
    { 0x0289,   2,      "Quadro4 780 XGL" },
    { 0x028C,   2,      "Quadro4 700 GoGL" }
};

#endif /* __NV_LEGACY_H */
