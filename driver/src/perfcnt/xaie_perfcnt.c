/******************************************************************************
* Copyright (C) 2019 - 2020 Xilinx, Inc.  All rights reserved.
* SPDX-License-Identifier: MIT
******************************************************************************/


/*****************************************************************************/
/**
* @file xaie_perfcnt.c
* @{
*
* This file contains routines for AIE performance counters
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who     Date        Changes
* ----- ------  --------    ---------------------------------------------------
* 1.0   Dishita 11/22/2019  Initial creation
* 1.1   Tejus   04/13/2020  Remove use of range in apis
* 1.2   Dishita 04/16/2020  Fix compiler warnings
*
* </pre>
*
******************************************************************************/
/***************************** Include Files *********************************/
#include "xaie_perfcnt.h"
#include "xaie_events.h"

/*****************************************************************************/
/***************************** Macro Definitions *****************************/

/************************** Function Definitions *****************************/
/*****************************************************************************/
/* This API reads the given counter for the given range of tiles
*
* @param        DevInst: Device Instance
* @param        Loc: Location of AIE tile
* @param        Counter:Performance Counter
* @return       XAIE_OK on success
*
* @note
*
******************************************************************************/
u32 XAie_PerfCounterGet(XAie_DevInst *DevInst, XAie_LocType Loc,
		XAie_PerfCounters Counter)
{
	u32 CounterRegOffset;
	u64 CounterRegAddr;
	u8 TileType;
	const XAie_PerfMod *PerfMod;

	if((DevInst == XAIE_NULL) ||
			(DevInst->IsReady != XAIE_COMPONENT_IS_READY)) {
		XAieLib_print("Error: Invalid Device Instance\n");
		return XAIE_INVALID_ARGS;
	}

	TileType = _XAie_GetTileTypefromLoc(DevInst, Loc);
	if(TileType == XAIEGBL_TILE_TYPE_MAX) {
		XAieLib_print("Error: Invalid Tile Type\n");
		return XAIE_INVALID_TILE;
	}

	PerfMod = &DevInst->DevProp.DevMod[TileType].PerfMod[0];
	/*
	 * Checking if the counter is memory module counter of AIE tile
	 * XAie_PerfCounters Enum number corresponds to mem module - counter 4
	 * or 5, it is implied to be counter 0 or 1 of mem module of aie tile.
	 */
	if(Counter >= XAIE_MEMPERFCOUNTER_0) {
		Counter -= XAIE_MEMPERFCOUNTER_0;
		PerfMod = &DevInst->DevProp.DevMod[TileType].PerfMod[1];
	}

	/* Checking for valid Counter */
	if(Counter >= PerfMod->MaxCounterVal) {
		XAieLib_print("Error: Invalid Counter number: %d\n", Counter);
		return XAIE_INVALID_ARGS;
	}

	/* Get offset address based on Counter */
	CounterRegOffset = PerfMod->PerfCounterBaseAddr +
				((Counter)*PerfMod->PerfCounterOffsetAdd);

	/* Compute absolute address and write to register */
	CounterRegAddr = DevInst->BaseAddr + _XAie_GetTileAddr(DevInst, Loc.Row,
						Loc.Col) + CounterRegOffset;
	return XAieGbl_Read32(CounterRegAddr);
}
/*****************************************************************************/
/* This API configures the control registers corresponding to the counters
*  with the start and stop event for the given tile
*
* @param        DevInst: Device Instance
* @param        Loc: Location of the tile
* @param        Counter:Performance Counter
* @param        StartEvent:Event that triggers start to the counter
* @Param        StopEvent: Event that triggers stop to the counter
* @return       XAIE_OK on success
*
* @note
*
******************************************************************************/
AieRC XAie_PerfCounterControlSet(XAie_DevInst *DevInst, XAie_LocType Loc,
		XAie_PerfCounters Counter, XAie_Events StartEvent,
		XAie_Events StopEvent)
{
	u32 RegOffset, FldVal, FldMask;
	u64 RegAddr;
	u8 TileType, IntStartEvent, IntStopEvent;
	const XAie_PerfMod *PerfMod;
	const XAie_EvntMod *EvntMod;

	if((DevInst == XAIE_NULL) ||
			(DevInst->IsReady != XAIE_COMPONENT_IS_READY)) {
		XAieLib_print("Error: Invalid Device Instance\n");
		return XAIE_INVALID_ARGS;
	}

	TileType = _XAie_GetTileTypefromLoc(DevInst, Loc);
	if(TileType == XAIEGBL_TILE_TYPE_MAX) {
		XAieLib_print("Error: Invalid Tile Type\n");
		return XAIE_INVALID_TILE;
	}

	PerfMod = &DevInst->DevProp.DevMod[TileType].PerfMod[0];
	EvntMod = &DevInst->DevProp.DevMod[TileType].EvntMod[0];
	/*
	 * Checking if the counter is memory module counter of AIE tile
	 * XAie_PerfCounters Enum number corresponds to mem module - counter 4
	 * or 5, it is implied to be counter 0 or 1 of mem module of aie tile.
	 */
	if(Counter >= XAIE_MEMPERFCOUNTER_0) {
		Counter -= XAIE_MEMPERFCOUNTER_0;
		PerfMod = &DevInst->DevProp.DevMod[TileType].PerfMod[1];
		EvntMod = &DevInst->DevProp.DevMod[TileType].EvntMod[1];
	}

	/* check if the event passed as input is corresponding to the module */
	if(StartEvent < EvntMod->EventMin || StartEvent > EvntMod->EventMax ||
		StopEvent < EvntMod->EventMin || StopEvent > EvntMod->EventMax) {
		XAieLib_print("Error: Invalid Event id\n");
		return XAIE_INVALID_ARGS;
	}

	/* Subtract the module offset from event number */
	StartEvent -= EvntMod->EventMin;
	StopEvent -= EvntMod->EventMin;

	/* Getting the true event number from the enum to array mapping */
	IntStartEvent = EvntMod->XAie_EventNumber[StartEvent];
	IntStopEvent = EvntMod->XAie_EventNumber[StopEvent];

	/*checking for valid true event number */
	if(IntStartEvent == XAIE_EVENT_INVALID ||
			IntStopEvent == XAIE_EVENT_INVALID) {
		XAieLib_print("Error: Invalid Event id\n");
		return XAIE_INVALID_ARGS;
	}

	/* Checking for valid Counter */
	if(Counter >= PerfMod->MaxCounterVal) {
		XAieLib_print("Error: Invalid Counter number: %d\n", Counter);
		return XAIE_INVALID_ARGS;
	}

	/* Get offset address based on Counter */
	RegOffset = PerfMod->PerfCtrlBaseAddr +
				(Counter / 2U * PerfMod->PerfCtrlOffsetAdd);
	/* Compute mask for performance control register */
	FldMask = (PerfMod->Start.Mask | PerfMod->Stop.Mask) <<
				(PerfMod->StartStopShift * (Counter % 2U));
	/* Compute value to be written to the performance control register */
	FldVal = XAie_SetField(IntStartEvent,
		PerfMod->Start.Lsb + (PerfMod->StartStopShift * (Counter % 2U)),
		PerfMod->Start.Mask << (PerfMod->StartStopShift * (Counter % 2U)))|
		XAie_SetField(IntStopEvent,
		PerfMod->Stop.Lsb + (PerfMod->StartStopShift * (Counter % 2U)),
		PerfMod->Stop.Mask << (PerfMod->StartStopShift * (Counter % 2U)));

	/* Compute absolute address and write to register */
	RegAddr = DevInst->BaseAddr +
			_XAie_GetTileAddr(DevInst, Loc.Row, Loc.Col) + RegOffset;
	XAieGbl_MaskWrite32(RegAddr, FldMask, FldVal);
	return XAIE_OK;
}

/*****************************************************************************/
/* This API configures the control registers corresponding to the counter
*  with the reset event for the given range of tiles
*
* @param        DevInst: Device Instance
* @param        Loc: Location of AIE tile
* @param        Counter:Performance Counter
* @param        ResetEvent:Event that triggers reset to the counter
* @return       XAIE_OK on success
*
* @note
*
******************************************************************************/
AieRC XAie_PerfCounterResetControlSet(XAie_DevInst *DevInst, XAie_LocType Loc,
		XAie_PerfCounters Counter, XAie_Events ResetEvent)
{
	u32 ResetRegOffset, ResetFldMask;
	u64 ResetRegAddr, ResetFldVal;
	u8 TileType, IntResetEvent;
	const XAie_PerfMod *PerfMod;
	const XAie_EvntMod *EvntMod;

	if((DevInst == XAIE_NULL) ||
			(DevInst->IsReady != XAIE_COMPONENT_IS_READY)) {
		XAieLib_print("Error: Invalid Device Instance\n");
		return XAIE_INVALID_ARGS;
	}

	TileType = _XAie_GetTileTypefromLoc(DevInst, Loc);
	if(TileType == XAIEGBL_TILE_TYPE_MAX) {
		XAieLib_print("Error: Invalid Tile Type\n");
		return XAIE_INVALID_TILE;
	}

	PerfMod = &DevInst->DevProp.DevMod[TileType].PerfMod[0];
	EvntMod = &DevInst->DevProp.DevMod[TileType].EvntMod[0];
	/*
	 * Checking if the counter is memory module counter of AIE tile
	 * XAie_PerfCounters Enum number corresponds to mem module - counter 4
	 * or 5, it is implied to be counter 0 or 1 of mem module of aie tile.
	 */
	if(Counter >= XAIE_MEMPERFCOUNTER_0) {
		Counter -= XAIE_MEMPERFCOUNTER_0;
		PerfMod = &DevInst->DevProp.DevMod[TileType].PerfMod[1];
		EvntMod = &DevInst->DevProp.DevMod[TileType].EvntMod[1];
	}

	/* check if the event passed as input is corresponding to the module */
	if(ResetEvent < EvntMod->EventMin || ResetEvent > EvntMod->EventMax) {
		XAieLib_print("Error: Invalid Event id: %d\n", ResetEvent);
		return XAIE_INVALID_ARGS;
	}

	/* Subtract the module offset from event number */
	ResetEvent -= EvntMod->EventMin;

	/* Getting the true event number from the enum to array mapping */
	IntResetEvent = EvntMod->XAie_EventNumber[ResetEvent];

	/*checking for valid true event number */
	if(IntResetEvent == XAIE_EVENT_INVALID) {
		XAieLib_print("Error: Invalid Event id: %d\n", ResetEvent);
		return XAIE_INVALID_ARGS;
	}

	/* Checking for valid Counter */
	if(Counter >= PerfMod->MaxCounterVal) {
		XAieLib_print("Error: Invalid Counter number: %d\n", Counter);
		return XAIE_INVALID_ARGS;
	}

	/* Get offset address based on Counter */
	ResetRegOffset = PerfMod->PerfCtrlResetBaseAddr;

	/* Compute mask for performance control register */
	ResetFldMask = PerfMod->Reset.Mask <<
					(PerfMod->ResetShift * (Counter));
	/* Compute value to be written to the performance control register */
	ResetFldVal = XAie_SetField(IntResetEvent,
		PerfMod->Reset.Lsb + (PerfMod->ResetShift * Counter),
		PerfMod->Reset.Mask << (PerfMod->ResetShift * Counter));

	/* Compute absolute address and write to register */
	ResetRegAddr = DevInst->BaseAddr +
		_XAie_GetTileAddr(DevInst, Loc.Row, Loc.Col) + ResetRegOffset;
	XAieGbl_MaskWrite32(ResetRegAddr, ResetFldMask, ResetFldVal);
	return XAIE_OK;
}

/*****************************************************************************/
/* This API sets the performance counter event value for the given tile.
*
* @param        DevInst: Device Instance
* @param        Loc: Location of Tile
* @param        Counter:Performance Counter
* @param        CounterVal:Performance Counter Value
* @return       XAIE_OK on success
*
* @note
*
******************************************************************************/
AieRC XAie_PerfCounterSet(XAie_DevInst *DevInst, XAie_LocType Loc,
		XAie_PerfCounters Counter, u32 CounterVal)
{
	u32 CounterRegOffset;
	u64 CounterRegAddr;
	u8 TileType;
	const XAie_PerfMod *PerfMod;

	if((DevInst == XAIE_NULL) ||
			(DevInst->IsReady != XAIE_COMPONENT_IS_READY)) {
		XAieLib_print("Error: Invalid Device Instance\n");
		return XAIE_INVALID_ARGS;
	}

	TileType = _XAie_GetTileTypefromLoc(DevInst, Loc);
	if(TileType == XAIEGBL_TILE_TYPE_MAX) {
		XAieLib_print("Error: Invalid Tile Type\n");
		return XAIE_INVALID_TILE;
	}

	PerfMod = &DevInst->DevProp.DevMod[TileType].PerfMod[0];
	/*
	 * Checking if the counter is memory module counter of AIE tile
	 * XAie_PerfCounters Enum number corresponds to mem module - counter 4
	 * or 5, it is implied to be counter 0 or 1 of mem module of aie tile.
	 */
	if(Counter >= XAIE_MEMPERFCOUNTER_0) {
		Counter -= XAIE_MEMPERFCOUNTER_0;
		PerfMod = &DevInst->DevProp.DevMod[TileType].PerfMod[1];
	}

	/* Checking for valid Counter */
	if(Counter >= PerfMod->MaxCounterVal) {
		XAieLib_print("Error: Invalid Counter number: %d\n", Counter);
		return XAIE_INVALID_ARGS;
	}

	/* Get offset address based on Counter */
	CounterRegOffset = PerfMod->PerfCounterBaseAddr +
					((Counter)*PerfMod->PerfCounterOffsetAdd);

	/* Compute absolute address and write to register */
	CounterRegAddr = DevInst->BaseAddr +
		_XAie_GetTileAddr(DevInst, Loc.Row ,Loc.Col) + CounterRegOffset;
	XAieGbl_Write32(CounterRegAddr,CounterVal);
	return XAIE_OK;
}
/*****************************************************************************/
/* This API sets the performance counter event value for the given tile.
*
* @param        DevInst: Device Instance
* @param        Loc: Location of AIE tile
* @param        Counter:Performance Counter
* @param        EventVal:Event value to set
* @return       XAIE_OK on success
*
* @note
*
******************************************************************************/
AieRC XAie_PerfCounterEventValueSet(XAie_DevInst *DevInst, XAie_LocType Loc,
		XAie_PerfCounters Counter, u32 EventVal)
{
	u32 CounterRegOffset;
	u64 CounterRegAddr;
	u8 TileType;
	const XAie_PerfMod *PerfMod;

	if((DevInst == XAIE_NULL) ||
			(DevInst->IsReady != XAIE_COMPONENT_IS_READY)) {
		XAieLib_print("Error: Invalid Device Instance\n");
		return XAIE_INVALID_ARGS;
	}

	TileType = _XAie_GetTileTypefromLoc(DevInst, Loc);
	if(TileType == XAIEGBL_TILE_TYPE_MAX) {
		XAieLib_print("Error: Invalid Tile Type\n");
		return XAIE_INVALID_TILE;
	}

	PerfMod = &DevInst->DevProp.DevMod[TileType].PerfMod[0];
	/*
	 * Checking if the counter is memory module counter of AIE tile
	 * XAie_PerfCounters Enum number corresponds to mem module - counter 4
	 * or 5, it is implied to be counter 0 or 1 of mem module of aie tile.
	 */
	if(Counter >= XAIE_MEMPERFCOUNTER_0) {
		Counter -= XAIE_MEMPERFCOUNTER_0;
		PerfMod = &DevInst->DevProp.DevMod[TileType].PerfMod[1];
	}

	/* Checking for valid Counter */
	if(Counter >= PerfMod->MaxCounterVal) {
		XAieLib_print("Error: Invalid Counter number: %d\n", Counter);
		return XAIE_INVALID_ARGS;
	}

	/* Get offset address based on Counter */
	CounterRegOffset = (PerfMod->PerfCounterEvtValBaseAddr) +
				((Counter)*PerfMod->PerfCounterOffsetAdd);

	/* Compute absolute address and write to register */
	CounterRegAddr = DevInst->BaseAddr +
		_XAie_GetTileAddr(DevInst, Loc.Row, Loc.Col) + CounterRegOffset;
	XAieGbl_Write32(CounterRegAddr,EventVal);
	return XAIE_OK;
}
