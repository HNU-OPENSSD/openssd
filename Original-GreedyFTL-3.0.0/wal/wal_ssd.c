/*
 * wal_ssd.c
 *
 *  Created on: 2019.1.21
 *      Author: lanyjdk
 */
#include "wal_ssd.h"

#include <assert.h>
#include <stdlib.h>
#include "../memory_map.h"
#include "xil_printf.h"
#include "../address_translation.h"

P_INPORCESS_TABLE_MAP inprocessTableMap;

P_LOG_LIST_MAP logListMap;

unsigned long write_to_ssd_count = 0;

void InitWALProtocol() {
	inprocessTableMap = (P_INPORCESS_TABLE_MAP) INPROCESS_TABLE_MAP_ADDR;

	logListMap = (P_LOG_LIST_MAP) LOG_LIST_MAP_ADDR;

	int i;
	for (i = 0; i < MAX_INPROCESS_TABLE_SIZE; i++) {
		inprocessTableMap->inprocess_table[i].lpnInsertPos = 0;
		inprocessTableMap->isTxIdValid[i] = 0;
	}

}

int search_ip_txtab_by_txid(unsigned int txId) {
	int i;
	for (i = 0; i < MAX_INPROCESS_TABLE_SIZE; i++) {
		int ip_txtab_txid = inprocessTableMap->txId[i];
		if (ip_txtab_txid == txId) {
			return i;
		}
	}

	return -1;
}

int find_free_pos_log_list() {
	int pos;

	for (pos = 0; pos < MAX_LOG_LIST_SIZE; pos++) {
		int is_valid = logListMap->isTablePosValid[pos];
		if ((pos == MAX_LOG_LIST_SIZE - 1) && (is_valid == 1)) {
			return -1;
		}

		if (is_valid == 1) {
			continue;
		}

		break;
	}

	return pos;
}

int find_free_pos_inprocesstable() {
	int pos;
	for (pos = 0; pos < MAX_INPROCESS_TABLE_SIZE; pos++) {
		int is_txid_valid = inprocessTableMap->isTxIdValid[pos];
		if ((pos == MAX_INPROCESS_TABLE_SIZE - 1) && (is_txid_valid == 1)) {
			return -1;
		}

		if (is_txid_valid == 1) {
			continue;
		}

		break;
	}

	return pos;
}

void add_tx_write_to_txid_list(unsigned int txId, unsigned int lpn,
		unsigned int ppn) {
	int inprocessTableNo;
	inprocessTableNo = search_ip_txtab_by_txid(txId);

	if (inprocessTableNo == -1) {
		int insertPos = find_free_pos_inprocesstable();
		if (insertPos != -1) {
			inprocessTableMap->inprocess_table[insertPos * MAX_TX_LIST_SIZE
					+ inprocessTableMap->inprocess_table[insertPos].lpnInsertPos].lpn =
					lpn;
			inprocessTableMap->inprocess_table[insertPos * MAX_TX_LIST_SIZE
					+ inprocessTableMap->inprocess_table[insertPos].lpnInsertPos].ppn =
					ppn;
			inprocessTableMap->txId[insertPos] = txId;
//			inprocessTableMap->inprocess_table[insertPos].lpn[inprocessTableMap->inprocess_table[insertPos].lpnInsertPos] =
//					lpn;
//			inprocessTableMap->inprocess_table[insertPos].ppn[inprocessTableMap->inprocess_table[insertPos].lpnInsertPos] =
//					ppn;
//			inprocessTableMap->inprocess_table[insertPos].txId = txId;
			inprocessTableMap->isTxIdValid[insertPos] = 1;
			inprocessTableMap->inprocess_table[insertPos].lpnInsertPos++;
		} else {
			assert(
					!"[WARNING] There is no available postion in inprocess table [WARNING]");
		}
	} else {
		if (inprocessTableMap->inprocess_table[inprocessTableNo].lpnInsertPos
				== MAX_TX_LIST_SIZE) {
			xil_printf("txId = %d\r\n", txId);
			assert(
					!"[WARNING] Tere is no available postion in txid_list table [WARNING]");
		} else {
			inprocessTableMap->inprocess_table[inprocessTableNo
					* MAX_TX_LIST_SIZE
					+ inprocessTableMap->inprocess_table[inprocessTableNo].lpnInsertPos].lpn =
					lpn;
			inprocessTableMap->inprocess_table[inprocessTableNo
					* MAX_TX_LIST_SIZE
					+ inprocessTableMap->inprocess_table[inprocessTableNo].lpnInsertPos].ppn =
					ppn;
//			inprocessTableMap->inprocess_table[inprocessTableNo].lpn[inprocessTableMap->inprocess_table[inprocessTableNo].lpnInsertPos] =
//					lpn;
//			inprocessTableMap->inprocess_table[inprocessTableNo].ppn[inprocessTableMap->inprocess_table[inprocessTableNo].lpnInsertPos] =
//					ppn;
			inprocessTableMap->inprocess_table[inprocessTableNo].lpnInsertPos++;
		}
	}
}

void add_phys_page_to_log_list(unsigned int txId, unsigned int lpn,
		unsigned int ppn) {
//	int i;

//	xil_printf("lpn = %d\r\n", lpn);
//	for (i = 0; i < MAX_LOG_LIST_SIZE; i++) {
//		if (ppn == logListMap->log_list[i].ppn) {
//			return;
//		}
//	}

	int insertPos = find_free_pos_log_list();
	//xil_printf("insertPos = %d\r\n", insertPos);

	if (insertPos != -1) {
		logListMap->log_list[insertPos].lpn = lpn;
		logListMap->log_list[insertPos].ppn = ppn;
		logListMap->log_list[insertPos].txId = txId;
		logListMap->isTablePosValid[insertPos] = 1;
	} else {
		write_log_to_ssd();
		logListMap->log_list[0].lpn = lpn;
		logListMap->log_list[0].ppn = ppn;
		logListMap->log_list[0].txId = txId;
		logListMap->isTablePosValid[0] = 1;

	}
}

void write_log_to_ssd() {
	int i, lpn, ppn, txId, dieNo, reqSlotTag;
	//int virtualSliceAddr;

	for (i = 0; i < MAX_LOG_LIST_SIZE; i++) {
		if (logListMap->isTablePosValid[i] == 1) {
			lpn = logListMap->log_list[i].lpn;
			ppn = logListMap->log_list[i].ppn;
			txId = logListMap->log_list[i].txId;
//			virtualSliceAddr =
//					logicalSliceMapPtr->logicalSlice[lpn].virtualSliceAddr;
//			xil_printf(
//					"count = %d, i = %d, ppn = %d, lpn = %d, viraddr = %d, logaddr = %d\r\n",
//					write_to_ssd_count, i, ppn, lpn,
//					logicalSliceMapPtr->logicalSlice[lpn].virtualSliceAddr,
//					virtualSliceMapPtr->virtualSlice[ppn].logicalSliceAddr);

			if (ppn != VSA_NONE) {
				dieNo = Vsa2VdieTranslation(ppn);

				//read
				reqSlotTag = GetFromFreeReqQ();

				reqPoolPtr->reqPool[reqSlotTag].txId = txId;
				reqPoolPtr->reqPool[reqSlotTag].reqType = REQ_TYPE_NAND;
				reqPoolPtr->reqPool[reqSlotTag].reqCode = REQ_CODE_READ;
				reqPoolPtr->reqPool[reqSlotTag].logicalSliceAddr = lpn;
				reqPoolPtr->reqPool[reqSlotTag].reqOpt.dataBufFormat =
				REQ_OPT_DATA_BUF_TEMP_ENTRY;
				reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandAddr =
				REQ_OPT_NAND_ADDR_VSA;
				reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandEcc =
				REQ_OPT_NAND_ECC_ON;
				reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandEccWarning =
				REQ_OPT_NAND_ECC_WARNING_OFF;
				reqPoolPtr->reqPool[reqSlotTag].reqOpt.rowAddrDependencyCheck =
				REQ_OPT_ROW_ADDR_DEPENDENCY_CHECK;
				reqPoolPtr->reqPool[reqSlotTag].reqOpt.blockSpace =
				REQ_OPT_BLOCK_SPACE_MAIN;
				reqPoolPtr->reqPool[reqSlotTag].dataBufInfo.entry =
						AllocateTempDataBuf(dieNo);
				UpdateTempDataBufEntryInfoBlockingReq(
						reqPoolPtr->reqPool[reqSlotTag].dataBufInfo.entry,
						reqSlotTag);
				reqPoolPtr->reqPool[reqSlotTag].nandInfo.virtualSliceAddr = ppn;

				//SelectLowLevelReqQ(reqSlotTag);
				SelectLowLevelReqQ(reqSlotTag, write_log_to_ssdFUN);

				//write
				reqSlotTag = GetFromFreeReqQ();

				reqPoolPtr->reqPool[reqSlotTag].txId = txId;
				reqPoolPtr->reqPool[reqSlotTag].reqType = REQ_TYPE_NAND;
				reqPoolPtr->reqPool[reqSlotTag].reqCode = REQ_CODE_WRITE;
				reqPoolPtr->reqPool[reqSlotTag].logicalSliceAddr = lpn;
				reqPoolPtr->reqPool[reqSlotTag].reqOpt.dataBufFormat =
				REQ_OPT_DATA_BUF_TEMP_ENTRY;
				reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandAddr =
				REQ_OPT_NAND_ADDR_VSA;
				reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandEcc =
				REQ_OPT_NAND_ECC_ON;
				reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandEccWarning =
				REQ_OPT_NAND_ECC_WARNING_OFF;
				reqPoolPtr->reqPool[reqSlotTag].reqOpt.rowAddrDependencyCheck =
				REQ_OPT_ROW_ADDR_DEPENDENCY_CHECK;
				reqPoolPtr->reqPool[reqSlotTag].reqOpt.blockSpace =
				REQ_OPT_BLOCK_SPACE_MAIN;
				reqPoolPtr->reqPool[reqSlotTag].dataBufInfo.entry =
						AllocateTempDataBuf(dieNo);
				UpdateTempDataBufEntryInfoBlockingReq(
						reqPoolPtr->reqPool[reqSlotTag].dataBufInfo.entry,
						reqSlotTag);
				reqPoolPtr->reqPool[reqSlotTag].nandInfo.virtualSliceAddr =
						FindFreeVirtualSlice();

				logicalSliceMapPtr->logicalSlice[lpn].virtualSliceAddr =
						reqPoolPtr->reqPool[reqSlotTag].nandInfo.virtualSliceAddr;
				virtualSliceMapPtr->virtualSlice[reqPoolPtr->reqPool[reqSlotTag].nandInfo.virtualSliceAddr].logicalSliceAddr =
						lpn;

				write_to_ssd_count++;
//				if (write_to_ssd_count % 100 == 0) {
//					xil_printf("write_to_ssd_count = %lu", write_to_ssd_count);
//				}
//				unsigned int bufDepCheckReport;
//				bufDepCheckReport = CheckBufDep(reqSlotTag);
//				xil_printf("write_to_ssd_count = %lu, bufDepCheckReport = %s, \r\n", write_to_ssd_count, bufDepCheckReport == BUF_DEPENDENCY_REPORT_PASS? "BUF_DEPENDENCY_REPORT_PASS" : "BUF_DEPENDENCY_REPORT_BLOCKED");

				SelectLowLevelReqQ(reqSlotTag, write_log_to_ssdFUN);

			}

			logListMap->isTablePosValid[i] = 0;

//			if (virtualSliceAddr != VSA_NONE) {
//				if (virtualSliceMapPtr->virtualSlice[virtualSliceAddr].logicalSliceAddr
//						!= lpn) {
//					logListMap->isTablePosValid[i] = 0;
//					continue;
//				}
//
//				dieNo = Vsa2VdieTranslation(virtualSliceAddr);
//				//victimBlockNo = Vsa2VblockTranslation(virtualSliceAddr);
//
//				// unlink
//				//SelectiveGetFromGcVictimList(dieNo, victimBlockNo);
//				//virtualBlockMapPtr->block[dieNo][victimBlockNo].invalidSliceCnt++;
//				//logicalSliceMapPtr->logicalSlice[logicalSliceAddr].virtualSliceAddr = VSA_NONE;
//			}

//			if (lpn != LSA_NONE
//					&& ppn
//							== logicalSliceMapPtr->logicalSlice[lpn].virtualSliceAddr) {
//				xil_printf("write_to_ssd_count = %d, i = %d\r\n", write_to_ssd_count, i);
//				//read
//				reqSlotTag = GetFromFreeReqQ();
//
//				reqPoolPtr->reqPool[reqSlotTag].txId = txId;
//				reqPoolPtr->reqPool[reqSlotTag].reqType = REQ_TYPE_NAND;
//				reqPoolPtr->reqPool[reqSlotTag].reqCode = REQ_CODE_READ;
//				reqPoolPtr->reqPool[reqSlotTag].logicalSliceAddr = lpn;
//				reqPoolPtr->reqPool[reqSlotTag].reqOpt.dataBufFormat =
//				REQ_OPT_DATA_BUF_TEMP_ENTRY;
//				reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandAddr =
//				REQ_OPT_NAND_ADDR_VSA;
//				reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandEcc =
//				REQ_OPT_NAND_ECC_ON;
//				reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandEccWarning =
//				REQ_OPT_NAND_ECC_WARNING_OFF;
//				reqPoolPtr->reqPool[reqSlotTag].reqOpt.rowAddrDependencyCheck =
//				REQ_OPT_ROW_ADDR_DEPENDENCY_CHECK;
//				reqPoolPtr->reqPool[reqSlotTag].reqOpt.blockSpace =
//				REQ_OPT_BLOCK_SPACE_MAIN;
//				reqPoolPtr->reqPool[reqSlotTag].dataBufInfo.entry =
//						AllocateTempDataBuf(dieNo);
//				UpdateTempDataBufEntryInfoBlockingReq(
//						reqPoolPtr->reqPool[reqSlotTag].dataBufInfo.entry,
//						reqSlotTag);
//				reqPoolPtr->reqPool[reqSlotTag].nandInfo.virtualSliceAddr =
//						virtualSliceAddr;
//
//				SelectLowLevelReqQ(reqSlotTag);
//
//				//write
//				reqSlotTag = GetFromFreeReqQ();
//
//				reqPoolPtr->reqPool[reqSlotTag].txId = txId;
//				reqPoolPtr->reqPool[reqSlotTag].reqType = REQ_TYPE_NAND;
//				reqPoolPtr->reqPool[reqSlotTag].reqCode = REQ_CODE_WRITE;
//				reqPoolPtr->reqPool[reqSlotTag].logicalSliceAddr = lpn;
//				reqPoolPtr->reqPool[reqSlotTag].reqOpt.dataBufFormat =
//				REQ_OPT_DATA_BUF_TEMP_ENTRY;
//				reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandAddr =
//				REQ_OPT_NAND_ADDR_VSA;
//				reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandEcc =
//				REQ_OPT_NAND_ECC_ON;
//				reqPoolPtr->reqPool[reqSlotTag].reqOpt.nandEccWarning =
//				REQ_OPT_NAND_ECC_WARNING_OFF;
//				reqPoolPtr->reqPool[reqSlotTag].reqOpt.rowAddrDependencyCheck =
//				REQ_OPT_ROW_ADDR_DEPENDENCY_CHECK;
//				reqPoolPtr->reqPool[reqSlotTag].reqOpt.blockSpace =
//				REQ_OPT_BLOCK_SPACE_MAIN;
//				reqPoolPtr->reqPool[reqSlotTag].dataBufInfo.entry =
//						AllocateTempDataBuf(dieNo);
//				UpdateTempDataBufEntryInfoBlockingReq(
//						reqPoolPtr->reqPool[reqSlotTag].dataBufInfo.entry,
//						reqSlotTag);
//				reqPoolPtr->reqPool[reqSlotTag].nandInfo.virtualSliceAddr =
//						FindFreeVirtualSlice();
//
//				logicalSliceMapPtr->logicalSlice[lpn].virtualSliceAddr =
//						reqPoolPtr->reqPool[reqSlotTag].nandInfo.virtualSliceAddr;
//				virtualSliceMapPtr->virtualSlice[reqPoolPtr->reqPool[reqSlotTag].nandInfo.virtualSliceAddr].logicalSliceAddr =
//						lpn;
//
//				SelectLowLevelReqQ(reqSlotTag);
//			}
//			logListMap->isTablePosValid[i] = 0;
		}
	}
}

void ssd_process_commit_request_wal(unsigned int txId) {
	unsigned int lpn, inprocessTableNo;

	inprocessTableNo = search_ip_txtab_by_txid(txId);
	if (inprocessTableNo == -1) {
		//xil_printf("Warning:can not find txId %d in inprocess txtable\r\n",txId);
		xil_printf("Commit failed txId %d\r\n", txId);

	} else {
		INPROCESS_TABLE_ENTRY inprocessTableEntry =
				inprocessTableMap->inprocess_table[inprocessTableNo];
		if (inprocessTableEntry.lpnInsertPos != 0) {
			int pos;
			for (pos = 0; pos < inprocessTableEntry.lpnInsertPos; pos++) {
				//lpn = inprocessTableMap->inprocess_table[inprocessTableNo].lpn[pos];
				lpn = inprocessTableMap->inprocess_table[inprocessTableNo
						* MAX_TX_LIST_SIZE + pos].lpn;
				add_to_mpi(lpn);
			}
		}

		clear_txid_in_inprocess_table(inprocessTableNo);
		//xil_printf("Commit finished successfully txId %d\r\n", txId);
	}

}

void clear_txid_in_inprocess_table(unsigned int inprocessTableNo) {
	inprocessTableMap->inprocess_table[inprocessTableNo].lpnInsertPos = 0;
	inprocessTableMap->txId[inprocessTableNo] = 0;
	//inprocessTableMap->inprocess_table[inprocessTableNo].txId = 0;
	inprocessTableMap->isTxIdValid[inprocessTableNo] = 0;
}

void ssd_process_abort_request_wal(unsigned int txId) {
	unsigned int lpn, inprocessTableNo;
	inprocessTableNo = search_ip_txtab_by_txid(txId);

	if (inprocessTableNo == -1) {
		//xil_printf("Warning:can not find txId %d in inprocess txtable\r\n",txId);
		xil_printf("Abort failed txId %d\r\n", txId);

	} else {

		//TODO 根据OPENSSD的固件实现来看，若abort掉，应该还需要将对应的lpn加入GcVictimList，否则页面无法回收，下面代码即为此工作

		INPROCESS_TABLE_ENTRY inprocessTableEntry =
				inprocessTableMap->inprocess_table[inprocessTableNo];
		if (inprocessTableEntry.lpnInsertPos != 0) {
			int pos;
			for (pos = 0; pos < inprocessTableEntry.lpnInsertPos; pos++) {
//				lpn = inprocessTableMap->inprocess_table[inprocessTableNo].lpn[pos];
				lpn = inprocessTableMap->inprocess_table[inprocessTableNo
						* MAX_TX_LIST_SIZE + pos].lpn;
				InvalidateOldVsa(lpn);
			}
		}

		clear_txid_in_inprocess_table(inprocessTableNo);
		int i;
		for (i = 0; i < MAX_LOG_LIST_SIZE; i++) {
			if (logListMap->log_list[i].txId == txId) {
				logListMap->isTablePosValid[i] = 0;
			}
//			if (logListMap->isTablePosValid[i] == 1) {
//			}
		}
		//xil_printf("Commit finished successfully txId %d\r\n", txId);
	}

}
