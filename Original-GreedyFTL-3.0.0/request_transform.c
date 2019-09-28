//////////////////////////////////////////////////////////////////////////////////
// request_transform.c for Cosmos+ OpenSSD
// Copyright (c) 2017 Hanyang University ENC Lab.
// Contributed by Yong Ho Song <yhsong@enc.hanyang.ac.kr>
//				  Jaewook Kwak <jwkwak@enc.hanyang.ac.kr>
//			      Sangjin Lee <sjlee@enc.hanyang.ac.kr>
//
// This file is part of Cosmos+ OpenSSD.
//
// Cosmos+ OpenSSD is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 3, or (at your option)
// any later version.
//
// Cosmos+ OpenSSD is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
// See the GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Cosmos+ OpenSSD; see the file COPYING.
// If not, see <http://www.gnu.org/licenses/>.
//////////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////////
// Company: ENC Lab. <http://enc.hanyang.ac.kr>
// Engineer: Jaewook Kwak <jwkwak@enc.hanyang.ac.kr>
//
// Project Name: Cosmos+ OpenSSD
// Design Name: Cosmos+ Firmware
// Module Name: Request Scheduler
// File Name: request_transform.c
//
// Version: v1.0.0
//
// Description:
//	 - transform request information
//   - check dependency between requests
//   - issue host DMA request to host DMA engine
//////////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////////
// Revision History:
//
// * v1.0.0
//   - First draft
//////////////////////////////////////////////////////////////////////////////////

#include "xil_printf.h"
#include <assert.h>
#include "nvme/nvme.h"
#include "nvme/host_lld.h"
#include "memory_map.h"
#include "ftl_config.h"
#include <stdlib.h>

// add by yanjie.tan @ 2019.1.22
#include "wal/wal_ssd.h"

P_ROW_ADDR_DEPENDENCY_TABLE rowAddrDependencyTablePtr;
unsigned long pages_written_to_flash_rxdma;
unsigned long pages_written_to_flash_nand;
unsigned long pages_written_to_flash_log;

unsigned long drfn, eb, etbs, eubs, edbe, fbb, gc, ina, rbbt, rtstll, sbbt,
		wlts;

void InitDependencyTable() {
	unsigned int blockNo, wayNo, chNo;
	rowAddrDependencyTablePtr =
			(P_ROW_ADDR_DEPENDENCY_TABLE) ROW_ADDR_DEPENDENCY_TABLE_ADDR;

	for (blockNo = 0; blockNo < MAIN_BLOCKS_PER_DIE; blockNo++) {
		for (wayNo = 0; wayNo < USER_WAYS; wayNo++) {
			for (chNo = 0; chNo < USER_CHANNELS; chNo++) {
				rowAddrDependencyTablePtr->block[chNo][wayNo][blockNo].permittedProgPage =
						0;
				rowAddrDependencyTablePtr->block[chNo][wayNo][blockNo].blockedReadReqCnt =
						0;
				rowAddrDependencyTablePtr->block[chNo][wayNo][blockNo].blockedEraseReqFlag =
						0;
			}
		}
	}
	pages_written_to_flash_rxdma = 0;
	pages_written_to_flash_nand = 0;
	pages_written_to_flash_log = 0;
	drfn = 0;
	eb = 0;
	etbs = 0;
	eubs = 0;
	edbe = 0;
	fbb = 0;
	gc = 0;
	ina = 0;
	rbbt = 0;
	rtstll = 0;
	sbbt = 0;
	wlts = 0;
}

void ReqTransNvmeToSlice(unsigned int cmdSlotTag, unsigned int startLba,
		unsigned int nlb, unsigned int cmdCode, unsigned int txId) {
	unsigned int reqSlotTag, requestedNvmeBlock, tempNumOfNvmeBlock,
			transCounter, tempLsa, loop, nvmeBlockOffset, nvmeDmaStartIndex,
			reqCode;

	requestedNvmeBlock = nlb + 1;
	transCounter = 0;
	nvmeDmaStartIndex = 0;
	tempLsa = startLba / NVME_BLOCKS_PER_SLICE;
	loop = ((startLba % NVME_BLOCKS_PER_SLICE) + requestedNvmeBlock)
			/ NVME_BLOCKS_PER_SLICE;

	if (cmdCode == IO_NVM_WRITE)
		reqCode = REQ_CODE_WRITE;
	else if (cmdCode == IO_NVM_READ)
		reqCode = REQ_CODE_READ;
	else
		assert(!"[WARNING] Not supported command code [WARNING]");

	//first transform
	nvmeBlockOffset = (startLba % NVME_BLOCKS_PER_SLICE);
	if (loop)
		tempNumOfNvmeBlock = NVME_BLOCKS_PER_SLICE - nvmeBlockOffset;
	else
		tempNumOfNvmeBlock = requestedNvmeBlock;

	reqSlotTag = GetFromFreeReqQ();

	reqPoolPtr->reqPool[reqSlotTag].txId = txId;
	reqPoolPtr->reqPool[reqSlotTag].reqType = REQ_TYPE_SLICE;
	reqPoolPtr->reqPool[reqSlotTag].reqCode = reqCode;
	reqPoolPtr->reqPool[reqSlotTag].nvmeCmdSlotTag = cmdSlotTag;
	reqPoolPtr->reqPool[reqSlotTag].logicalSliceAddr = tempLsa;
	reqPoolPtr->reqPool[reqSlotTag].nvmeDmaInfo.startIndex = nvmeDmaStartIndex;
	reqPoolPtr->reqPool[reqSlotTag].nvmeDmaInfo.nvmeBlockOffset =
			nvmeBlockOffset;
	reqPoolPtr->reqPool[reqSlotTag].nvmeDmaInfo.numOfNvmeBlock =
			tempNumOfNvmeBlock;

	PutToSliceReqQ(reqSlotTag);

	tempLsa++;
	transCounter++;
	nvmeDmaStartIndex += tempNumOfNvmeBlock;

	//transform continue
	while (transCounter < loop) {
		nvmeBlockOffset = 0;
		tempNumOfNvmeBlock = NVME_BLOCKS_PER_SLICE;

		reqSlotTag = GetFromFreeReqQ();

		reqPoolPtr->reqPool[reqSlotTag].txId = txId;
		reqPoolPtr->reqPool[reqSlotTag].reqType = REQ_TYPE_SLICE;
		reqPoolPtr->reqPool[reqSlotTag].reqCode = reqCode;
		reqPoolPtr->reqPool[reqSlotTag].nvmeCmdSlotTag = cmdSlotTag;
		reqPoolPtr->reqPool[reqSlotTag].logicalSliceAddr = tempLsa;
		reqPoolPtr->reqPool[reqSlotTag].nvmeDmaInfo.startIndex =
				nvmeDmaStartIndex;
		reqPoolPtr->reqPool[reqSlotTag].nvmeDmaInfo.nvmeBlockOffset =
				nvmeBlockOffset;
		reqPoolPtr->reqPool[reqSlotTag].nvmeDmaInfo.numOfNvmeBlock =
				tempNumOfNvmeBlock;

		PutToSliceReqQ(reqSlotTag);

		tempLsa++;
		transCounter++;
		nvmeDmaStartIndex += tempNumOfNvmeBlock;
	}

	//last transform
	nvmeBlockOffset = 0;
	tempNumOfNvmeBlock =
			(startLba + requestedNvmeBlock) % NVME_BLOCKS_PER_SLICE;
	if ((tempNumOfNvmeBlock == 0) || (loop == 0))
		return;

	reqSlotTag = GetFromFreeReqQ();

	reqPoolPtr->reqPool[reqSlotTag].txId = txId;
	reqPoolPtr->reqPool[reqSlotTag].reqType = REQ_TYPE_SLICE;
	reqPoolPtr->reqPool[reqSlotTag].reqCode = reqCode;
	reqPoolPtr->reqPool[reqSlotTag].nvmeCmdSlotTag = cmdSlotTag;
	reqPoolPtr->reqPool[reqSlotTag].logicalSliceAddr = tempLsa;
	reqPoolPtr->reqPool[reqSlotTag].nvmeDmaInfo.startIndex = nvmeDmaStartIndex;
	reqPoolPtr->reqPool[reqSlotTag].nvmeDmaInfo.nvmeBlockOffset =
			nvmeBlockOffset;
	reqPoolPtr->reqPool[reqSlotTag].nvmeDmaInfo.numOfNvmeBlock =
			tempNumOfNvmeBlock;

	PutToSliceReqQ(reqSlotTag);
}

void EvictDataBufEntry(unsigned int originReqSlotTag) {
	unsigned int reqSlotTag, virtualSliceAddr, dataBufEntry;

	dataBufEntry = reqPoolPtr->reqPool[originReqSlotTag].dataBufInfo.entry;
	if (dataBufMapPtr->dataBuf[dataBufEntry].dirty == DATA_BUF_DIRTY) {
		reqSlotTag = GetFromFreeReqQ();
		virtualSliceAddr = AddrTransWrite(
				dataBufMapPtr->dataBuf[dataBufEntry].logicalSliceAddr);

		reqPoolPtr->reqPool[reqSlotTag].reqType = REQ_TYPE_NAND;
		reqPoolPtr->reqPool[reqSlotTag].reqCode = REQ_CODE_WRITE;
		reqPoolPtr->reqPool[reqSlotTag].nvmeCmdSlotTag =
				reqPoolPtr->reqPool[originReqSlotTag].nvmeCmdSlotTag;
		reqPoolPtr->reqPool[reqSlotTag].logicalSliceAddr =
				dataBufMapPtr->dataBuf[dataBufEntry].logicalSliceAddr;
		reqPoolPtr->reqPool[reqSlotTag].reqOpt.dataBufFormat =
		REQ_OPT_DATA_BUF_ENTRY;
		reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandAddr = REQ_OPT_NAND_ADDR_VSA;
		reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandEcc = REQ_OPT_NAND_ECC_ON;
		reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandEccWarning =
		REQ_OPT_NAND_ECC_WARNING_ON;
		reqPoolPtr->reqPool[reqSlotTag].reqOpt.rowAddrDependencyCheck =
		REQ_OPT_ROW_ADDR_DEPENDENCY_CHECK;
		reqPoolPtr->reqPool[reqSlotTag].reqOpt.blockSpace =
		REQ_OPT_BLOCK_SPACE_MAIN;
		reqPoolPtr->reqPool[reqSlotTag].dataBufInfo.entry = dataBufEntry;
		UpdateDataBufEntryInfoBlockingReq(dataBufEntry, reqSlotTag);
		reqPoolPtr->reqPool[reqSlotTag].nandInfo.virtualSliceAddr =
				virtualSliceAddr;

		//SelectLowLevelReqQ(reqSlotTag);
		SelectLowLevelReqQ(reqSlotTag, EvictDataBufEntryFUN);

		dataBufMapPtr->dataBuf[dataBufEntry].dirty = DATA_BUF_CLEAN;
	}
}

void DataReadFromNand(unsigned int originReqSlotTag) {
	unsigned int reqSlotTag, virtualSliceAddr;

	virtualSliceAddr = AddrTransRead(
			reqPoolPtr->reqPool[originReqSlotTag].logicalSliceAddr);

	if (virtualSliceAddr != VSA_FAIL) {
		reqSlotTag = GetFromFreeReqQ();

		reqPoolPtr->reqPool[reqSlotTag].reqType = REQ_TYPE_NAND;
		reqPoolPtr->reqPool[reqSlotTag].reqCode = REQ_CODE_READ;
		reqPoolPtr->reqPool[reqSlotTag].nvmeCmdSlotTag =
				reqPoolPtr->reqPool[originReqSlotTag].nvmeCmdSlotTag;
		reqPoolPtr->reqPool[reqSlotTag].logicalSliceAddr =
				reqPoolPtr->reqPool[originReqSlotTag].logicalSliceAddr;
		reqPoolPtr->reqPool[reqSlotTag].reqOpt.dataBufFormat =
		REQ_OPT_DATA_BUF_ENTRY;
		reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandAddr = REQ_OPT_NAND_ADDR_VSA;
		reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandEcc = REQ_OPT_NAND_ECC_ON;
		reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandEccWarning =
		REQ_OPT_NAND_ECC_WARNING_ON;
		reqPoolPtr->reqPool[reqSlotTag].reqOpt.rowAddrDependencyCheck =
		REQ_OPT_ROW_ADDR_DEPENDENCY_CHECK;
		reqPoolPtr->reqPool[reqSlotTag].reqOpt.blockSpace =
		REQ_OPT_BLOCK_SPACE_MAIN;

		reqPoolPtr->reqPool[reqSlotTag].dataBufInfo.entry =
				reqPoolPtr->reqPool[originReqSlotTag].dataBufInfo.entry;
		UpdateDataBufEntryInfoBlockingReq(
				reqPoolPtr->reqPool[reqSlotTag].dataBufInfo.entry, reqSlotTag);
		reqPoolPtr->reqPool[reqSlotTag].nandInfo.virtualSliceAddr =
				virtualSliceAddr;

		//SelectLowLevelReqQ(reqSlotTag);
		SelectLowLevelReqQ(reqSlotTag, DataReadFromNandFUN);

	}
}

void ReqTransSliceToLowLevel() {
	unsigned int reqSlotTag, dataBufEntry;

	while (sliceReqQ.headReq != REQ_SLOT_TAG_NONE) {
		reqSlotTag = GetFromSliceReqQ();
		if (reqSlotTag == REQ_SLOT_TAG_FAIL)
			return;

//		if (reqPoolPtr->reqPool[reqSlotTag].txId != 0) {
//			xil_printf("tran write: txid = %d\r\n",
//					reqPoolPtr->reqPool[reqSlotTag].txId);
//		}

//allocate a data buffer entry for this request
		dataBufEntry = CheckDataBufHit(reqSlotTag);
		if (dataBufEntry != DATA_BUF_FAIL) {
			//data buffer hit
			reqPoolPtr->reqPool[reqSlotTag].dataBufInfo.entry = dataBufEntry;
//			if (reqPoolPtr->reqPool[reqSlotTag].txId != 0) {
//				xil_printf(
//						"data buffer hit. logicalSliceAddr = %d, virtualSliceAddr = %d, txId = %d\r\n",
//						reqPoolPtr->reqPool[reqSlotTag].logicalSliceAddr,
//						logicalSliceMapPtr->logicalSlice[reqPoolPtr->reqPool[reqSlotTag].logicalSliceAddr].virtualSliceAddr,
//						reqPoolPtr->reqPool[reqSlotTag].txId);
////				xil_printf("tran write: add to in-process table : %u\r\n",
////						reqPoolPtr->reqPool[reqSlotTag].txId);
//				add_tx_write_to_txid_list(reqPoolPtr->reqPool[reqSlotTag].txId,
//						reqPoolPtr->reqPool[reqSlotTag].logicalSliceAddr,
//						logicalSliceMapPtr->logicalSlice[reqPoolPtr->reqPool[reqSlotTag].logicalSliceAddr].virtualSliceAddr);
//			}

		} else {
			//data buffer miss, allocate a new buffer entry
			dataBufEntry = AllocateDataBuf();
			reqPoolPtr->reqPool[reqSlotTag].dataBufInfo.entry = dataBufEntry;

			//clear the allocated data buffer entry being used by a previous request
			EvictDataBufEntry(reqSlotTag);

			//update meta-data of the allocated data buffer entry
			dataBufMapPtr->dataBuf[dataBufEntry].logicalSliceAddr =
					reqPoolPtr->reqPool[reqSlotTag].logicalSliceAddr;
			PutToDataBufHashList(dataBufEntry);

			if (reqPoolPtr->reqPool[reqSlotTag].reqCode == REQ_CODE_READ)
				DataReadFromNand(reqSlotTag);
			else if (reqPoolPtr->reqPool[reqSlotTag].reqCode == REQ_CODE_WRITE)
				if (reqPoolPtr->reqPool[reqSlotTag].nvmeDmaInfo.numOfNvmeBlock
						!= NVME_BLOCKS_PER_SLICE) //for read modify write
					DataReadFromNand(reqSlotTag);
		}

		//add by yanjie.tan 2019.4.10 在此处加入inprocesstable可以考虑到所有情况而不发生遗漏
		// In-Process Table Structure-> request metadata (Fig.4 in paper)
		if (reqPoolPtr->reqPool[reqSlotTag].txId != 0) {
//			xil_printf(
//					"tran write: add to in-process table. lpn = %d, ppn = %d, txId = %d, req_code = %s\r\n",
//					reqPoolPtr->reqPool[reqSlotTag].logicalSliceAddr,
//					logicalSliceMapPtr->logicalSlice[reqPoolPtr->reqPool[reqSlotTag].logicalSliceAddr].virtualSliceAddr,
//					reqPoolPtr->reqPool[reqSlotTag].txId,
//					reqPoolPtr->reqPool[reqSlotTag].reqCode == REQ_CODE_WRITE ?
//							"write" : "read");
//			xil_printf("tran write: add to in-process table : %u\r\n",
//					reqPoolPtr->reqPool[reqSlotTag].txId);
			add_tx_write_to_txid_list(reqPoolPtr->reqPool[reqSlotTag].txId,
					reqPoolPtr->reqPool[reqSlotTag].logicalSliceAddr,
					logicalSliceMapPtr->logicalSlice[reqPoolPtr->reqPool[reqSlotTag].logicalSliceAddr].virtualSliceAddr);

//			add_phys_page_to_log_list(reqPoolPtr->reqPool[reqSlotTag].txId,
//					reqPoolPtr->reqPool[reqSlotTag].logicalSliceAddr,
//					logicalSliceMapPtr->logicalSlice[reqPoolPtr->reqPool[reqSlotTag].logicalSliceAddr].virtualSliceAddr);

		}

		//transform this slice request to nvme request
		if (reqPoolPtr->reqPool[reqSlotTag].reqCode == REQ_CODE_WRITE) {
			dataBufMapPtr->dataBuf[dataBufEntry].dirty = DATA_BUF_DIRTY;
			reqPoolPtr->reqPool[reqSlotTag].reqCode = REQ_CODE_RxDMA;
			add_phys_page_to_log_list(reqPoolPtr->reqPool[reqSlotTag].txId,
					reqPoolPtr->reqPool[reqSlotTag].logicalSliceAddr,
					logicalSliceMapPtr->logicalSlice[reqPoolPtr->reqPool[reqSlotTag].logicalSliceAddr].virtualSliceAddr);

		} else if (reqPoolPtr->reqPool[reqSlotTag].reqCode == REQ_CODE_READ)
			reqPoolPtr->reqPool[reqSlotTag].reqCode = REQ_CODE_TxDMA;
		else
			assert(!"[WARNING] Not supported reqCode. [WARNING]");

		reqPoolPtr->reqPool[reqSlotTag].reqType = REQ_TYPE_NVME_DMA;
		reqPoolPtr->reqPool[reqSlotTag].reqOpt.dataBufFormat =
		REQ_OPT_DATA_BUF_ENTRY;

		UpdateDataBufEntryInfoBlockingReq(dataBufEntry, reqSlotTag);

		//SelectLowLevelReqQ(reqSlotTag);
		SelectLowLevelReqQ(reqSlotTag, ReqTransSliceToLowLevelFUN);
	}
}

unsigned int CheckBufDep(unsigned int reqSlotTag) {
	if (reqPoolPtr->reqPool[reqSlotTag].prevBlockingReq == REQ_SLOT_TAG_NONE)
		return BUF_DEPENDENCY_REPORT_PASS;
	else
		return BUF_DEPENDENCY_REPORT_BLOCKED;
}

unsigned int CheckRowAddrDep(unsigned int reqSlotTag,
		unsigned int checkRowAddrDepOpt) {
	unsigned int dieNo, chNo, wayNo, blockNo, pageNo;

	if (reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandAddr == REQ_OPT_NAND_ADDR_VSA) {
		dieNo = Vsa2VdieTranslation(
				reqPoolPtr->reqPool[reqSlotTag].nandInfo.virtualSliceAddr);
		chNo = Vdie2PchTranslation(dieNo);
		wayNo = Vdie2PwayTranslation(dieNo);
		blockNo = Vsa2VblockTranslation(
				reqPoolPtr->reqPool[reqSlotTag].nandInfo.virtualSliceAddr);
		pageNo = Vsa2VpageTranslation(
				reqPoolPtr->reqPool[reqSlotTag].nandInfo.virtualSliceAddr);
	} else
		assert(!"[WARNING] Not supported reqOpt-nandAddress [WARNING]");

	if (reqPoolPtr->reqPool[reqSlotTag].reqCode == REQ_CODE_READ) {
		if (checkRowAddrDepOpt == ROW_ADDR_DEPENDENCY_CHECK_OPT_SELECT) {
			if (rowAddrDependencyTablePtr->block[chNo][wayNo][blockNo].blockedEraseReqFlag)
				SyncReleaseEraseReq(chNo, wayNo, blockNo);

			if (pageNo
					< rowAddrDependencyTablePtr->block[chNo][wayNo][blockNo].permittedProgPage)
				return ROW_ADDR_DEPENDENCY_REPORT_PASS;

			rowAddrDependencyTablePtr->block[chNo][wayNo][blockNo].blockedReadReqCnt++;
		} else if (checkRowAddrDepOpt == ROW_ADDR_DEPENDENCY_CHECK_OPT_RELEASE) {
			if (pageNo
					< rowAddrDependencyTablePtr->block[chNo][wayNo][blockNo].permittedProgPage) {
				rowAddrDependencyTablePtr->block[chNo][wayNo][blockNo].blockedReadReqCnt--;
				return ROW_ADDR_DEPENDENCY_REPORT_PASS;
			}
		} else
			assert(!"[WARNING] Not supported checkRowAddrDepOpt [WARNING]");
	} else if (reqPoolPtr->reqPool[reqSlotTag].reqCode == REQ_CODE_WRITE) {
		if (pageNo
				== rowAddrDependencyTablePtr->block[chNo][wayNo][blockNo].permittedProgPage) {
			rowAddrDependencyTablePtr->block[chNo][wayNo][blockNo].permittedProgPage++;

			return ROW_ADDR_DEPENDENCY_REPORT_PASS;
		}
	} else if (reqPoolPtr->reqPool[reqSlotTag].reqCode == REQ_CODE_ERASE) {
		if (rowAddrDependencyTablePtr->block[chNo][wayNo][blockNo].permittedProgPage
				== reqPoolPtr->reqPool[reqSlotTag].nandInfo.programmedPageCnt)
			if (rowAddrDependencyTablePtr->block[chNo][wayNo][blockNo].blockedReadReqCnt
					== 0) {
				rowAddrDependencyTablePtr->block[chNo][wayNo][blockNo].permittedProgPage =
						0;
				rowAddrDependencyTablePtr->block[chNo][wayNo][blockNo].blockedEraseReqFlag =
						0;

				return ROW_ADDR_DEPENDENCY_REPORT_PASS;
			}

		if (checkRowAddrDepOpt == ROW_ADDR_DEPENDENCY_CHECK_OPT_SELECT)
			rowAddrDependencyTablePtr->block[chNo][wayNo][blockNo].blockedEraseReqFlag =
					1;
		else if (checkRowAddrDepOpt == ROW_ADDR_DEPENDENCY_CHECK_OPT_RELEASE) {
			//pass, go to return
		} else
			assert(!"[WARNING] Not supported checkRowAddrDepOpt [WARNING]");
	} else
		assert(!"[WARNING] Not supported reqCode [WARNING]");

	return ROW_ADDR_DEPENDENCY_REPORT_BLOCKED;
}

unsigned int UpdateRowAddrDepTableForBufBlockedReq(unsigned int reqSlotTag) {
	unsigned int dieNo, chNo, wayNo, blockNo, pageNo, bufDepCheckReport;

	if (reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandAddr == REQ_OPT_NAND_ADDR_VSA) {
		dieNo = Vsa2VdieTranslation(
				reqPoolPtr->reqPool[reqSlotTag].nandInfo.virtualSliceAddr);
		chNo = Vdie2PchTranslation(dieNo);
		wayNo = Vdie2PwayTranslation(dieNo);
		blockNo = Vsa2VblockTranslation(
				reqPoolPtr->reqPool[reqSlotTag].nandInfo.virtualSliceAddr);
		pageNo = Vsa2VpageTranslation(
				reqPoolPtr->reqPool[reqSlotTag].nandInfo.virtualSliceAddr);
	} else
		assert(!"[WARNING] Not supported reqOpt-nandAddress [WARNING]");

	if (reqPoolPtr->reqPool[reqSlotTag].reqCode == REQ_CODE_READ) {
		if (rowAddrDependencyTablePtr->block[chNo][wayNo][blockNo].blockedEraseReqFlag) {
			SyncReleaseEraseReq(chNo, wayNo, blockNo);

			bufDepCheckReport = CheckBufDep(reqSlotTag);
			if (bufDepCheckReport == BUF_DEPENDENCY_REPORT_PASS) {
				if (pageNo
						< rowAddrDependencyTablePtr->block[chNo][wayNo][blockNo].permittedProgPage)
					PutToNandReqQ(reqSlotTag, chNo, wayNo);
				else {
					rowAddrDependencyTablePtr->block[chNo][wayNo][blockNo].blockedReadReqCnt++;
					PutToBlockedByRowAddrDepReqQ(reqSlotTag, chNo, wayNo);
				}

				return ROW_ADDR_DEPENDENCY_TABLE_UPDATE_REPORT_SYNC;
			}
		}
		rowAddrDependencyTablePtr->block[chNo][wayNo][blockNo].blockedReadReqCnt++;
	} else if (reqPoolPtr->reqPool[reqSlotTag].reqCode == REQ_CODE_ERASE)
		rowAddrDependencyTablePtr->block[chNo][wayNo][blockNo].blockedEraseReqFlag =
				1;

	return ROW_ADDR_DEPENDENCY_TABLE_UPDATE_REPORT_DONE;
}

void SelectLowLevelReqQ(unsigned int reqSlotTag, unsigned int fun) {
	unsigned int dieNo, chNo, wayNo, bufDepCheckReport, rowAddrDepCheckReport,
			rowAddrDepTableUpdateReport;

	bufDepCheckReport = CheckBufDep(reqSlotTag);
//	if (fun == ReqTransSliceToLowLevelFUN) {
//		xil_printf("bufDepCheckReport = %s \r\n",
//				bufDepCheckReport == BUF_DEPENDENCY_REPORT_PASS ?
//						"BUF_DEPENDENCY_REPORT_PASS" :
//						"BUF_DEPENDENCY_REPORT_BLOCKED");
//	}

	if (bufDepCheckReport == BUF_DEPENDENCY_REPORT_PASS) {
		if (reqPoolPtr->reqPool[reqSlotTag].reqType == REQ_TYPE_NVME_DMA) {
			if (reqPoolPtr->reqPool[reqSlotTag].reqCode == REQ_CODE_RxDMA) {
				//add by yanjie.tan  pages_written_to_flash count
				pages_written_to_flash_rxdma++;
//				if (pages_written_to_flash_rxdma % 100 == 0) {
//					xil_printf(
//							"pages_written_to_flash_num_rxdma = %lu, bufDepCheckReport = %s\r\n",
//							pages_written_to_flash_rxdma,
//							bufDepCheckReport == BUF_DEPENDENCY_REPORT_PASS ?
//									"BUF_DEPENDENCY_REPORT_PASS" :
//									"BUF_DEPENDENCY_REPORT_BLOCKED");
//				}

				switch (fun) {
				case DataReadFromNandFUN:
					drfn++;
					break;
				case EraseBlockFUN:
					eb++;
					break;
				case EraseTotalBlockSpaceFUN:
					etbs++;
					break;
				case EraseUserBlockSpaceFUN:
					eubs++;
					break;
				case EvictDataBufEntryFUN:
					edbe++;
					break;
				case FindBadBlockFUN:
					fbb++;
					break;
				case GarbageCollectionFUN:
					gc++;
					break;
				case InitNandArrayFUN:
					ina++;
					break;
				case ReadBadBlockTableFUN:
					rbbt++;
					break;
				case ReqTransSliceToLowLevelFUN:
					rtstll++;
					break;
				case SaveBadBlockTableFUN:
					sbbt++;
					break;
				case write_log_to_ssdFUN:
					wlts++;
					break;
				default:
					xil_printf("there is other fun\r\n");
				}
				if (edbe != 0 && (edbe % 10000 == 0)) {
//					xil_printf(
//							"drfn = %lu, eb = %lu, etbs = %lu, eubs = %lu, edbe = %lu, fbb = %lu, gc = %lu, ina = %lu, rbbt = %lu, rtstll = %lu, sbbt = %lu, wlts = %lu, txId = %lu\r\n",
//							drfn, eb, etbs, eubs, edbe, fbb, gc, ina, rbbt,
//							rtstll, sbbt, wlts,
//							reqPoolPtr->reqPool[reqSlotTag].txId);
//					xil_printf(
//							"drfn = %lu, edbe = %lu, gc = %lu, rtstll = %lu, wlts = %lu, num_nand = %lu, num_log = %lu, num_rxdma = %lu, txId = %lu\r\n",
//							drfn, edbe, gc, rtstll, wlts,
//							pages_written_to_flash_nand,
//							pages_written_to_flash_log,
//							pages_written_to_flash_rxdma,
//							reqPoolPtr->reqPool[reqSlotTag].txId);
					xil_printf("pages_nand = %lu, pages_log = %lu, pages_rxdma = %lu, edbe = %lu, gc = %lu, wlts = %lu\r\n", pages_written_to_flash_nand, pages_written_to_flash_log, pages_written_to_flash_rxdma, edbe, gc, wlts);
				}

			}
			IssueNvmeDmaReq(reqSlotTag);
			PutToNvmeDmaReqQ(reqSlotTag);
		} else if (reqPoolPtr->reqPool[reqSlotTag].reqType == REQ_TYPE_NAND) {

			//add by yanjie.tan  pages_written_to_flash count
			if (reqPoolPtr->reqPool[reqSlotTag].reqCode == REQ_CODE_WRITE) {
				pages_written_to_flash_nand++;
				if (pages_written_to_flash_nand % 10000 == 0) {
//					xil_printf(
//							"pages_written_to_flash_num_nand = %lu, edbe = %lu\r\n",
//							pages_written_to_flash_nand, edbe);
					xil_printf("pages_nand = %lu, pages_log = %lu, pages_rxdma = %lu, edbe = %lu, gc = %lu, wlts = %lu\r\n", pages_written_to_flash_nand, pages_written_to_flash_log, pages_written_to_flash_rxdma, edbe, gc, wlts);

				}
				switch (fun) {
				case DataReadFromNandFUN:
					drfn++;
					break;
				case EraseBlockFUN:
					eb++;
					break;
				case EraseTotalBlockSpaceFUN:
					etbs++;
					break;
				case EraseUserBlockSpaceFUN:
					eubs++;
					break;
				case EvictDataBufEntryFUN:
					edbe++;
					break;
				case FindBadBlockFUN:
					fbb++;
					break;
				case GarbageCollectionFUN:
					gc++;
					break;
				case InitNandArrayFUN:
					ina++;
					break;
				case ReadBadBlockTableFUN:
					rbbt++;
					break;
				case ReqTransSliceToLowLevelFUN:
					rtstll++;
					break;
				case SaveBadBlockTableFUN:
					sbbt++;
					break;
				case write_log_to_ssdFUN:
					wlts++;
					break;
				default:
					xil_printf("there is other fun\r\n");
				}
				if (edbe != 0 && (edbe % 10000 == 0)) {
//					xil_printf(
//							"drfn = %lu, eb = %lu, etbs = %lu, eubs = %lu, edbe = %lu, fbb = %lu, gc = %lu, ina = %lu, rbbt = %lu, rtstll = %lu, sbbt = %lu, wlts = %lu, txId = %lu\r\n",
//							drfn, eb, etbs, eubs, edbe, fbb, gc, ina, rbbt,
//							rtstll, sbbt, wlts,
//							reqPoolPtr->reqPool[reqSlotTag].txId);
//					xil_printf(
//							"drfn = %lu, edbe = %lu, gc = %lu, rtstll = %lu, wlts = %lu, num_nand = %lu, num_log = %lu, num_rxdma = %lu, txId = %lu\r\n",
//							drfn, edbe, gc, rtstll, wlts,
//							pages_written_to_flash_nand,
//							pages_written_to_flash_log,
//							pages_written_to_flash_rxdma,
//							reqPoolPtr->reqPool[reqSlotTag].txId);
					xil_printf("pages_nand = %lu, pages_log = %lu, pages_rxdma = %lu, edbe = %lu, gc = %lu, wlts = %lu\r\n", pages_written_to_flash_nand, pages_written_to_flash_log, pages_written_to_flash_rxdma, edbe, gc, wlts);

				}
			}

			if (reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandAddr
					== REQ_OPT_NAND_ADDR_VSA) {
				dieNo =
						Vsa2VdieTranslation(
								reqPoolPtr->reqPool[reqSlotTag].nandInfo.virtualSliceAddr);
				chNo = Vdie2PchTranslation(dieNo);
				wayNo = Vdie2PwayTranslation(dieNo);
			} else if (reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandAddr
					== REQ_OPT_NAND_ADDR_PHY_ORG) {
				chNo = reqPoolPtr->reqPool[reqSlotTag].nandInfo.physicalCh;
				wayNo = reqPoolPtr->reqPool[reqSlotTag].nandInfo.physicalWay;
			} else
				assert(!"[WARNING] Not supported reqOpt-nandAddress [WARNING]");

			if (reqPoolPtr->reqPool[reqSlotTag].reqOpt.rowAddrDependencyCheck
					== REQ_OPT_ROW_ADDR_DEPENDENCY_CHECK) {
				rowAddrDepCheckReport = CheckRowAddrDep(reqSlotTag,
				ROW_ADDR_DEPENDENCY_CHECK_OPT_SELECT);

				if (rowAddrDepCheckReport == ROW_ADDR_DEPENDENCY_REPORT_PASS)
					PutToNandReqQ(reqSlotTag, chNo, wayNo);
				else if (rowAddrDepCheckReport
						== ROW_ADDR_DEPENDENCY_REPORT_BLOCKED)
					PutToBlockedByRowAddrDepReqQ(reqSlotTag, chNo, wayNo);
				else
					assert(!"[WARNING] Not supported report [WARNING]");
			} else if (reqPoolPtr->reqPool[reqSlotTag].reqOpt.rowAddrDependencyCheck
					== REQ_OPT_ROW_ADDR_DEPENDENCY_NONE)
				PutToNandReqQ(reqSlotTag, chNo, wayNo);
			else
				assert(!"[WARNING] Not supported reqOpt [WARNING]");

		} else
			assert(!"[WARNING] Not supported reqType [WARNING]");
	} else if (bufDepCheckReport == BUF_DEPENDENCY_REPORT_BLOCKED) {
		if (reqPoolPtr->reqPool[reqSlotTag].reqType == REQ_TYPE_NAND) {

			//add by yanjie.tan  pages_written_to_flash count
			pages_written_to_flash_log++;
//			if (pages_written_to_flash_log % 1000 == 0) {
//				xil_printf(
//						"pages_written_to_flash_num_log = %lu, bufDepCheckReport = %s\r\n",
//						pages_written_to_flash_log,
//						bufDepCheckReport == BUF_DEPENDENCY_REPORT_PASS ?
//								"BUF_DEPENDENCY_REPORT_PASS" :
//								"BUF_DEPENDENCY_REPORT_BLOCKED");
//			}
//			if (pages_written_to_flash_nand % 10000 == 0) {
//				xil_printf("pages_written_to_flash_num_nand = %lu\r\n",
//						pages_written_to_flash_nand);
//			}
			switch (fun) {
			case DataReadFromNandFUN:
				drfn++;
				break;
			case EraseBlockFUN:
				eb++;
				break;
			case EraseTotalBlockSpaceFUN:
				etbs++;
				break;
			case EraseUserBlockSpaceFUN:
				eubs++;
				break;
			case EvictDataBufEntryFUN:
				edbe++;
				break;
			case FindBadBlockFUN:
				fbb++;
				break;
			case GarbageCollectionFUN:
				gc++;
				break;
			case InitNandArrayFUN:
				ina++;
				break;
			case ReadBadBlockTableFUN:
				rbbt++;
				break;
			case ReqTransSliceToLowLevelFUN:
				rtstll++;
				break;
			case SaveBadBlockTableFUN:
				sbbt++;
				break;
			case write_log_to_ssdFUN:
				wlts++;
				break;
			default:
				xil_printf("there is other fun\r\n");
			}
			if (edbe != 0 && (edbe % 10000 == 0)) {
//				xil_printf(
//						"drfn = %lu, eb = %lu, etbs = %lu, eubs = %lu, edbe = %lu, fbb = %lu, gc = %lu, ina = %lu, rbbt = %lu, rtstll = %lu, sbbt = %lu, wlts = %lu, txId = %lu\r\n",
//						drfn, eb, etbs, eubs, edbe, fbb, gc, ina, rbbt, rtstll,
//						sbbt, wlts, reqPoolPtr->reqPool[reqSlotTag].txId);
//				xil_printf(
//						"drfn = %lu, edbe = %lu, gc = %lu, rtstll = %lu, wlts = %lu, num_nand = %lu, num_log = %lu, num_rxdma = %lu, txId = %lu\r\n",
//						drfn, edbe, gc, rtstll, wlts,
//						pages_written_to_flash_nand, pages_written_to_flash_log,
//						pages_written_to_flash_rxdma,
//						reqPoolPtr->reqPool[reqSlotTag].txId);
				xil_printf("pages_nand = %lu, pages_log = %lu, pages_rxdma = %lu, edbe = %lu, gc = %lu, wlts = %lu\r\n", pages_written_to_flash_nand, pages_written_to_flash_log, pages_written_to_flash_rxdma, edbe, gc, wlts);
			}

			if (reqPoolPtr->reqPool[reqSlotTag].reqOpt.rowAddrDependencyCheck
					== REQ_OPT_ROW_ADDR_DEPENDENCY_CHECK) {
				rowAddrDepTableUpdateReport =
						UpdateRowAddrDepTableForBufBlockedReq(reqSlotTag);

				if (rowAddrDepTableUpdateReport
						== ROW_ADDR_DEPENDENCY_TABLE_UPDATE_REPORT_DONE) {
					//pass, go to PutToBlockedByBufDepReqQ
				} else if (rowAddrDepTableUpdateReport
						== ROW_ADDR_DEPENDENCY_TABLE_UPDATE_REPORT_SYNC)
					return;
				else
					assert(!"[WARNING] Not supported report [WARNING]");
			}
		}

		PutToBlockedByBufDepReqQ(reqSlotTag);
	} else
		assert(!"[WARNING] Not supported report [WARNING]");
}

void ReleaseBlockedByBufDepReq(unsigned int reqSlotTag) {
	unsigned int targetReqSlotTag, dieNo, chNo, wayNo, rowAddrDepCheckReport;

	targetReqSlotTag = REQ_SLOT_TAG_NONE;
	if (reqPoolPtr->reqPool[reqSlotTag].nextBlockingReq != REQ_SLOT_TAG_NONE) {
		targetReqSlotTag = reqPoolPtr->reqPool[reqSlotTag].nextBlockingReq;
		reqPoolPtr->reqPool[targetReqSlotTag].prevBlockingReq =
		REQ_SLOT_TAG_NONE;
		reqPoolPtr->reqPool[reqSlotTag].nextBlockingReq = REQ_SLOT_TAG_NONE;
	}

	if (reqPoolPtr->reqPool[reqSlotTag].reqOpt.dataBufFormat
			== REQ_OPT_DATA_BUF_ENTRY) {
		if (dataBufMapPtr->dataBuf[reqPoolPtr->reqPool[reqSlotTag].dataBufInfo.entry].blockingReqTail
				== reqSlotTag)
			dataBufMapPtr->dataBuf[reqPoolPtr->reqPool[reqSlotTag].dataBufInfo.entry].blockingReqTail =
			REQ_SLOT_TAG_NONE;
	} else if (reqPoolPtr->reqPool[reqSlotTag].reqOpt.dataBufFormat
			== REQ_OPT_DATA_BUF_TEMP_ENTRY) {
		if (tempDataBufMapPtr->tempDataBuf[reqPoolPtr->reqPool[reqSlotTag].dataBufInfo.entry].blockingReqTail
				== reqSlotTag)
			tempDataBufMapPtr->tempDataBuf[reqPoolPtr->reqPool[reqSlotTag].dataBufInfo.entry].blockingReqTail =
			REQ_SLOT_TAG_NONE;
	}

	if ((targetReqSlotTag != REQ_SLOT_TAG_NONE)
			&& (reqPoolPtr->reqPool[targetReqSlotTag].reqQueueType
					== REQ_QUEUE_TYPE_BLOCKED_BY_BUF_DEP)) {
		SelectiveGetFromBlockedByBufDepReqQ(targetReqSlotTag);

		if (reqPoolPtr->reqPool[targetReqSlotTag].reqType == REQ_TYPE_NVME_DMA) {
			IssueNvmeDmaReq(targetReqSlotTag);
			PutToNvmeDmaReqQ(targetReqSlotTag);
		} else if (reqPoolPtr->reqPool[targetReqSlotTag].reqType
				== REQ_TYPE_NAND) {
			if (reqPoolPtr->reqPool[targetReqSlotTag].reqOpt.nandAddr
					== REQ_OPT_NAND_ADDR_VSA) {
				dieNo =
						Vsa2VdieTranslation(
								reqPoolPtr->reqPool[targetReqSlotTag].nandInfo.virtualSliceAddr);
				chNo = Vdie2PchTranslation(dieNo);
				wayNo = Vdie2PwayTranslation(dieNo);
			} else
				assert(!"[WARNING] Not supported reqOpt-nandAddress [WARNING]");

			if (reqPoolPtr->reqPool[targetReqSlotTag].reqOpt.rowAddrDependencyCheck
					== REQ_OPT_ROW_ADDR_DEPENDENCY_CHECK) {
				rowAddrDepCheckReport = CheckRowAddrDep(targetReqSlotTag,
				ROW_ADDR_DEPENDENCY_CHECK_OPT_RELEASE);

				if (rowAddrDepCheckReport == ROW_ADDR_DEPENDENCY_REPORT_PASS)
					PutToNandReqQ(targetReqSlotTag, chNo, wayNo);
				else if (rowAddrDepCheckReport
						== ROW_ADDR_DEPENDENCY_REPORT_BLOCKED)
					PutToBlockedByRowAddrDepReqQ(targetReqSlotTag, chNo, wayNo);
				else
					assert(!"[WARNING] Not supported report [WARNING]");
			} else if (reqPoolPtr->reqPool[targetReqSlotTag].reqOpt.rowAddrDependencyCheck
					== REQ_OPT_ROW_ADDR_DEPENDENCY_NONE)
				PutToNandReqQ(targetReqSlotTag, chNo, wayNo);
			else
				assert(!"[WARNING] Not supported reqOpt [WARNING]");
		}
	}
}

void ReleaseBlockedByRowAddrDepReq(unsigned int chNo, unsigned int wayNo) {
	unsigned int reqSlotTag, nextReq, rowAddrDepCheckReport;

	reqSlotTag = blockedByRowAddrDepReqQ[chNo][wayNo].headReq;

	while (reqSlotTag != REQ_SLOT_TAG_NONE) {
		nextReq = reqPoolPtr->reqPool[reqSlotTag].nextReq;

		if (reqPoolPtr->reqPool[reqSlotTag].reqOpt.rowAddrDependencyCheck
				== REQ_OPT_ROW_ADDR_DEPENDENCY_CHECK) {
			rowAddrDepCheckReport = CheckRowAddrDep(reqSlotTag,
			ROW_ADDR_DEPENDENCY_CHECK_OPT_RELEASE);

			if (rowAddrDepCheckReport == ROW_ADDR_DEPENDENCY_REPORT_PASS) {
				SelectiveGetFromBlockedByRowAddrDepReqQ(reqSlotTag, chNo,
						wayNo);
				PutToNandReqQ(reqSlotTag, chNo, wayNo);
			} else if (rowAddrDepCheckReport
					== ROW_ADDR_DEPENDENCY_REPORT_BLOCKED) {
				//pass, go to while loop
			} else
				assert(!"[WARNING] Not supported report [WARNING]");
		} else
			assert(!"[WARNING] Not supported reqOpt [WARNING]");

		reqSlotTag = nextReq;
	}
}

void IssueNvmeDmaReq(unsigned int reqSlotTag) {
	unsigned int devAddr, dmaIndex, numOfNvmeBlock;

	dmaIndex = reqPoolPtr->reqPool[reqSlotTag].nvmeDmaInfo.startIndex;
	devAddr = GenerateDataBufAddr(reqSlotTag);
	numOfNvmeBlock = 0;

	if (reqPoolPtr->reqPool[reqSlotTag].reqCode == REQ_CODE_RxDMA) {
		//add by yanjie.tan  pages_written_to_flash count
//		pages_written_to_flash_rxdma++;
//		if (pages_written_to_flash_rxdma % 10000 == 0) {
//			xil_printf("pages_written_to_flash_num_rxdma = %lu\r\n",
//					pages_written_to_flash_rxdma);
//		}

		while (numOfNvmeBlock
				< reqPoolPtr->reqPool[reqSlotTag].nvmeDmaInfo.numOfNvmeBlock) {
			if (reqPoolPtr->reqPool[reqSlotTag].txId != 0) {
				//xil_printf("tx_write here will return HOST_DMA_CMD_FIFO_REG_ADDR %u\r\n");
			}
			set_auto_rx_dma(reqPoolPtr->reqPool[reqSlotTag].nvmeCmdSlotTag,
					dmaIndex, devAddr, NVME_COMMAND_AUTO_COMPLETION_ON);

			numOfNvmeBlock++;
			dmaIndex++;
			devAddr += BYTES_PER_NVME_BLOCK;
		}
		reqPoolPtr->reqPool[reqSlotTag].nvmeDmaInfo.reqTail =
				g_hostDmaStatus.fifoTail.autoDmaRx;
		reqPoolPtr->reqPool[reqSlotTag].nvmeDmaInfo.overFlowCnt =
				g_hostDmaAssistStatus.autoDmaRxOverFlowCnt;
	} else if (reqPoolPtr->reqPool[reqSlotTag].reqCode == REQ_CODE_TxDMA) {
		while (numOfNvmeBlock
				< reqPoolPtr->reqPool[reqSlotTag].nvmeDmaInfo.numOfNvmeBlock) {
			set_auto_tx_dma(reqPoolPtr->reqPool[reqSlotTag].nvmeCmdSlotTag,
					dmaIndex, devAddr, NVME_COMMAND_AUTO_COMPLETION_ON);

			numOfNvmeBlock++;
			dmaIndex++;
			devAddr += BYTES_PER_NVME_BLOCK;
		}
		reqPoolPtr->reqPool[reqSlotTag].nvmeDmaInfo.reqTail =
				g_hostDmaStatus.fifoTail.autoDmaTx;
		reqPoolPtr->reqPool[reqSlotTag].nvmeDmaInfo.overFlowCnt =
				g_hostDmaAssistStatus.autoDmaTxOverFlowCnt;
	} else
		assert(!"[WARNING] Not supported reqCode [WARNING]");
}

void CheckDoneNvmeDmaReq() {
	unsigned int reqSlotTag, prevReq;
	unsigned int rxDone, txDone;

	reqSlotTag = nvmeDmaReqQ.tailReq;
	rxDone = 0;
	txDone = 0;

	while (reqSlotTag != REQ_SLOT_TAG_NONE) {
		prevReq = reqPoolPtr->reqPool[reqSlotTag].prevReq;

		if (reqPoolPtr->reqPool[reqSlotTag].reqCode == REQ_CODE_RxDMA) {
			if (!rxDone)
				rxDone =
						check_auto_rx_dma_partial_done(
								reqPoolPtr->reqPool[reqSlotTag].nvmeDmaInfo.reqTail,
								reqPoolPtr->reqPool[reqSlotTag].nvmeDmaInfo.overFlowCnt);

			if (rxDone)
				SelectiveGetFromNvmeDmaReqQ(reqSlotTag);
		} else {
			if (!txDone)
				txDone =
						check_auto_tx_dma_partial_done(
								reqPoolPtr->reqPool[reqSlotTag].nvmeDmaInfo.reqTail,
								reqPoolPtr->reqPool[reqSlotTag].nvmeDmaInfo.overFlowCnt);

			if (txDone)
				SelectiveGetFromNvmeDmaReqQ(reqSlotTag);
		}

		reqSlotTag = prevReq;
	}
}

