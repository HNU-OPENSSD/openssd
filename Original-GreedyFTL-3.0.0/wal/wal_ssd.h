/*
 * wal_ssd.h
 *
 *  Created on: 2019.1.21
 *      Author: lanyjdk
 */

#ifndef SRC_WAL_WAL_SSD_H_
#define SRC_WAL_WAL_SSD_H_
#define NULL ((void *)0)

//add by yanjie.tan 2019.3.25
#define MAX_INPROCESS_TABLE_SIZE 200
#define MAX_LOG_LIST_SIZE 2048

#define MAX_TX_LIST_SIZE 10000
//#define AVAILABLE_COMMIT_REQ_COUNT 200


typedef struct _PHYS_PAGE {
	unsigned int txId;
	unsigned int lpn;
	unsigned int ppn;
//	unsigned int version;
//	unsigned int ref_count;
//	unsigned int erase_flag;
//	unsigned int referer;
//	unsigned int referee;
	//int backpointer;
//	unsigned int commit_flag;
} PHYS_PAGE, *P_PHYS_PAGE;

typedef struct _LOG_LIST_MAP {
	//数组中，第一个数据作为头数据，仅做指向用，不存有效数据
	PHYS_PAGE log_list[MAX_LOG_LIST_SIZE];
	unsigned int isTablePosValid[MAX_LOG_LIST_SIZE];
} LOG_LIST_MAP, *P_LOG_LIST_MAP;

//typedef struct _INPORCESS_TABLE_ENTRY {
//	unsigned int txId;
//	//unsigned int txStatus;
//	unsigned int lpnInsertPos; //lpn and ppn should append in this postion in the lpn and ppn arrays, init is 0; max is MAX_TX_LIST_SIZE;
//	unsigned int lpn[MAX_TX_LIST_SIZE];
//	unsigned int ppn[MAX_TX_LIST_SIZE];
//
//} INPROCESS_TABLE_ENTRY, *P_INPROCESS_TABLE_ENTRY;
//
//typedef struct _INPORCESS_TABLE_MAP {
//	INPROCESS_TABLE_ENTRY inprocess_table[MAX_INPROCESS_TABLE_SIZE];
//	// 标志位，标识inprocess table中的txid是否有效，即是否提交或放弃还是依旧在表中，有效为1，无效为0
//	unsigned int isTxIdValid[MAX_INPROCESS_TABLE_SIZE];
//} INPORCESS_TABLE_MAP, *P_INPORCESS_TABLE_MAP;

typedef struct _INPORCESS_TABLE_ENTRY {
//	unsigned int txId;
//	//unsigned int txStatus;
	unsigned int lpnInsertPos; //lpn and ppn should append in this postion in the lpn and ppn arrays, init is 0; max is MAX_TX_LIST_SIZE;
	unsigned int lpn;
	unsigned int ppn;

} INPROCESS_TABLE_ENTRY, *P_INPROCESS_TABLE_ENTRY;

typedef struct _INPORCESS_TABLE_MAP {
	unsigned int txId[MAX_INPROCESS_TABLE_SIZE];
	INPROCESS_TABLE_ENTRY inprocess_table[MAX_INPROCESS_TABLE_SIZE * MAX_TX_LIST_SIZE];
	// 标志位，标识inprocess table中的txid是否有效，即是否提交或放弃还是依旧在表中，有效为1，无效为0
	unsigned int isTxIdValid[MAX_INPROCESS_TABLE_SIZE];
} INPORCESS_TABLE_MAP, *P_INPORCESS_TABLE_MAP;


#define IN_NVM_MAPPING	0x01
#define NOT_IN_NVM_MAPPING	0x00

extern P_INPORCESS_TABLE_MAP inprocessTableMap;

extern P_LOG_LIST_MAP logListMap;

void InitWALProtocol();
int search_ip_txtab_by_txid(unsigned int txId);
int find_free_pos_inprocesstable();
void add_tx_write_to_txid_list(unsigned int txId, unsigned int lpn,
		unsigned int ppn);
void ssd_process_commit_request_wal(unsigned int txId);
void clear_txid_in_inprocess_table(unsigned int inprocessTableNo);

void add_phys_page_to_log_list(unsigned int txId, unsigned int lpn, unsigned int ppn);

void ssd_process_abort_request_wal (unsigned int txId);
void write_log_to_ssd();
#endif /* SRC_WAL_WAL_SSD_H_ */
