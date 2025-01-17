/******************************************************************************
* Copyright (C) 2019 - 2022 Xilinx, Inc.  All rights reserved.
* SPDX-License-Identifier: MIT
******************************************************************************/


/*****************************************************************************/
/**
* @file xaie_elfloader.h
* @{
*
* Header file for core elf loader functions
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who     Date     Changes
* ----- ------  -------- -----------------------------------------------------
* 1.0   Tejus   09/24/2019  Initial creation
* 1.1   Tejus   03/20/2020  Remove range apis
* 1.2   Tejus   05/26/2020  Add API to load elf from memory.
* </pre>
*
******************************************************************************/
#ifndef XAIELOADER_H
#define XAIELOADER_H

#include "xaie_feature_config.h"
#ifdef XAIE_FEATURE_ELF_ENABLE

/***************************** Include Files *********************************/
#include <elf.h>
#include <stdlib.h>
#include <string.h>
#include "xaie_helper.h"
#include "xaiegbl.h"
#include "xaiegbl_defs.h"
#include "xaiegbl_defs.h"

/************************** Constant Definitions *****************************/
#define XAIE_LOAD_ELF_TXT	(1U << 0U)
#define XAIE_LOAD_ELF_BSS	(1U << 1U)
#define XAIE_LOAD_ELF_DATA	(1U << 2U)
#define XAIE_LOAD_ELF_ALL	(XAIE_LOAD_ELF_TXT | XAIE_LOAD_ELF_BSS | \
					XAIE_LOAD_ELF_DATA)

/************************** Variable Definitions *****************************/
typedef struct {
	u32 start;	/**< Stack start address */
	u32 end;	/**< Stack end address */
} XAieSim_StackSz;
/************************** Function Prototypes  *****************************/

AieRC XAie_LoadElf(XAie_DevInst *DevInst, XAie_LocType Loc, const char *ElfPtr,
		u8 LoadSym);
AieRC XAie_LoadElfMem(XAie_DevInst *DevInst, XAie_LocType Loc,
		const unsigned char* ElfMem);
AieRC XAie_LoadElfSection(XAie_DevInst *DevInst, XAie_LocType Loc,
		const unsigned char *SectionPtr, const Elf32_Phdr *Phdr);
AieRC XAie_LoadElfSectionBlock(XAie_DevInst *DevInst, XAie_LocType Loc,
		const unsigned char* SectionPtr, u64 TgtAddr, u32 Size);
AieRC XAie_LoadElfPartial(XAie_DevInst *DevInst, XAie_LocType Loc,
		const char* ElfPtr, u8 Sections, u8 LoadSym);

void _XAie_PrintElfHdr(const Elf32_Ehdr *Ehdr);
void _XAie_PrintProgSectHdr(const Elf32_Phdr *Phdr);
#endif /* XAIE_FEATURE_ELF_ENABLE */
#endif		/* end of protection macro */
/** @} */
