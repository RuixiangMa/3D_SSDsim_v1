/*****************************************************************************************************************************
This is a project on 3D_SSDsim, based on ssdsim under the framework of the completion of structures, the main function:
1.Support for 3D commands, for example:mutli plane\interleave\copyback\program suspend/Resume..etc
2.Multi - level parallel simulation
3.Clear hierarchical interface
4.4-layer structure

FileName�� buffer.c
Author: Zuo Lu 		Version: 1.1	Date:2017/05/12
Description: 
buff layer: only contains data cache (minimum processing size for the sector, that is, unit = 512B), mapping table (page-level);

History:
<contributor>     <time>        <version>       <desc>									<e-mail>
Zuo Lu	        2017/04/06	      1.0		    Creat 3D_SSDsim							617376665@qq.com
Zuo Lu			2017/05/12		  1.1			Support advanced commands:mutli plane   617376665@qq.com
*****************************************************************************************************************************/
#define _CRTDBG_MAP_ALLOC

#include <stdlib.h>
#include <crtdbg.h>
#include <stdio.h>
#include <time.h>
#include <string.h>

#include "ssd.h"
#include "initialize.h"
#include "flash.h"
#include "buffer.h"
#include "interface.h"
#include "ftl.h"
#include "fcl.h"


/**********************************************************************************************************************************************
*Buff strategy:Blocking buff strategy
*1--first check the buffer is full, if dissatisfied, check whether the current request to put down the data, if so, put the current request,
*if not, then block the buffer;
*
*2--If buffer is blocked, select the replacement of the two ends of the page. If the two full page, then issued together to lift the buffer
*block; if a partial page 1 full page or 2 partial page, then issued a pre-read request, waiting for the completion of full page and then issued
*And then release the buffer block.
***********************************************************************************************************************************************/
struct ssd_info *buffer_management(struct ssd_info *ssd)
{
	unsigned int j, lsn, lpn, last_lpn, first_lpn, complete_flag;
	struct request *new_request;

#ifdef DEBUG
	printf("enter buffer_management,  current time:%I64u\n", ssd->current_time);
#endif

	ssd->dram->current_time = ssd->current_time;

	new_request = ssd->request_work;   //ȡ�����Ϲ���ָ�������
	lsn = new_request->lsn;
	lpn = new_request->lsn / ssd->parameter->subpage_page;
	last_lpn = (new_request->lsn + new_request->size - 1) / ssd->parameter->subpage_page;
	first_lpn = new_request->lsn / ssd->parameter->subpage_page;   //����lpn

	new_request->need_distr_flag = (unsigned int*)malloc(sizeof(unsigned int)*((last_lpn - first_lpn + 1)*ssd->parameter->subpage_page / 32 + 1));
	alloc_assert(new_request->need_distr_flag, "new_request->need_distr_flag");
	memset(new_request->need_distr_flag, 0, sizeof(unsigned int)*((last_lpn - first_lpn + 1)*ssd->parameter->subpage_page / 32 + 1));
	complete_flag = 1;

	if (new_request->operation == READ)
	{
		//�ȴ���д���棬�ڴ��������
		handle_write_buffer(ssd, new_request);
		//handle_read_cache(ssd, new_request);
		for (j = 0; j <= (last_lpn - first_lpn + 1)*ssd->parameter->subpage_page / 32; j++)
		{
			if (new_request->need_distr_flag[j] != 0)
			{
				complete_flag = 0;
				break;
			}
		}
		//δ��ȫ���У������󻮷�Ϊ������
		if (complete_flag == 0)
		{
			req_r_distribute(ssd);
		}
	}
	else if (new_request->operation == WRITE)
	{
		//handle_read_cache(ssd, new_request);
		handle_write_buffer(ssd, new_request);
	}
	//��ȫ���У��������ʱ
	if ((complete_flag == 1) && (new_request->subs == NULL))
	{
		new_request->begin_time = ssd->current_time;
		new_request->response_time = ssd->current_time + 1000;
	}

	new_request->cmplt_flag = 1;
	ssd->buffer_full_flag = 0;
	return ssd;
}

struct ssd_info *handle_write_buffer(struct ssd_info *ssd, struct request *req)
{
	unsigned int full_page, lsn, lpn, last_lpn, first_lpn;
	unsigned int need_distb_flag,lsn_flag, mask, index;
	struct buffer_group *buffer_node, key;
	unsigned int state,offset1 = 0, offset2 = 0, flag = 0;


	full_page = ~(0xffffffff << ssd->parameter->subpage_page);
	lsn = req->lsn;
	lpn = req->lsn / ssd->parameter->subpage_page;
	last_lpn = (req->lsn + req->size - 1) / ssd->parameter->subpage_page;
	first_lpn = req->lsn / ssd->parameter->subpage_page;   //����lpn

	if (req->operation == READ)
	{
		//ɨ��д���棬�۲�д�������Ƿ����� 
		while (lpn <= last_lpn)
		{
			/************************************************************************************************
			*need_distb_flag��ʾ�Ƿ���Ҫִ��distribution������1��ʾ��Ҫִ�У�buffer��û�У�0��ʾ����Ҫִ��
			*��1��ʾ��Ҫ�ַ���0��ʾ����Ҫ�ַ�����Ӧ���ʼȫ����Ϊ1
			*************************************************************************************************/
			need_distb_flag = full_page;   //need_distb_flag��־��ǰlpn��ҳ��״̬λ
			key.group = lpn;
			buffer_node = (struct buffer_group*)avlTreeFind(ssd->dram->buffer, (TREE_NODE *)&key);		// buffer node 

			while ((buffer_node != NULL) && (lsn<(lpn + 1)*ssd->parameter->subpage_page) && (lsn <= (req->lsn + req->size - 1)))
			{
				lsn_flag = full_page;
				mask = 1 << (lsn%ssd->parameter->subpage_page);						                    //whileֻ��һ��ִ����һ����ҳ
				if (mask > 0x80000000)
				{
					printf("the subpage number is larger than 32!add some cases");						//ע������ָ������ҳ�����������ܳ���32
					getchar();
				}
				else if ((buffer_node->stored & mask) == mask)
				{
					flag = 1;
					lsn_flag = lsn_flag&(~mask);														//�ѵ�ǰ��buff�����е���ҳ״̬�޸�Ϊ0��������lsn_flag��
				}

				if (flag == 1)
				{
					if (ssd->dram->buffer->buffer_head != buffer_node)
					{
						if (ssd->dram->buffer->buffer_tail == buffer_node)
						{
							buffer_node->LRU_link_pre->LRU_link_next = NULL;
							ssd->dram->buffer->buffer_tail = buffer_node->LRU_link_pre;
						}
						else
						{
							buffer_node->LRU_link_pre->LRU_link_next = buffer_node->LRU_link_next;
							buffer_node->LRU_link_next->LRU_link_pre = buffer_node->LRU_link_pre;
						}
						buffer_node->LRU_link_next = ssd->dram->buffer->buffer_head;
						ssd->dram->buffer->buffer_head->LRU_link_pre = buffer_node;
						buffer_node->LRU_link_pre = NULL;
						ssd->dram->buffer->buffer_head = buffer_node;
					}
					req->complete_lsn_count++;
				}
				need_distb_flag = need_distb_flag&lsn_flag;												//���浱ǰlpn����ҳ״̬��0��ʾ���У�1��ʾδ����

				flag = 0;
				lsn++;
			}
			if (need_distb_flag == 0x00000000)
				ssd->dram->buffer->read_hit++;
			else
				ssd->dram->buffer->read_miss_hit++;


			//���λ�÷ǳ�����Ҫ
			index = (lpn - first_lpn) / (32 / ssd->parameter->subpage_page);							//index��ʾneed_distr_flag�кܶ��int�������־��index����������������
			req->need_distr_flag[index] = req->need_distr_flag[index] | (need_distb_flag << (((lpn - first_lpn) % (32 / ssd->parameter->subpage_page))*ssd->parameter->subpage_page));
			lpn++;
		}
	}
	else if (req->operation == WRITE)
	{
		while (lpn <= last_lpn)
		{
			mask = ~(0xffffffff << (ssd->parameter->subpage_page));
			state = mask;

			if (lpn == first_lpn)
			{
				offset1 = ssd->parameter->subpage_page - ((lpn + 1)*ssd->parameter->subpage_page - req->lsn);
				state = state&(0xffffffff << offset1);
			}
			if (lpn == last_lpn)
			{
				offset2 = ssd->parameter->subpage_page - ((lpn + 1)*ssd->parameter->subpage_page - (req->lsn + req->size));
				state = state&(~(0xffffffff << offset2));
			}

			ssd = insert2buffer(ssd, lpn, state, NULL, req);
			lpn++;
		}
	}
	
	return ssd;
}

struct ssd_info *handle_read_cache(struct ssd_info *ssd, struct request *req)
{
	return ssd;
}


struct ssd_info * getout2buffer(struct ssd_info *ssd, struct sub_request *sub, struct request *req)
{
	unsigned int req_write_counts = 0;
	struct buffer_group *pt;
	unsigned int sub_req_state = 0, sub_req_size = 0, sub_req_lpn = 0, sub_req_type = 0;
	struct sub_request *sub_req = NULL;

	//tail node replacement
	pt = ssd->dram->buffer->buffer_tail;
	sub_req_state = pt->stored;
	sub_req_size = size(pt->stored);
	sub_req_lpn = pt->group;
	sub_req = NULL;
	sub_req = creat_sub_request(ssd, sub_req_lpn, sub_req_size, sub_req_state, req, WRITE);

	//Delete the node
	ssd->dram->buffer->buffer_sector_count = ssd->dram->buffer->buffer_sector_count - sub_req_size;

	if (pt == ssd->dram->buffer->buffer_tail)
	{
		if (ssd->dram->buffer->buffer_head->LRU_link_next == NULL){
			ssd->dram->buffer->buffer_head = NULL;
			ssd->dram->buffer->buffer_tail = NULL;
		}
		else{
			ssd->dram->buffer->buffer_tail = pt->LRU_link_pre;
			ssd->dram->buffer->buffer_tail->LRU_link_next = NULL;
		}
	}
	else
	{
		printf("buffer_tail delete failed\n");
		getchar();
	}
			
	avlTreeDel(ssd->dram->buffer, (TREE_NODE *)pt);
	pt->LRU_link_next = NULL;
	pt->LRU_link_pre = NULL;
	AVL_TREENODE_FREE(ssd->dram->buffer, (TREE_NODE *)pt);
	pt = NULL;

	return ssd;
}


/*******************************************************************************
*The function is to write data to the buffer,Called by buffer_management()
********************************************************************************/
struct ssd_info * insert2buffer(struct ssd_info *ssd, unsigned int lpn, int state, struct sub_request *sub, struct request *req)
{
	int write_back_count, flag = 0;                                                             /*flag��ʾΪд���������ڿռ��Ƿ���ɣ�0��ʾ��Ҫ��һ���ڣ�1��ʾ�Ѿ��ڿ�*/
	unsigned int i, lsn, hit_flag, add_flag, sector_count, active_region_flag = 0, free_sector = 0;
	struct buffer_group *buffer_node = NULL, *pt, *new_node = NULL, key;
	struct sub_request *sub_req = NULL, *update = NULL;


	unsigned int sub_req_state = 0, sub_req_size = 0, sub_req_lpn = 0;

#ifdef DEBUG
	printf("enter insert2buffer,  current time:%I64u, lpn:%d, state:%d,\n", ssd->current_time, lpn, state);
#endif

	sector_count = size(state);                                                                /*��Ҫд��buffer��sector����*/
	key.group = lpn;
	buffer_node = (struct buffer_group*)avlTreeFind(ssd->dram->buffer, (TREE_NODE *)&key);    /*��ƽ���������Ѱ��buffer node*/

	if (buffer_node == NULL)
	{
		free_sector = ssd->dram->buffer->max_buffer_sector - ssd->dram->buffer->buffer_sector_count;
		if (free_sector >= sector_count)
		{
			flag = 1;
			ssd->dram->buffer->write_hit++;
		}
		if (flag == 0)
		{
			write_back_count = sector_count - free_sector;
			ssd->dram->buffer->write_miss_hit++;
			//ssd->dram->buffer->write_miss_hit = ssd->dram->buffer->write_miss_hit + write_back_count;
			while (write_back_count>0)
			{
				sub_req = NULL;
				sub_req_state = ssd->dram->buffer->buffer_tail->stored;
				sub_req_size = size(ssd->dram->buffer->buffer_tail->stored);
				sub_req_lpn = ssd->dram->buffer->buffer_tail->group;
				sub_req = creat_sub_request(ssd, sub_req_lpn, sub_req_size, sub_req_state, req, WRITE);

				/**********************************************************************************
				*req��Ϊ�գ���ʾ���insert2buffer��������buffer_management�е��ã�������request����
				*reqΪ�գ���ʾ�����������process�����д���һ�Զ�ӳ���ϵ�Ķ���ʱ����Ҫ���������
				*�����ݼӵ�buffer�У�����ܲ���ʵʱ��д�ز�������Ҫ�����ʵʱ��д�ز��������������
				*������������������
				***********************************************************************************/
				if (req != NULL)
				{
				}
				else
				{
					sub_req->next_subs = sub->next_subs;
					sub->next_subs = sub_req;
				}

				/*********************************************************************
				*д������뵽��ƽ�����������ʱ��Ҫ�޸�dram��buffer_sector_count��
				*ά��ƽ�����������avlTreeDel()��AVL_TREENODE_FREE()������ά��LRU�㷨��
				**********************************************************************/
				ssd->dram->buffer->buffer_sector_count = ssd->dram->buffer->buffer_sector_count - sub_req->size;
				pt = ssd->dram->buffer->buffer_tail;
				avlTreeDel(ssd->dram->buffer, (TREE_NODE *)pt);
				if (ssd->dram->buffer->buffer_head->LRU_link_next == NULL){
					ssd->dram->buffer->buffer_head = NULL;
					ssd->dram->buffer->buffer_tail = NULL;
				}
				else{
					ssd->dram->buffer->buffer_tail = ssd->dram->buffer->buffer_tail->LRU_link_pre;
					ssd->dram->buffer->buffer_tail->LRU_link_next = NULL;
				}
				pt->LRU_link_next = NULL;
				pt->LRU_link_pre = NULL;
				AVL_TREENODE_FREE(ssd->dram->buffer, (TREE_NODE *)pt);
				pt = NULL;

				write_back_count = write_back_count - sub_req->size;                            /*��Ϊ������ʵʱд�ز�������Ҫ������д�ز�����������*/
			}
		}

		/******************************************************************************
		*����һ��buffer node���������ҳ������ֱ�ֵ��������Ա����ӵ����׺Ͷ�������
		*******************************************************************************/
		new_node = NULL;
		new_node = (struct buffer_group *)malloc(sizeof(struct buffer_group));
		alloc_assert(new_node, "buffer_group_node");
		memset(new_node, 0, sizeof(struct buffer_group));

		new_node->group = lpn;
		new_node->stored = state;
		new_node->dirty_clean = state;
		new_node->LRU_link_pre = NULL;
		new_node->LRU_link_next = ssd->dram->buffer->buffer_head;
		if (ssd->dram->buffer->buffer_head != NULL){
			ssd->dram->buffer->buffer_head->LRU_link_pre = new_node;
		}
		else{
			ssd->dram->buffer->buffer_tail = new_node;
		}
		ssd->dram->buffer->buffer_head = new_node;
		new_node->LRU_link_pre = NULL;
		avlTreeAdd(ssd->dram->buffer, (TREE_NODE *)new_node);
		ssd->dram->buffer->buffer_sector_count += sector_count;
	}
	/****************************************************************************************
	*��buffer�����е����
	*��Ȼ�����ˣ��������е�ֻ��lpn���п���������д����ֻ����Ҫдlpn��һpage��ĳ����sub_page
	*��ʱ����Ҫ��һ�����ж�
	*****************************************************************************************/
	else
	{
		ssd->dram->buffer->write_hit++;
		for (i = 0; i<ssd->parameter->subpage_page; i++)
		{
			/*************************************************************
			*�ж�state��iλ�ǲ���1
			*�����жϵ�i��sector�Ƿ����buffer�У�1��ʾ���ڣ�0��ʾ�����ڡ�
			**************************************************************/
			if ((state >> i) % 2 != 0)
			{
				lsn = lpn*ssd->parameter->subpage_page + i;
				hit_flag = 0;
				hit_flag = (buffer_node->stored)&(0x00000001 << i);

				if (hit_flag != 0)				                                          /*�����ˣ���Ҫ���ýڵ��Ƶ�buffer�Ķ��ף����ҽ����е�lsn���б��*/
				{
					active_region_flag = 1;                                             /*������¼�����buffer node�е�lsn�Ƿ����У����ں������ֵ���ж�*/

					if (req != NULL)
					{
						if (ssd->dram->buffer->buffer_head != buffer_node)
						{
							if (ssd->dram->buffer->buffer_tail == buffer_node)
							{
								ssd->dram->buffer->buffer_tail = buffer_node->LRU_link_pre;
								buffer_node->LRU_link_pre->LRU_link_next = NULL;
							}
							else if (buffer_node != ssd->dram->buffer->buffer_head)
							{
								buffer_node->LRU_link_pre->LRU_link_next = buffer_node->LRU_link_next;
								buffer_node->LRU_link_next->LRU_link_pre = buffer_node->LRU_link_pre;
							}
							buffer_node->LRU_link_next = ssd->dram->buffer->buffer_head;
							ssd->dram->buffer->buffer_head->LRU_link_pre = buffer_node;
							buffer_node->LRU_link_pre = NULL;
							ssd->dram->buffer->buffer_head = buffer_node;
						}
						//ssd->dram->buffer->write_hit++;
						req->complete_lsn_count++;                                        /*�ؼ� ����buffer������ʱ ����req->complete_lsn_count++��ʾ��buffer��д�����ݡ�*/
					}
					else
					{
					}
				}
				else
				{
					if (ssd->dram->buffer->buffer_sector_count >= ssd->dram->buffer->max_buffer_sector)
					{
						if (buffer_node == ssd->dram->buffer->buffer_tail)                  /*������еĽڵ���buffer�����һ���ڵ㣬������������ڵ�*/
						{
							pt = ssd->dram->buffer->buffer_tail->LRU_link_pre;
							ssd->dram->buffer->buffer_tail->LRU_link_pre = pt->LRU_link_pre;
							ssd->dram->buffer->buffer_tail->LRU_link_pre->LRU_link_next = ssd->dram->buffer->buffer_tail;
							ssd->dram->buffer->buffer_tail->LRU_link_next = pt;
							pt->LRU_link_next = NULL;
							pt->LRU_link_pre = ssd->dram->buffer->buffer_tail;
							ssd->dram->buffer->buffer_tail = pt;

						}
						sub_req = NULL;
						sub_req_state = ssd->dram->buffer->buffer_tail->stored;
						sub_req_size = size(ssd->dram->buffer->buffer_tail->stored);
						sub_req_lpn = ssd->dram->buffer->buffer_tail->group;
						sub_req = creat_sub_request(ssd, sub_req_lpn, sub_req_size, sub_req_state, req, WRITE);

						if (req != NULL)
						{

						}
						else if (req == NULL)
						{
							sub_req->next_subs = sub->next_subs;
							sub->next_subs = sub_req;
						}

						ssd->dram->buffer->buffer_sector_count = ssd->dram->buffer->buffer_sector_count - sub_req->size;
						pt = ssd->dram->buffer->buffer_tail;
						avlTreeDel(ssd->dram->buffer, (TREE_NODE *)pt);

						/************************************************************************/
						/* ��:  ������������buffer�Ľڵ㲻Ӧ����ɾ����						*/
						/*			��ȵ�д����֮�����ɾ��									*/
						/************************************************************************/
						if (ssd->dram->buffer->buffer_head->LRU_link_next == NULL)
						{
							ssd->dram->buffer->buffer_head = NULL;
							ssd->dram->buffer->buffer_tail = NULL;
						}
						else{
							ssd->dram->buffer->buffer_tail = ssd->dram->buffer->buffer_tail->LRU_link_pre;
							ssd->dram->buffer->buffer_tail->LRU_link_next = NULL;
						}
						pt->LRU_link_next = NULL;
						pt->LRU_link_pre = NULL;
						AVL_TREENODE_FREE(ssd->dram->buffer, (TREE_NODE *)pt);
						pt = NULL;
					}

					/*�ڶ���:���µ�lsn�ӵ�������buffer�ڵ���*/
					add_flag = 0x00000001 << (lsn%ssd->parameter->subpage_page);

					if (ssd->dram->buffer->buffer_head != buffer_node)                      /*�����buffer�ڵ㲻��buffer�Ķ��ף���Ҫ������ڵ��ᵽ����*/
					{
						if (ssd->dram->buffer->buffer_tail == buffer_node)
						{
							buffer_node->LRU_link_pre->LRU_link_next = NULL;
							ssd->dram->buffer->buffer_tail = buffer_node->LRU_link_pre;
						}
						else
						{
							buffer_node->LRU_link_pre->LRU_link_next = buffer_node->LRU_link_next;
							buffer_node->LRU_link_next->LRU_link_pre = buffer_node->LRU_link_pre;
						}
						buffer_node->LRU_link_next = ssd->dram->buffer->buffer_head;
						ssd->dram->buffer->buffer_head->LRU_link_pre = buffer_node;
						buffer_node->LRU_link_pre = NULL;
						ssd->dram->buffer->buffer_head = buffer_node;
					}
					buffer_node->stored = buffer_node->stored | add_flag;
					buffer_node->dirty_clean = buffer_node->dirty_clean | add_flag;
					ssd->dram->buffer->buffer_sector_count++;
				}

			}
		}
	}

	return ssd;
}

/**********************************************************************************
*Read requests allocate sub-request functions, decompose each request into sub-requests 
*based on the request queue and buffer hit check, hang the sub request queue on the 
*channel, and the different channel has its own sub request queue
**********************************************************************************/
struct ssd_info *req_r_distribute(struct ssd_info *ssd)
{
	unsigned int start, end, first_lsn, last_lsn, lpn, flag = 0, flag_attached = 0, full_page;
	unsigned int j, k, sub_size;
	int i = 0;
	struct request *req;
	struct sub_request *sub;
	int* complt;

#ifdef DEBUG
	printf("enter distribute,  current time:%I64u\n", ssd->current_time);
#endif
	full_page = ~(0xffffffff << ssd->parameter->subpage_page);

	req = ssd->request_work;
	if (req->response_time != 0){
		return ssd;
	}
	if (req->operation == WRITE)
	{
		return ssd;
	}

	if (req != NULL)
	{
		if (req->distri_flag == 0)
		{
			if (req->complete_lsn_count != ssd->request_work->size)
			{
				first_lsn = req->lsn;
				last_lsn = first_lsn + req->size;
				complt = req->need_distr_flag;    //Sub pages that have been completed in buff

				//Start, end Indicates the start and end of the request in units of sub pages
				start = first_lsn - first_lsn % ssd->parameter->subpage_page;
				end = (last_lsn / ssd->parameter->subpage_page + 1) * ssd->parameter->subpage_page;

				//how many sub pages are requested, that is, how many int types are marked
				i = (end - start) / 32;


				while (i >= 0)
				{
					/*************************************************************************************
					*Each bit of a 32-bit integer data represents a subpage, 32 / ssd-> parameter-> subpage_page 
					*indicates how many pages are, and the status of each page is stored in req-> need_distr_flag, 
					*that is, Complt, by comparing complt each and full_page, you can know whether this page is 
					*processed. Create a subquery with the creat_sub_request function if no processing is done.
					*************************************************************************************/
					for (j = 0; j<32 / ssd->parameter->subpage_page; j++)
					{
						k = (complt[((end - start) / 32 - i)] >> (ssd->parameter->subpage_page*j)) & full_page;   //buff inserted in the bit state of the opposite, the corresponding lpn bit state out
						if (k != 0)    //Note that the current lpn did not completely hit in the buffe, the need for a read operation
						{
							lpn = start / ssd->parameter->subpage_page + ((end - start) / 32 - i) * 32 / ssd->parameter->subpage_page + j;
							sub_size = transfer_size(ssd, k, lpn, req);  //Sub_size is the number of pages that need to go to the flash
							if (sub_size == 0)
							{
								continue;
							}
							else
							{
								printf("normal read\n");
								sub = creat_sub_request(ssd, lpn, sub_size, 0, req, req->operation);
							}
						}
					}
					i = i - 1;
				}
			}
			else
			{
				req->begin_time = ssd->current_time;
				req->response_time = ssd->current_time + 1000;
			}

		}
	}
	req->cmplt_flag = 1; 
	return ssd;
}


/*********************************************************************************************
*The no_buffer_distribute () function is processed when ssd has no dram��
*This is no need to read and write requests in the buffer inside the search, directly use the 
*creat_sub_request () function to create sub-request, and then deal with.
*********************************************************************************************/
struct ssd_info *no_buffer_distribute(struct ssd_info *ssd)
{
	unsigned int lsn, lpn, last_lpn, first_lpn, complete_flag = 0, state;
	unsigned int flag = 0, flag1 = 1, active_region_flag = 0;           //to indicate the lsn is hitted or not
	struct request *req = NULL;
	struct sub_request *sub = NULL, *sub_r = NULL, *update = NULL;
	struct local *loc = NULL;
	struct channel_info *p_ch = NULL;


	unsigned int mask = 0;
	unsigned int offset1 = 0, offset2 = 0;
	unsigned int sub_size = 0;
	unsigned int sub_state = 0;

	ssd->dram->current_time = ssd->current_time;
	//req = ssd->request_tail;
	req = ssd->request_work;
	lsn = req->lsn;
	lpn = req->lsn / ssd->parameter->subpage_page;
	last_lpn = (req->lsn + req->size - 1) / ssd->parameter->subpage_page;
	first_lpn = req->lsn / ssd->parameter->subpage_page;

	if (req->operation == READ)
	{
		while (lpn <= last_lpn)
		{
			sub_state = (ssd->dram->map->map_entry[lpn].state & 0x7fffffff);
			sub_size = size(sub_state);
			sub = creat_sub_request(ssd, lpn, sub_size, sub_state, req, req->operation);
			lpn++;
		}
	}
	else if (req->operation == WRITE)
	{
		while (lpn <= last_lpn)
		{
			mask = ~(0xffffffff << (ssd->parameter->subpage_page));
			state = mask;
			if (lpn == first_lpn)
			{
				offset1 = ssd->parameter->subpage_page - ((lpn + 1)*ssd->parameter->subpage_page - req->lsn);
				state = state&(0xffffffff << offset1);
			}
			if (lpn == last_lpn)
			{
				offset2 = ssd->parameter->subpage_page - ((lpn + 1)*ssd->parameter->subpage_page - (req->lsn + req->size));
				state = state&(~(0xffffffff << offset2));
			}
			sub_size = size(state);

			sub = creat_sub_request(ssd, lpn, sub_size, state, req, req->operation);
			lpn++;
		}
	}

	return ssd;
}

/****************************************************************************************************************
*The function of the transfer_size () is to calculate the size of the sub request that needs to be processed
*The first special case of first_lpn and last_lpn is handled in the function, since these two cases are likely 
*not to deal with a whole page but to deal with a part of the page, since lsn may not be the first subpage of a page.
*******************************************************************************************************************/
unsigned int transfer_size(struct ssd_info *ssd, int need_distribute, unsigned int lpn, struct request *req)
{
	unsigned int first_lpn, last_lpn, state, trans_size;
	unsigned int mask = 0, offset1 = 0, offset2 = 0;

	first_lpn = req->lsn / ssd->parameter->subpage_page;
	last_lpn = (req->lsn + req->size - 1) / ssd->parameter->subpage_page;

	mask = ~(0xffffffff << (ssd->parameter->subpage_page));
	state = mask;
	if (lpn == first_lpn)
	{
		offset1 = ssd->parameter->subpage_page - ((lpn + 1)*ssd->parameter->subpage_page - req->lsn);
		state = state&(0xffffffff << offset1);
	}
	if (lpn == last_lpn)
	{
		offset2 = ssd->parameter->subpage_page - ((lpn + 1)*ssd->parameter->subpage_page - (req->lsn + req->size));
		state = state&(~(0xffffffff << offset2));
	}

	trans_size = size(state&need_distribute);

	return trans_size;
}



/***********************************************************************************
*According to the status of each page to calculate the number of each need to deal 
*with the number of sub-pages, that is, a sub-request to deal with the number of pages
************************************************************************************/
unsigned int size(unsigned int stored)
{
	unsigned int i, total = 0, mask = 0x80000000;

#ifdef DEBUG
	printf("enter size\n");
#endif
	for (i = 1; i <= 32; i++)
	{
		if (stored & mask) total++;     //The total count indicates that the flag is 0 in the subpage
		stored <<= 1;
	}
#ifdef DEBUG
	printf("leave size\n");
#endif
	return total;
}


/**************************************************************
this function is to create sub_request based on lpn, size, state
****************************************************************/
struct sub_request * creat_sub_request(struct ssd_info * ssd, unsigned int lpn, int size, unsigned int state, struct request * req, unsigned int operation)
{
	struct sub_request* sub = NULL, *sub_r = NULL;
	struct channel_info * p_ch = NULL;
	struct local * loc = NULL;
	unsigned int flag = 0;

	sub = (struct sub_request*)malloc(sizeof(struct sub_request));                        /*����һ��������Ľṹ*/
	alloc_assert(sub, "sub_request");
	memset(sub, 0, sizeof(struct sub_request));


	if (sub == NULL)
	{
		return NULL;
	}
	sub->location = NULL;
	sub->next_node = NULL;
	sub->next_subs = NULL;
	sub->update = NULL;

	if (req != NULL)
	{
		sub->next_subs = req->subs;
		req->subs = sub;
		sub->total_request = req;
	}

	/*************************************************************************************
	*�ڶ�����������£���һ��ǳ���Ҫ����ҪԤ���ж϶�������������Ƿ����������������ͬ�ģ�
	*�еĻ�����������Ͳ�����ִ���ˣ����µ�������ֱ�Ӹ�Ϊ���
	**************************************************************************************/
	if (operation == READ)
	{
		loc = find_location(ssd, ssd->dram->map->map_entry[lpn].pn);
		sub->location = loc;
		sub->begin_time = ssd->current_time;
		sub->current_state = SR_WAIT;
		sub->current_time = 0x7fffffffffffffff;
		sub->next_state = SR_R_C_A_TRANSFER;
		sub->next_state_predict_time = 0x7fffffffffffffff;
		sub->lpn = lpn;
		sub->size = size;                                                               /*��Ҫ�������������������С*/
		sub->update_read_flag = 0;

		p_ch = &ssd->channel_head[loc->channel];
		sub->ppn = ssd->dram->map->map_entry[lpn].pn;
		sub->operation = READ;
		sub->state = (ssd->dram->map->map_entry[lpn].state & 0x7fffffff);
		sub_r = p_ch->subs_r_head;                                                      /*һ�¼��а���flag�����жϸö�������������Ƿ����������������ͬ�ģ��еĻ������µ�������ֱ�Ӹ�Ϊ���*/
		flag = 0;
		while (sub_r != NULL)
		{
			if (sub_r->ppn == sub->ppn)
			{
				flag = 1;
				break;
			}
			sub_r = sub_r->next_node;
		}
		if (flag == 0)
		{
			if (p_ch->subs_r_tail != NULL)
			{
				p_ch->subs_r_tail->next_node = sub;
				p_ch->subs_r_tail = sub;
			}
			else
			{
				p_ch->subs_r_head = sub;
				p_ch->subs_r_tail = sub;
			}
		}
		else
		{
			sub->current_state = SR_R_DATA_TRANSFER;
			sub->current_time = ssd->current_time;
			sub->next_state = SR_COMPLETE;
			sub->next_state_predict_time = ssd->current_time + 1000;
			sub->complete_time = ssd->current_time + 1000;
		}
	}
	/*************************************************************************************
	*д���������£�����Ҫ���õ�����allocate_location(ssd ,sub)������̬����Ͷ�̬������
	**************************************************************************************/
	else if (operation == WRITE)
	{
		sub->ppn = 0;
		sub->operation = WRITE;
		sub->location = (struct local *)malloc(sizeof(struct local));
		alloc_assert(sub->location, "sub->location");
		memset(sub->location, 0, sizeof(struct local));

		sub->current_state = SR_WAIT;
		sub->current_time = ssd->current_time;
		sub->lpn = lpn;
		sub->size = size;
		sub->state = state;
		sub->begin_time = ssd->current_time;

		if (allocate_location(ssd, sub) == ERROR)
		{
			free(sub->location);
			sub->location = NULL;
			free(sub);
			sub = NULL;
			printf("allocate_location error \n");
			getchar();
			return NULL;
		}

	}
	else
	{
		free(sub->location);
		sub->location = NULL;
		free(sub);
		sub = NULL;
		printf("\nERROR ! Unexpected command.\n");
		return NULL;
	}

	return sub;
}

/***************************************
*Write request dynamic allocation mount
***************************************/
Status allocate_location(struct ssd_info * ssd, struct sub_request *sub_req)
{
	struct sub_request * update = NULL;
	unsigned int channel_num = 0, chip_num = 0, die_num = 0, plane_num = 0;
	struct local *location = NULL;

	channel_num = ssd->parameter->channel_number;
	chip_num = ssd->parameter->chip_channel[0];
	die_num = ssd->parameter->die_chip;
	plane_num = ssd->parameter->plane_die;


	if (ssd->parameter->allocation_scheme == 0)                                          /*��̬��������*/
	{
		/******************************************************************
		* �ڶ�̬�����У���Ϊҳ�ĸ��²���ʹ�ò���copyback������
		*��Ҫ����һ�������󣬲���ֻ�������������ɺ���ܽ������ҳ��д����
		*******************************************************************/
		if (ssd->dram->map->map_entry[sub_req->lpn].state != 0)
		{
			if ((sub_req->state&ssd->dram->map->map_entry[sub_req->lpn].state) != ssd->dram->map->map_entry[sub_req->lpn].state)
			{
				//ssd->read_count++;
				ssd->update_read_count++;
				ssd->update_write_count++;

				update = (struct sub_request *)malloc(sizeof(struct sub_request));
				alloc_assert(update, "update");
				memset(update, 0, sizeof(struct sub_request));

				if (update == NULL)
				{
					return ERROR;
				}
				update->location = NULL;
				update->next_node = NULL;
				update->next_subs = NULL;
				update->update = NULL;
				location = find_location(ssd, ssd->dram->map->map_entry[sub_req->lpn].pn);
				update->location = location;
				update->begin_time = ssd->current_time;
				update->current_state = SR_WAIT;
				update->current_time = 0x7fffffffffffffff;
				update->next_state = SR_R_C_A_TRANSFER;
				update->next_state_predict_time = 0x7fffffffffffffff;
				update->lpn = sub_req->lpn;
				update->state = ((ssd->dram->map->map_entry[sub_req->lpn].state^sub_req->state) & 0x7fffffff);
				update->size = size(update->state);
				update->ppn = ssd->dram->map->map_entry[sub_req->lpn].pn;
				update->operation = READ;
				update->update_read_flag = 1;

				if (ssd->channel_head[location->channel].subs_r_tail != NULL)            /*�����µĶ����󣬲��ҹҵ�channel��subs_r_tail����β*/
				{
					ssd->channel_head[location->channel].subs_r_tail->next_node = update;
					ssd->channel_head[location->channel].subs_r_tail = update;
				}
				else
				{
					ssd->channel_head[location->channel].subs_r_tail = update;
					ssd->channel_head[location->channel].subs_r_head = update;
				}
			}
		}

		if (ssd->parameter->dynamic_allocation == 0)
		{
			sub_req->location->channel = -1;
			sub_req->location->chip = -1;
			sub_req->location->die = -1;
			sub_req->location->plane = -1;
			sub_req->location->block = -1;
			sub_req->location->page = -1;        //������ssdinfo�ϣ���Ϊȫ��̬���䣬��֪��������ص��ĸ�channel����

			if (ssd->subs_w_tail != NULL)
			{
				ssd->subs_w_tail->next_node = sub_req;
				ssd->subs_w_tail = sub_req;
			}
			else
			{
				ssd->subs_w_tail = sub_req;
				ssd->subs_w_head = sub_req;
			}

			if (update != NULL)
			{
				sub_req->update_read_flag = 1;
				sub_req->update = update;
				sub_req->state = (sub_req->state | update->state);
				sub_req->size = size(sub_req->state);
			}
			else
			{
				sub_req->update_read_flag = 0;
			}
		}
	}

	return SUCCESS;
}