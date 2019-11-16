//////////////////////////////////////////////////////////////////////////////////
// main.c for Cosmos+ OpenSSD
// Copyright (c) 2016 Hanyang University ENC Lab.
// Contributed by Yong Ho Song <yhsong@enc.hanyang.ac.kr>
//				  Youngjin Jo <yjjo@enc.hanyang.ac.kr>
//				  Sangjin Lee <sjlee@enc.hanyang.ac.kr>
//				  Jaewook Kwak <jwkwak@enc.hanyang.ac.kr>
//				  Kibin Park <kbpark@enc.hanyang.ac.kr>
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
//			 Kibin Park <kbpark@enc.hanyang.ac.kr>
//
// Project Name: Cosmos+ OpenSSD
// Design Name: Cosmos+ Firmware
// Module Name: Main
// File Name: main.c
//
// Version: v1.0.2
//
// Description:
//   - initializes caches, MMU, exception handler
//   - calls nvme_main function
//////////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////////
// Revision History:
//
// * v1.0.2
//   - An address region (0x0020_0000 ~ 0x179F_FFFF) is used to uncached & nonbuffered region
//   - An address region (0x1800_0000 ~ 0x3FFF_FFFF) is used to cached & buffered region
//
// * v1.0.1
//   - Paging table setting is modified for QSPI or SD card boot mode
//     * An address region (0x0010_0000 ~ 0x001F_FFFF) is used to place code, data, heap and stack sections
//     * An address region (0x0010_0000 ~ 0x001F_FFFF) is setted a cached&bufferd region
//
// * v1.0.0
//   - First draft
//////////////////////////////////////////////////////////////////////////////////



#include "xil_cache.h"
#include "xil_exception.h"
#include "xil_mmu.h"
#include "xparameters_ps.h"
#include "xscugic_hw.h"
#include "xscugic.h"
#include "xil_printf.h"
#include "nvme/debug.h"

#include "nvme/nvme.h"
#include "nvme/nvme_main.h"
#include "nvme/host_lld.h"


XScuGic GicInstance;

int main()
{
	unsigned int u;

	XScuGic_Config *IntcConfig; //中断控制器配置 屏蔽所有的中断

	Xil_ICacheDisable();  //d-cache & i-cache 两者为两种不同的cache，不同类型的缓冲区
	Xil_DCacheDisable();  //ICache中存储有微处理器需要的指令 & DCache则是作为一个数据的存储会发生读或者写
	Xil_DisableMMU(); //关闭内存管理单元

	// Paging table set
	#define MB (1024*1024)
	for (u = 0; u < 4096; u++) //设置存储空间的cache缓存功能 设置内存属性
	{
		if (u < 0x2)
			Xil_SetTlbAttributes(u * MB, 0xC1E); // cached & buffered   ？？？？？？
		else if (u < 0x180)
			Xil_SetTlbAttributes(u * MB, 0xC12); // uncached & nonbuffered
		else if (u < 0x400)
			Xil_SetTlbAttributes(u * MB, 0xC1E); // cached & buffered
		else
			Xil_SetTlbAttributes(u * MB, 0xC12); // uncached & nonbuffered
	}
	Xil_EnableMMU();
	Xil_ICacheEnable();
	Xil_DCacheEnable();
	xil_printf("[!] MMU has been enabled.\r\n");
	xil_printf("\r\n Hello COSMOS+ OpenSSD !!! \r\n");


	Xil_ExceptionInit(); //异常处理初始化
	//调用Xil_ExceptionInit来生成一个异常表，并为单独的异常生成一些空白处理程序。
										//是中断的基地址
	IntcConfig = XScuGic_LookupConfig(XPAR_SCUGIC_SINGLE_DEVICE_ID);
								//查找设备配置的程序，带的参数为设备ID,也就是看我们的中断向量是否存在
								//中断向量是中断服务程序的入口地址，在计算机中中断向量的地址存放一条跳转到中断服务程序的跳转指令。
	XScuGic_CfgInitialize(&GicInstance, IntcConfig, IntcConfig->CpuBaseAddress);

	Xil_ExceptionRegisterHandler(XIL_EXCEPTION_ID_INT,
								(Xil_ExceptionHandler)XScuGic_InterruptHandler, //函数指针 这个函数是基本的中断驱动函数。 它必须 连接到中断源， 以便在中断控制器的中断激活时被调用。 它将解决哪些中断是活动的和启用的， 并调用适当的中断处理程序。 它使用中断类型信息来确定何时确认中断。 首先处理最高优先级的中断。 此函数假定中断向量表已预先初始化。
								&GicInstance); //中断控制器实例
	//中断触发之后统一由XScuGic_InterruptHandler先处理，然后在HandlerTable中查找相应的处理函数。这个HandlerTable数组的长度为95个，包含了所有的中断ID。

	//将中断处理函数的地址与参数放入中断向量表HandlerTable中
	XScuGic_Connect(&GicInstance, 61,
					(Xil_ExceptionHandler)dev_irq_handler,//中断请求初始化
					(void *)0);  // 连接中断

	XScuGic_Enable(&GicInstance, 61); //使能参数61对应的中断源

	// Enable interrupts in the Processor. //异常中断请求
	Xil_ExceptionEnableMask(XIL_EXCEPTION_IRQ);
	Xil_ExceptionEnable();

	dev_irq_init();

	nvme_main();

	xil_printf("done\r\n");

	return 0;
}
