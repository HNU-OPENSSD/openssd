//////////////////////////////////////////////////////////////////////////////////
// nvme_io_cmd.c for Cosmos+ OpenSSD
// Copyright (c) 2016 Hanyang University ENC Lab.
// Contributed by Yong Ho Song <yhsong@enc.hanyang.ac.kr>
//				  Youngjin Jo <yjjo@enc.hanyang.ac.kr>
//				  Sangjin Lee <sjlee@enc.hanyang.ac.kr>
//				  Jaewook Kwak <jwkwak@enc.hanyang.ac.kr>
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
// Engineer: Sangjin Lee <sjlee@enc.hanyang.ac.kr>
//			 Jaewook Kwak <jwkwak@enc.hanyang.ac.kr>
//
// Project Name: Cosmos+ OpenSSD
// Design Name: Cosmos+ Firmware
// Module Name: NVMe IO Command Handler
// File Name: nvme_io_cmd.c
//
// Version: v1.0.1
//
// Description:
//   - handles NVMe IO command
//////////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////////
// Revision History:
//
// * v1.0.1
//   - header file for buffer is changed from "ia_lru_buffer.h" to "lru_buffer.h"
//
// * v1.0.0
//   - First draft
//////////////////////////////////////////////////////////////////////////////////

#include "xil_printf.h"
#include "debug.h"
#include "io_access.h"

#include "nvme.h"
#include "host_lld.h"
#include "nvme_io_cmd.h"

#include "../ftl_config.h"
#include "../request_transform.h"

#include "../wal/wal_ssd.h"

void handle_nvme_io_read(unsigned int cmdSlotTag, NVME_IO_COMMAND *nvmeIOCmd) {
	IO_READ_COMMAND_DW12 readInfo12;
	//IO_READ_COMMAND_DW13 readInfo13;
	//IO_READ_COMMAND_DW15 readInfo15;
	unsigned int startLba[2];
	unsigned int nlb;

	unsigned int txId = 0;

	readInfo12.dword = nvmeIOCmd->dword[12];
	//readInfo13.dword = nvmeIOCmd->dword[13];
	//readInfo15.dword = nvmeIOCmd->dword[15];

	startLba[0] = nvmeIOCmd->dword[10];
	startLba[1] = nvmeIOCmd->dword[11];
	nlb = readInfo12.NLB;

	ASSERT(
			startLba[0] < storageCapacity_L && (startLba[1] < STORAGE_CAPACITY_H || startLba[1] == 0));
	//ASSERT(nlb < MAX_NUM_OF_NLB);
	ASSERT((nvmeIOCmd->PRP1[0] & 0xF) == 0 && (nvmeIOCmd->PRP2[0] & 0xF) == 0); //error
	ASSERT(nvmeIOCmd->PRP1[1] < 0x10 && nvmeIOCmd->PRP2[1] < 0x10);

	ReqTransNvmeToSlice(cmdSlotTag, startLba[0], nlb, IO_NVM_READ, txId);
}

void handle_nvme_io_commit(unsigned int cmdSlotTag, NVME_IO_COMMAND *nvmeIOCmd) {
	unsigned int txId = 0;
	txId = nvmeIOCmd->reserved1[0];
	if (txId != 0) {
		ssd_process_commit_request_wal(txId);
	}

	//xil_printf("io_commit_txId: %u\r\n", txId);
}

void handle_nvme_io_write(unsigned int cmdSlotTag, NVME_IO_COMMAND *nvmeIOCmd) {
	IO_READ_COMMAND_DW12 writeInfo12;
	//IO_READ_COMMAND_DW13 writeInfo13;
	//IO_READ_COMMAND_DW15 writeInfo15;
	unsigned int startLba[2];
	unsigned int nlb;

	// add by yanjie.tan
	unsigned int txId = 0;
	txId = nvmeIOCmd->reserved1[0];

	//unsigned short cid;
	//cid = (unsigned short) nvmeIOCmd->CID;
	//xil_printf("io_write_cid in fun handle_nvme_io_write: %u\r\n", cid);

	//xil_printf("io_write_txId in fun handle_nvme_io_write: %u\r\n", txId);

	writeInfo12.dword = nvmeIOCmd->dword[12];
	//writeInfo13.dword = nvmeIOCmd->dword[13];
	//writeInfo15.dword = nvmeIOCmd->dword[15];

	//if(writeInfo12.FUA == 1)
	//	xil_printf("write FUA\r\n");

	startLba[0] = nvmeIOCmd->dword[10];
	startLba[1] = nvmeIOCmd->dword[11];
	nlb = writeInfo12.NLB;

	ASSERT(
			startLba[0] < storageCapacity_L && (startLba[1] < STORAGE_CAPACITY_H || startLba[1] == 0));
	//ASSERT(nlb < MAX_NUM_OF_NLB);
	ASSERT((nvmeIOCmd->PRP1[0] & 0xF) == 0 && (nvmeIOCmd->PRP2[0] & 0xF) == 0);
	ASSERT(nvmeIOCmd->PRP1[1] < 0x10 && nvmeIOCmd->PRP2[1] < 0x10);

	ReqTransNvmeToSlice(cmdSlotTag, startLba[0], nlb, IO_NVM_WRITE, txId);
}

void handle_nvme_io_cmd(NVME_COMMAND *nvmeCmd) {
	NVME_IO_COMMAND *nvmeIOCmd;
	NVME_COMPLETION nvmeCPL;
	unsigned int opc;

	nvmeIOCmd = (NVME_IO_COMMAND*) nvmeCmd->cmdDword;
	opc = (unsigned int) nvmeIOCmd->OPC;

	switch (opc) {
	case IO_NVM_FLUSH: {
		nvmeCPL.dword[0] = 0;
		nvmeCPL.specific = 0x0;

		unsigned int txId = 0;
		txId = nvmeIOCmd->reserved1[0];
		if (txId != 0) {
			//xil_printf("IO Commit Command\r\n");
			handle_nvme_io_commit(nvmeCmd->cmdSlotTag, nvmeIOCmd);

		} else {
			//xil_printf("IO Flush Command\r\n");
		}

		set_auto_nvme_cpl(nvmeCmd->cmdSlotTag, nvmeCPL.specific,
				nvmeCPL.statusFieldWord);
		break;
	}
	case IO_NVM_WRITE: {
		//xil_printf("IO Write Command\r\n");

		handle_nvme_io_write(nvmeCmd->cmdSlotTag, nvmeIOCmd);
		break;
	}
	case IO_NVM_READ: {
		//xil_printf("IO Read Command\r\n");
		handle_nvme_io_read(nvmeCmd->cmdSlotTag, nvmeIOCmd);
		break;
	}
	case IO_NVM_COMMIT: {
		//xil_printf("IO Commit Command\r\n");
		handle_nvme_io_commit(nvmeCmd->cmdSlotTag, nvmeIOCmd);

		nvmeCPL.dword[0] = 0;
		nvmeCPL.specific = 0x0;
		set_auto_nvme_cpl(nvmeCmd->cmdSlotTag, nvmeCPL.specific,
				nvmeCPL.statusFieldWord);
		break;
	}
	case IO_NVM_ABORT: {
		unsigned int txId;
		//xil_printf("IO Abort Command\r\n");
		txId = nvmeIOCmd->reserved1[0];
		ssd_process_abort_request_wal(txId);

		nvmeCPL.dword[0] = 0;
		nvmeCPL.specific = 0x0;
		set_auto_nvme_cpl(nvmeCmd->cmdSlotTag, nvmeCPL.specific,
				nvmeCPL.statusFieldWord);
		break;
	}
	default: {
		xil_printf("Not Support IO Command OPC: %X\r\n", opc);
		ASSERT(0);
		break;
	}
	}
}

