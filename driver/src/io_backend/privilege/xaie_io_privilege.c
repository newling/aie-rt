/******************************************************************************
* Copyright (C) 2021 - 2022 Xilinx, Inc.  All rights reserved.
* SPDX-License-Identifier: MIT
******************************************************************************/


/*****************************************************************************/
/**
* @file xaie_io_privilege.c
* @{
*
* This file contains privilege routines for io backends.
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who     Date	Changes
* ----- ------  --------    ---------------------------------------------------
* 1.0   Wendy 05/17/2021  Initial creation
*
* </pre>
*
******************************************************************************/
/***************************** Include Files *********************************/
#include <stdlib.h>

#include "xaie_clock.h"
#include "xaie_reset_aie.h"
#include "xaie_feature_config.h"
#include "xaie_helper.h"
#include "xaie_helper_internal.h"
#include "xaie_io_privilege.h"
#include "xaie_npi.h"

#if defined(XAIE_FEATURE_PRIVILEGED_ENABLE) && !defined(XAIE_FEATURE_LITE)

/*****************************************************************************/
/***************************** Macro Definitions *****************************/

#define XAIE_ISOLATE_EAST_MASK	(1U << 3)
#define XAIE_ISOLATE_NORTH_MASK	(1U << 2)
#define XAIE_ISOLATE_WEST_MASK	(1U << 1)
#define XAIE_ISOLATE_SOUTH_MASK	(1U << 0)
#define XAIE_ISOLATE_ALL_MASK	((1U << 4) - 1)

#define XAIE_ERROR_NPI_INTR_ID	0x1U
/************************** Function Definitions *****************************/
/*****************************************************************************/
/**
*
* This API set the tile column reset
*
* @param	DevInst: Device Instance
* @param	Loc: Location of AIE SHIM tile
* @param	RstEnable: XAIE_ENABLE to assert reset, XAIE_DISABLE to
*			   deassert reset.
*
* @return       XAIE_OK on success, error code on failure
*
* @note		It is not required to check the DevInst and the Loc tile type
*		as the caller function should provide the correct value.
*		This function is internal to this file.
*
******************************************************************************/
static AieRC _XAie_PrivilegeSetColReset(XAie_DevInst *DevInst,
		XAie_LocType Loc, u8 RstEnable)
{
	u8 TileType;
	u32 FldVal;
	u64 RegAddr;
	const XAie_PlIfMod *PlIfMod;

	TileType = DevInst->DevOps->GetTTypefromLoc(DevInst, Loc);
	PlIfMod = DevInst->DevProp.DevMod[TileType].PlIfMod;
	RegAddr = PlIfMod->ColRstOff +
		XAie_GetTileAddr(DevInst, Loc.Row, Loc.Col);
	FldVal = XAie_SetField(RstEnable,
			PlIfMod->ColRst.Lsb,
			PlIfMod->ColRst.Mask);

	return XAie_Write32(DevInst, RegAddr, FldVal);
}

/*****************************************************************************/
/**
*
* This API set the tile columns reset for every column in the partition
*
* @param	DevInst: Device Instance
* @param	RstEnable: XAIE_ENABLE to assert reset, XAIE_DISABLE to
*			   deassert reset.
*
* @return       XAIE_OK on success, error code on failure
*
* @note		It is not required to check the DevInst as the caller function
*		should provide the correct value.
*		This function is internal to this file.
*
******************************************************************************/
static AieRC _XAie_PrivilegeSetPartColReset(XAie_DevInst *DevInst,
		u8 RstEnable)
{
	AieRC RC = XAIE_OK;

	for(u8 C = 0; C < DevInst->NumCols; C++) {
		XAie_LocType Loc = XAie_TileLoc(C, 0U);

		RC = _XAie_PrivilegeSetColReset(DevInst, Loc, RstEnable);
		if(RC != XAIE_OK) {
			XAIE_ERROR("Failed to reset columns.\n");
			break;
		}
	}

	return RC;
}
/*****************************************************************************/
/**
*
* This API reset all SHIMs in the AI engine partition
*
* @param	DevInst: Device Instance
*
* @return	XAIE_OK for success, and error value for failure
*
* @note		This function asserts reset, and then deassert it.
*		It is not required to check the DevInst as the caller function
*		should provide the correct value.
*		This function is internal to this file.
*
******************************************************************************/
static AieRC _XAie_PrivilegeRstPartShims(XAie_DevInst *DevInst)
{
	AieRC RC;

	RC = DevInst->DevOps->SetPartColShimReset(DevInst, XAIE_ENABLE);
	if(RC != XAIE_OK) {
		return RC;
	}

	RC = _XAie_NpiSetShimReset(DevInst, XAIE_ENABLE);
	if(RC != XAIE_OK) {
		return RC;
	}

	RC = _XAie_NpiSetShimReset(DevInst, XAIE_DISABLE);
	if(RC != XAIE_OK) {
		return RC;
	}

	RC = DevInst->DevOps->SetPartColShimReset(DevInst, XAIE_DISABLE);

	return RC;
}

/*****************************************************************************/
/**
*
* This API sets to block NSU AXI MM slave error and decode error based on user
* inputs. If NSU errors is blocked, it will only generate error events.
*
* @param	DevInst: Device Instance
* @param	Loc: Location of AIE SHIM tile
* @param	BlockSlvEnable: XAIE_ENABLE to block NSU AXI MM Slave errors,
*				or XAIE_DISABLE to unblock.
* @param	BlockDecEnable: XAIE_ENABLE to block NSU AXI MM Decode errors,
*				or XAIE_DISABLE to unblock.
*
* @return       XAIE_OK on success, error code on failure
*
* @note		It is not required to check the DevInst and the Loc tile type
*		as the caller function should provide the correct value.
*		This function is internal to this file.
*
******************************************************************************/
static AieRC _XAie_PrivilegeSetBlockAxiMmNsuErr(XAie_DevInst *DevInst,
		XAie_LocType Loc, u8 BlockSlvEnable, u8 BlockDecEnable)
{
	u8 TileType;
	u32 FldVal;
	u64 RegAddr;
	const XAie_PlIfMod *PlIfMod;
	const XAie_ShimNocAxiMMConfig *ShimNocAxiMM;

	TileType = DevInst->DevOps->GetTTypefromLoc(DevInst, Loc);
	PlIfMod = DevInst->DevProp.DevMod[TileType].PlIfMod;
	ShimNocAxiMM = PlIfMod->ShimNocAxiMM;
	RegAddr = ShimNocAxiMM->RegOff +
		XAie_GetTileAddr(DevInst, Loc.Row, Loc.Col);
	FldVal = XAie_SetField(BlockSlvEnable,
			ShimNocAxiMM->NsuSlvErr.Lsb,
			ShimNocAxiMM->NsuSlvErr.Mask);
	FldVal |= XAie_SetField(BlockDecEnable,
			ShimNocAxiMM->NsuDecErr.Lsb,
			ShimNocAxiMM->NsuDecErr.Mask);

	return XAie_Write32(DevInst, RegAddr, FldVal);
}

/*****************************************************************************/
/**
*
* This API sets to block the NSU AXI MM slave error and decode error config
* for all the SHIM NOCs in the full partition based on user inputs.
*
* @param	DevInst: Device Instance
* @param	BlockSlvEnable: XAIE_ENABLE to block NSU AXI MM Slave errors,
*				or XAIE_DISABLE to unblock.
* @param	BlockDecEnable: XAIE_ENABLE to block NSU AXI MM Decode errors,
*				or XAIE_DISABLE to unblock.
*
* @return       XAIE_OK on success, error code on failure
*
* @note		It is not required to check the DevInst as the caller function
*		should provide the correct value.
*		This function will do the following steps:
*		 * set AXI MM registers NSU errors fields in all SHIM NOC tiles
*		This function is internal to this file.
*
******************************************************************************/
static AieRC _XAie_PrivilegeSetPartBlockAxiMmNsuErr(XAie_DevInst *DevInst,
		u8 BlockSlvEnable, u8 BlockDecEnable)
{
	AieRC RC = XAIE_OK;

	for(u8 C = 0; C < DevInst->NumCols; C++) {
		XAie_LocType Loc = XAie_TileLoc(C, 0U);
		u8 TileType;

		TileType = DevInst->DevOps->GetTTypefromLoc(DevInst, Loc);
		if(TileType != XAIEGBL_TILE_TYPE_SHIMNOC) {
			continue;
		}
		RC = _XAie_PrivilegeSetBlockAxiMmNsuErr(DevInst, Loc,
				BlockSlvEnable, BlockDecEnable);
		if(RC != XAIE_OK) {
			XAIE_ERROR("Failed to set SHIM NOC AXI MM Errors.");
			return RC;
		}
	}

	return RC;
}

/*****************************************************************************/
/**
*
* This API sets the NMU switch configuration for a given SHIM South tile
*
* @param	DevInst: Device Instance
* @param	Loc: Location of SHIM south tile
* @param	FwdEastEnable: Configuration of switch for NMU 0. XAIE_ENABLE
*			      to forward NOC to east neighbor. XAIE_DISABLE to
*			      connect NOC to local NMU 0.
* @param	FromWestEnable: Configuration of switch for NMU 1. XAIE_ENABLE
*			       to have NOC accept from west neighbor.
*			       XAIE_DISABLE to have NOC connected to local NMU
*			       1.
*
* @return       XAIE_OK on success, error code on failure
******************************************************************************/
static AieRC _XAie_PrivilegeSetNmuSwitch(XAie_DevInst *DevInst,
		XAie_LocType Loc, u8 FwdEastEnable, u8 FromWestEnable)
{
	const XAie_PlIfMod *PlIfMod;
	u64 RegAddr;
	u8 TileType;
	u32 FldVal;

	TileType = DevInst->DevOps->GetTTypefromLoc(DevInst, Loc);
	if (TileType != XAIEGBL_TILE_TYPE_SHIMNOC) {
		XAIE_ERROR("Invalid Tile Type.");
		return XAIE_ERR;
	}

	PlIfMod = DevInst->DevProp.DevMod[TileType].PlIfMod;
	RegAddr = PlIfMod->ShimNocNmuSwitchOff +
			XAie_GetTileAddr(DevInst, Loc.Row, Loc.Col);
	FldVal = XAie_SetField(FwdEastEnable,
			PlIfMod->ShimNocNmuSwitch0.Lsb,
			PlIfMod->ShimNocNmuSwitch0.Mask);
	FldVal |= XAie_SetField(FromWestEnable,
			PlIfMod->ShimNocNmuSwitch1.Lsb,
			PlIfMod->ShimNocNmuSwitch1.Mask);

	return XAie_Write32(DevInst, RegAddr, FldVal);
}

/*****************************************************************************/
/**
*
* This API sets the partitions NMU switch configuration for all SHIM NOCs in
* the full partition.
*
* @param	DevInst: Device Instance
*
* @return       XAIE_OK on success, error code on failure
******************************************************************************/
static AieRC _XAie_PrivilegeSetPartNmuSwitch(XAie_DevInst *DevInst)
{
	AieRC RC = XAIE_OK;

	if (DevInst->StartCol != 0U) {
		/*
		 * The only NMU switches that need to be configured
		 * are in absoulute column 0 and 1.
		 */
		XAIE_DBG("Partition does not have start column 0, not configuring NMU switches");
		return XAIE_OK;
	}

	RC = _XAie_PrivilegeSetNmuSwitch(DevInst, XAie_TileLoc(0U, DevInst->ShimRow),
			XAIE_ENABLE, XAIE_DISABLE);
	if (RC != XAIE_OK) {
		XAIE_ERROR("Failed to set switch configuration for column 0");
		return RC;
	}
	RC = _XAie_PrivilegeSetNmuSwitch(DevInst, XAie_TileLoc(1U, DevInst->ShimRow),
			XAIE_DISABLE, XAIE_ENABLE);
	if (RC != XAIE_OK) {
		XAIE_ERROR("Failed to set switch configuration for column 1");
		return RC;
	}

	return RC;
}

/*****************************************************************************/
/**
* This API sets partition NPI protected register enabling
*
* @param	DevInst: AI engine partition device instance pointer
* @param	Enable: XAIE_ENABLE to enable access to protected register.
*			XAIE_DISABLE to disable access.
*
* @return       XAIE_OK on success, error code on failure
*
* @note		This function is internal to this file.
*
*******************************************************************************/
static AieRC _XAie_PrivilegeSetPartProtectedRegs(XAie_DevInst *DevInst,
		u8 Enable)
{
	AieRC RC;
	XAie_NpiProtRegReq NpiProtReq = {0};

	NpiProtReq.NumCols = DevInst->NumCols;
	NpiProtReq.Enable = Enable;
	RC = _XAie_NpiSetProtectedRegEnable(DevInst, &NpiProtReq);
	if(RC != XAIE_OK) {
		XAIE_ERROR("Failed to set protected registers.\n");
	}

	return RC;
}

/*****************************************************************************/
/**
*
* This API sets NoC interrupt ID to which the interrupt from second level
* interrupt controller shall be driven to.
*
* @param	DevInst: Device Instance
* @param	Loc: Location of AIE Tile
* @param	NoCIrqId: NoC IRQ index on which the interrupt shall be
*			  driven.
*
* @return	XAIE_OK on success, error code on failure.
*
* @note		None.
*
******************************************************************************/
static AieRC _XAie_PrivilegeSetL2IrqId(XAie_DevInst *DevInst, XAie_LocType Loc,
		u8 NoCIrqId)
{
	u64 RegAddr;
	u32 RegOffset;
	const XAie_L2IntrMod *IntrMod;

	IntrMod = DevInst->DevProp.DevMod[XAIEGBL_TILE_TYPE_SHIMNOC].L2IntrMod;
	RegOffset = IntrMod->IrqRegOff;
	RegAddr = XAie_GetTileAddr(DevInst, Loc.Row, Loc.Col) + RegOffset;
	return XAie_Write32(DevInst, RegAddr, NoCIrqId);
}

/*****************************************************************************/
/**
*
* This API sets NoC interrupt ID to which the error interrupts from second
* level interrupt controller shall be driven to. All the second level interrupt
* controllers with a given partition are configured.
*
* @param	DevInst: Device Instance
*
* @return	XAIE_OK on success, error code on failure.
*
* @note		None.
*
******************************************************************************/
static AieRC _XAie_PrivilegeSetL2ErrIrq(XAie_DevInst *DevInst)
{
	AieRC RC;
	XAie_LocType Loc = XAie_TileLoc(0, DevInst->ShimRow);

	for (Loc.Col = 0; Loc.Col < DevInst->NumCols; Loc.Col++) {
		u8 TileType = DevInst->DevOps->GetTTypefromLoc(DevInst, Loc);
		if (TileType != XAIEGBL_TILE_TYPE_SHIMNOC) {
			continue;
		}

		RC = _XAie_PrivilegeSetL2IrqId(DevInst, Loc,
				XAIE_ERROR_NPI_INTR_ID);
		if(RC != XAIE_OK) {
			XAIE_ERROR("Failed to configure L2 error IRQ channel\n");
			return RC;
		}
	}

	return XAIE_OK;
}

/*****************************************************************************/
/**
* This API initializes the AI engine partition
*
* @param	DevInst: AI engine partition device instance pointer
* @param	Opts: Initialization options
*
* @return       XAIE_OK on success, error code on failure
*
* @note		This operation does the following steps to initialize an AI
*		engine partition:
*		- Clock gate all columns
*		- Reset Columns
*		- Ungate all Columns
*		- Remove columns reset
*		- Reset shims
*		- Setup AXI MM not to return errors for AXI decode or slave
*		  errors, raise events instead.
*		- ungate all columns
*		- Setup partition isolation.
*		- zeroize memory if it is requested
*
*******************************************************************************/
AieRC _XAie_PrivilegeInitPart(XAie_DevInst *DevInst, XAie_PartInitOpts *Opts)
{
	u32 OptFlags;
	AieRC RC;

	if(Opts != NULL) {
		OptFlags = Opts->InitOpts;
	} else {
		OptFlags = XAIE_PART_INIT_OPT_DEFAULT;
	}

	RC = _XAie_PrivilegeSetPartProtectedRegs(DevInst, XAIE_ENABLE);
	if(RC != XAIE_OK) {
		XAIE_ERROR("Failed to initialize partition, enable protected registers failed.\n");
		return RC;
	}

	if((OptFlags & XAIE_PART_INIT_OPT_COLUMN_RST) != 0U) {
		/* Gate all tiles before resetting columns to quiet traffic*/
		RC = _XAie_PmSetPartitionClock(DevInst, XAIE_DISABLE);
		if(RC != XAIE_OK) {
			_XAie_PrivilegeSetPartProtectedRegs(DevInst, XAIE_DISABLE);
			return RC;
		}

		RC = _XAie_PrivilegeSetPartColReset(DevInst, XAIE_ENABLE);
		if(RC != XAIE_OK) {
			_XAie_PrivilegeSetPartProtectedRegs(DevInst, XAIE_DISABLE);
			return RC;
		}

		/* Enable clock buffer before removing column reset */
		RC = _XAie_PmSetPartitionClock(DevInst, XAIE_ENABLE);
		if(RC != XAIE_OK) {
			_XAie_PrivilegeSetPartProtectedRegs(DevInst, XAIE_DISABLE);
			return RC;
		}

		RC = _XAie_PrivilegeSetPartColReset(DevInst, XAIE_DISABLE);
		if(RC != XAIE_OK) {
			_XAie_PrivilegeSetPartProtectedRegs(DevInst, XAIE_DISABLE);
			return RC;
		}

	}

	if((OptFlags & XAIE_PART_INIT_OPT_SHIM_RST) != 0U) {
		RC = _XAie_PrivilegeRstPartShims(DevInst);
		if(RC != XAIE_OK) {
			_XAie_PrivilegeSetPartProtectedRegs(DevInst, XAIE_DISABLE);
			return RC;
		}

		if(DevInst->DevProp.DevGen == XAIE_DEV_GEN_AIE2PS) {
			RC = _XAie_PrivilegeSetPartNmuSwitch(DevInst);
			if(RC != XAIE_OK) {
				_XAie_PrivilegeSetPartProtectedRegs(DevInst,
						XAIE_DISABLE);
				return RC;
			}
		}
	}

	if((OptFlags & XAIE_PART_INIT_OPT_BLOCK_NOCAXIMMERR) != 0U) {
		RC = _XAie_PrivilegeSetPartBlockAxiMmNsuErr(DevInst,
			XAIE_ENABLE, XAIE_ENABLE);
		if(RC != XAIE_OK) {
			_XAie_PrivilegeSetPartProtectedRegs(DevInst, XAIE_DISABLE);
			return RC;
		}
	}

	RC = DevInst->DevOps->SetPartColClockAfterRst(DevInst, XAIE_ENABLE);
	if(RC != XAIE_OK) {
		_XAie_PrivilegeSetPartProtectedRegs(DevInst, XAIE_DISABLE);
		return RC;
	}

	if ((OptFlags & XAIE_PART_INIT_OPT_ISOLATE) != 0U) {
		RC = DevInst->DevOps->SetPartIsolationAfterRst(DevInst, XAIE_INIT_ISOLATION);
		if(RC != XAIE_OK) {
			_XAie_PrivilegeSetPartProtectedRegs(DevInst, XAIE_DISABLE);
		}
	}
	else {
		RC = DevInst->DevOps->SetPartIsolationAfterRst(DevInst, XAIE_CLEAR_ISOLATION);
		if(RC != XAIE_OK) {
			_XAie_PrivilegeSetPartProtectedRegs(DevInst, XAIE_DISABLE);
		}
	}

	if ((OptFlags & XAIE_PART_INIT_OPT_ZEROIZEMEM) != 0U) {
		RC = DevInst->DevOps->PartMemZeroInit(DevInst);
		if(RC != XAIE_OK) {
			_XAie_PrivilegeSetPartProtectedRegs(DevInst, XAIE_DISABLE);
			return RC;
		}
	}

	RC = _XAie_PrivilegeSetL2ErrIrq(DevInst);
	if(RC != XAIE_OK) {
		XAIE_ERROR("Failed to configure L2 error IRQ channels\n");
		return RC;
	}

	/* Enable only the tiles requested in Opts parameter */
        if(Opts != NULL && Opts->NumUseTiles > 0) {
                XAie_BackendTilesArray TilesArray;

                TilesArray.NumTiles = Opts->NumUseTiles;
                TilesArray.Locs = Opts->Locs;

                RC = XAie_RunOp(DevInst, XAIE_BACKEND_OP_REQUEST_TILES,
                (void *)&TilesArray);

                if(RC != XAIE_OK) {
			_XAie_PrivilegeSetPartProtectedRegs(DevInst, XAIE_DISABLE);
                        return RC;
                }
        }

	/*
	 * This is a temporary workaround to unblock rel-v2023.1 and make
	 * XAie_PartitionInitialize() consistent with XAie_ResetPartition().
	 */
	if (DevInst->DevProp.DevGen == XAIE_DEV_GEN_AIE) {
		RC = _XAie_PmSetPartitionClock(DevInst, XAIE_DISABLE);
		if (RC != XAIE_OK) {
			_XAie_PrivilegeSetPartProtectedRegs(DevInst, XAIE_DISABLE);
			return RC;
		}

		for(u32 C = 0; C < DevInst->NumCols; C++) {
			XAie_LocType Loc;
			u32 ColClockStatus;

			Loc = XAie_TileLoc(C, 1);
			ColClockStatus = _XAie_GetTileBitPosFromLoc(DevInst, Loc);
			_XAie_ClrBitInBitmap(DevInst->DevOps->TilesInUse,
				       ColClockStatus, DevInst->NumRows - 1);
		}
	}

	RC = _XAie_PrivilegeSetPartProtectedRegs(DevInst, XAIE_DISABLE);
	return RC;
}

/*****************************************************************************/
/**
* This API tears down the AI engine partition
*
* @param	DevInst: AI engine partition device instance pointer
* @param	Opts: Initialization options
*
* @return       XAIE_OK on success, error code on failure
*
* @note		This operation does the following steps to initialize an AI
*		engine partition:
*		- Clock gate all columns
*		- Reset Columns
*		- Ungate all columns
*		- Reset shims
*		- Remove columns reset
*		- Ungate all columns
*		- Zeroize memories
*		- Clock gate all columns
*
*******************************************************************************/
AieRC _XAie_PrivilegeTeardownPart(XAie_DevInst *DevInst)
{
	AieRC RC;

	RC = _XAie_PrivilegeSetPartProtectedRegs(DevInst, XAIE_ENABLE);
	if(RC != XAIE_OK) {
		XAIE_ERROR("Failed to teardown partition, enable protected registers failed.\n");
		return RC;
	}

	RC = _XAie_PmSetPartitionClock(DevInst, XAIE_DISABLE);
	if(RC != XAIE_OK) {
		_XAie_PrivilegeSetPartProtectedRegs(DevInst, XAIE_DISABLE);
		return RC;
	}

	RC = _XAie_PrivilegeSetPartColReset(DevInst, XAIE_ENABLE);
	if(RC != XAIE_OK) {
		_XAie_PrivilegeSetPartProtectedRegs(DevInst, XAIE_DISABLE);
		return RC;
	}

	RC = _XAie_PmSetPartitionClock(DevInst, XAIE_ENABLE);
	if(RC != XAIE_OK) {
		_XAie_PrivilegeSetPartProtectedRegs(DevInst, XAIE_DISABLE);
		return RC;
	}

	RC = _XAie_PrivilegeSetPartColReset(DevInst, XAIE_DISABLE);
	if(RC != XAIE_OK) {
		_XAie_PrivilegeSetPartProtectedRegs(DevInst, XAIE_DISABLE);
		return RC;
	}

	RC = _XAie_PrivilegeRstPartShims(DevInst);
	if(RC != XAIE_OK) {
		_XAie_PrivilegeSetPartProtectedRegs(DevInst, XAIE_DISABLE);
		return RC;
	}

	RC = DevInst->DevOps->SetPartColClockAfterRst(DevInst, XAIE_ENABLE);
	if(RC != XAIE_OK) {
		_XAie_PrivilegeSetPartProtectedRegs(DevInst, XAIE_DISABLE);
		return RC;
	}

	RC = DevInst->DevOps->PartMemZeroInit(DevInst);
	if (RC != XAIE_OK) {
		_XAie_PrivilegeSetPartProtectedRegs(DevInst, XAIE_DISABLE);
		return RC;
	}

	RC = _XAie_PmSetPartitionClock(DevInst, XAIE_DISABLE);
	if(RC != XAIE_OK) {
		_XAie_PrivilegeSetPartProtectedRegs(DevInst, XAIE_DISABLE);
		return RC;
	}

	return _XAie_PrivilegeSetPartProtectedRegs(DevInst, XAIE_DISABLE);
}

/*****************************************************************************/
/**
* This API enables clock for all the tiles passed as argument to this API.
*
* @param	DevInst: AI engine partition device instance pointer
* @param	Args: Backend tile args
*
* @return       XAIE_OK on success, error code on failure
*
* @note		Internal only.
*
*******************************************************************************/
AieRC _XAie_PrivilegeRequestTiles(XAie_DevInst *DevInst,
		XAie_BackendTilesArray *Args)
{
	AieRC RC;
	if(DevInst->DevProp.DevGen != XAIE_DEV_GEN_AIE) {
		RC = _XAie_PrivilegeSetPartProtectedRegs(DevInst, XAIE_ENABLE);
		if(RC != XAIE_OK) {
			XAIE_ERROR("Failed to initialize partition, enable"
					" protected registers failed.\n");
			 return RC;
		}
	}

	RC = DevInst->DevOps->RequestTiles(DevInst, Args);
	if(RC != XAIE_OK) {
		XAIE_ERROR("Request tiles failed\n");
	}

	if (DevInst->DevProp.DevGen != XAIE_DEV_GEN_AIE) {
		_XAie_PrivilegeSetPartProtectedRegs(DevInst, XAIE_DISABLE);
	}

	return RC;
}

/*****************************************************************************/
/**
* This API enables column clock and module clock control register for requested
* tiles passed as argument to this API.
*
* @param	DevInst: AI engine partition device instance pointer
* @param	Args: Backend tile args
*
* @return       XAIE_OK on success, error code on failure
*
* @note		Internal only.
*
*******************************************************************************/
AieRC _XAie_PrivilegeSetColumnClk(XAie_DevInst *DevInst,
		XAie_BackendColumnReq *Args)
{
	AieRC RC;
	/* TODO: Configure previlege registers only for non-AIE devices. */
	if(DevInst->DevProp.DevGen != XAIE_DEV_GEN_AIE) {
		RC = _XAie_PrivilegeSetPartProtectedRegs(DevInst, XAIE_ENABLE);
		if(RC != XAIE_OK) {
			XAIE_ERROR("Failed to initialize partition, enable"
					" protected registers failed.\n");
			 return RC;
		}
	}

	RC = DevInst->DevOps->SetColumnClk(DevInst, Args);
	if(RC != XAIE_OK) {
		XAIE_ERROR("Set Column Clock failed\n");
	}

	if (DevInst->DevProp.DevGen != XAIE_DEV_GEN_AIE) {
		_XAie_PrivilegeSetPartProtectedRegs(DevInst, XAIE_DISABLE);
	}

	return RC;
}

/*****************************************************************************/
/**
 * *
 * * This API writes to the Dma Pause register of noc to pause/resume the issuing of
 * * new AXI-MM commands on dma channel
 * *
 * * @param	DevInst: Device Instance.
 * * @param	Loc: Location of the Shim tile.
 * * @param	ChNum: Channel number of dma(0 or 1)
 * * @param	Dir: s2mm or mm2s
 * * @param	Pause: Set 1 to pause or 0 to resume
 * *
 * * @return	XAIE_OK on success, Error code on failure.
 *
 * ******************************************************************************/
AieRC XAie_NocDmaPause(XAie_DevInst *DevInst, XAie_LocType *Loc,
		u8 ChNum, XAie_DmaDirection Dir, u8 Pause)
{
	AieRC RC;
	volatile   u64 RegAddr;
	volatile   u32 Mask, FldVal, Lsb;
	const struct XAie_DmaMod *NocDma = NULL;

	if (DevInst == NULL) {
		XAIE_ERROR("Invalid Device Instance\n");
		return XAIE_ERR;
	}

	NocDma  = DevInst->DevProp.DevMod[XAIEGBL_TILE_TYPE_SHIMNOC].DmaMod;
	if (NocDma == NULL) {
		XAIE_ERROR("Invalid Nocmod\n");
		return XAIE_ERR;
	}

	switch (ChNum) {
	case 0/*Ch0*/:
		switch (Dir) {
		case DMA_S2MM:
			Mask = NocDma->NocDmaPauseReg->S2mm_0.Mask;
			Lsb =  NocDma->NocDmaPauseReg->S2mm_0.Lsb;
			break;
		case DMA_MM2S:
			Mask = NocDma->NocDmaPauseReg->Mm2s_0.Mask;
			Lsb = NocDma->NocDmaPauseReg->Mm2s_0.Lsb;
			break;
		default:
			XAIE_ERROR("Invalid dma direction selected\n");
		}
		break;
	case 1/*Ch1*/:
	switch (Dir) {
		case DMA_S2MM:
			Mask = NocDma->NocDmaPauseReg->S2mm_1.Mask;
			Lsb = NocDma->NocDmaPauseReg->S2mm_1.Lsb;
			break;
		case DMA_MM2S:
			Mask = NocDma->NocDmaPauseReg->Mm2s_1.Mask;
			Lsb = NocDma->NocDmaPauseReg->Mm2s_1.Lsb;
			break;
		default:
			XAIE_ERROR("Invalid dma direction selected\n");
	}
	break;
	default:
		XAIE_ERROR("Invalid dma channel selected\n");
		return XAIE_ERR;
	}

	FldVal = XAie_SetField(Pause, Lsb, Mask);

	RC = _XAie_PrivilegeSetPartProtectedRegs(DevInst, XAIE_ENABLE);
	if(RC != XAIE_OK) {
		XAIE_ERROR("Failed to enable the"
				"partition protected registers.\n");
		return RC;
	}

	if(Loc == XAIE_NULL) {
		for(u32 col = 0; col < DevInst->NumCols; col++) {
			XAie_LocType Loc = XAie_TileLoc(col, 0);
			RegAddr = NocDma->NocDmaPauseReg->RegOff +
				XAie_GetTileAddr(DevInst, Loc.Row, Loc.Col);
			XAie_MaskWrite32(DevInst, RegAddr, Mask, FldVal);
		}
	} else {
		RegAddr = NocDma->NocDmaPauseReg->RegOff +
			XAie_GetTileAddr(DevInst, Loc->Row, Loc->Col);
		XAie_MaskWrite32(DevInst, RegAddr, Mask, FldVal);
	}

	RC = _XAie_PrivilegeSetPartProtectedRegs(DevInst, XAIE_DISABLE);
	if(RC != XAIE_OK) {
		XAIE_ERROR("Failed to disable the "
				"partition protected registers. \n");
		return RC;
	}

	return XAIE_OK;
}

/*****************************************************************************/
/*
 * *
 * * This API writes to the Dma Pause register of uc to pause/resume the issuing of
 * * new AXI-MM commands on MM2DM/DM2MM
 * *
 * * @param	DevInst: Device Instance
 * * @param	Loc: Location of the Shim tile.
 * * @param	Channel: DM2MM or MM2DM
 * * @param	Pause: Set 1 to pause or 0 to resume
 * *
 * * @return	XAIE_OK on success, Error code on failure.
 *
 * ******************************************************************************/
AieRC XAie_UcDmaPause(XAie_DevInst *DevInst, XAie_LocType *Loc,
		UcDmaDir_enum Dir, u8 Pause)
{
	u64 RegAddr;
	AieRC RC = XAIE_OK;
	u32 Mask, FldVal, Lsb;
	const struct XAie_UcMod *UcDma = NULL;

	if (DevInst == NULL) {
		XAIE_ERROR("Invalid Device Instance\n");
		return XAIE_ERR;
	}

	UcDma = DevInst->DevProp.DevMod[XAIEGBL_TILE_TYPE_SHIMNOC].UcMod;
	if (UcDma == NULL) {
		XAIE_ERROR("Invalid Ucmod\n");
		return XAIE_ERR;
	}

	switch (Dir) {
	case XAIE_UC_MODULE__DMA_DIR__DM2MM:
		Mask = UcDma->UcDmaPauseReg->Dm2mm.Mask;
		Lsb = UcDma->UcDmaPauseReg->Dm2mm.Lsb;
		break;
	case XAIE_UC_MODULE__DMA_DIR__MM2DM:
		Mask = UcDma->UcDmaPauseReg->Mm2dm.Mask;
		Lsb = UcDma->UcDmaPauseReg->Mm2dm.Lsb;
		break;
	default:
		XAIE_ERROR("Invalid DMA direction\n");
		return XAIE_INVALID_DMA_DIRECTION;
	}

	FldVal = XAie_SetField(Pause, Lsb, Mask);

	RC = _XAie_PrivilegeSetPartProtectedRegs(DevInst, XAIE_ENABLE);
	if(RC != XAIE_OK) {
		XAIE_ERROR("Failed to enable the"
				" partition protected registers.\n");
		return RC;
	}

	if(Loc == XAIE_NULL) {
		for(u32 col = 0; col < DevInst->NumCols; col++) {
			XAie_LocType Loc = XAie_TileLoc(col, 0);
			RegAddr = UcDma->UcDmaPauseReg->RegOff +
				XAie_GetTileAddr(DevInst, Loc.Row, Loc.Col);
			XAie_MaskWrite32(DevInst, RegAddr, Mask, FldVal);
		}
	} else {
		RegAddr = UcDma->UcDmaPauseReg->RegOff +
			XAie_GetTileAddr(DevInst, Loc->Row, Loc->Col);
		XAie_MaskWrite32(DevInst, RegAddr, Mask, FldVal);
	}

	RC = _XAie_PrivilegeSetPartProtectedRegs(DevInst, XAIE_DISABLE);
	if(RC != XAIE_OK) {
		XAIE_ERROR("Failed to disable the "
				"partition protected registers.\n");
		return RC;
	}

	return XAIE_OK;
}

/*****************************************************************************/
/*
 * *
 * * This API sets the AXIMM Isolation of the partition
 * *
 * * @param	DevInst: Device Instance
 * * @param	IsolationFlags: Directions to block AXIMM
 *			- XAIE_INIT_ISOLATION: Block east and west
 *			- XAIE_INIT_WEST: Block west
 *			- XAIE_INIT_EAST: Block east and west
 * *
 * * @return	XAIE_OK on success, Error code on failure.
 *
 * ******************************************************************************/
AieRC XAie_PrivilegeSetAxiMMIsolation(XAie_DevInst *DevInst, u8 IsolationFlags)
{
	AieRC RC;

	RC = _XAie_PrivilegeSetPartProtectedRegs(DevInst, XAIE_ENABLE);
	if(RC != XAIE_OK) {
		XAIE_ERROR("Failed to enable the"
				"partition protected registers.\n");
		return RC;
	}

	if(DevInst->DevProp.DevGen == XAIE_DEV_GEN_AIE2PS) {
		RC = DevInst->DevOps->SetAxiMMIsolation(DevInst, IsolationFlags);
		if(RC!= XAIE_OK) {
			XAIE_ERROR("Failed to set the AxiMM Isolation\n");
			return RC;
		}
	}

	RC = _XAie_PrivilegeSetPartProtectedRegs(DevInst, XAIE_DISABLE);
	if(RC != XAIE_OK) {
		XAIE_ERROR("Failed to disable the "
				"partition protected registers.\n");
		return RC;
	}

	return RC;
}

#else /* XAIE_FEATURE_PRIVILEGED_ENABLE */

AieRC _XAie_PrivilegeInitPart(XAie_DevInst *DevInst, XAie_PartInitOpts *Opts)
{
	(void)DevInst;
	(void)Opts;
	return XAIE_FEATURE_NOT_SUPPORTED;
}

AieRC _XAie_PrivilegeTeardownPart(XAie_DevInst *DevInst)
{
	(void)DevInst;
	return XAIE_FEATURE_NOT_SUPPORTED;
}

AieRC _XAie_PrivilegeRequestTiles(XAie_DevInst *DevInst,
		XAie_BackendTilesArray *Args)
{
	(void)DevInst;
	(void)Args;
	return XAIE_FEATURE_NOT_SUPPORTED;
}


AieRC _XAie_PrivilegeSetColumnClk(XAie_DevInst *DevInst,
		XAie_BackendColumnReq *Args)
{
	(void)DevInst;
	(void)Args;
	return XAIE_FEATURE_NOT_SUPPORTED;
}
AieRC XAie_UcDmaPause(XAie_DevInst *DevInst, XAie_LocType *Loc,
		UcDmaDir_enum Dir, u8 Pause)
{
	(void)DevInst;
	(void)Loc;
	(void)Dir;
	(void)Pause;
	return XAIE_FEATURE_NOT_SUPPORTED;
}

AieRC XAie_NocDmaPause(XAie_DevInst *DevInst, XAie_LocType *Loc,
		u8 ChNum, XAie_DmaDirection Dir, u8 Pause)
{
	(void)DevInst;
	(void)Loc;
	(void)ChNum;
	(void)Dir;
	(void)Pause;
	return XAIE_FEATURE_NOT_SUPPORTED;
}

AieRC XAie_PrivilegeSetAxiMMIsolation(XAie_DevInst *DevInst,
		u8 IsolationFlags)
{
	(void)DevInst;
	(void)IsolationFlags;
	return XAIE_FEATURE_NOT_SUPPORTED;
}
#endif /* XAIE_FEATURE_PRIVILEGED_ENABLE && !XAIE_FEATURE_LITE */
/** @} */
