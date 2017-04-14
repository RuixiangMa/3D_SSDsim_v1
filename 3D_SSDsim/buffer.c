/*****************************************************************************************************************************
This is a project on 3D_SSDsim, based on ssdsim under the framework of the completion of structures, the main function:
1.Support for 3D commands, for example:mutli plane\interleave\copyback\program suspend/Resume..etc
2.Multi - level parallel simulation
3.Clear hierarchical interface
4.4-layer structure

FileName�� ssd.c
Author: Zuo Lu 		Version: 1.0	Date:2017/04/06
Description: 
buff layer: only contains data cache (minimum processing size for the sector, that is, unit = 512B), mapping table (page-level);

History:
<contributor>     <time>        <version>       <desc>                   <e-mail>
Zuo Lu	        2017/04/06	      1.0		    Creat 3D_SSDsim       617376665@qq.com

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
*����buffer�Ǹ�дbuffer������Ϊд�������ģ���Ϊ��flash��ʱ��tRΪ20us��дflash��ʱ��tprogΪ200us������Ϊд������ܽ�ʡʱ��
*  �����������������buffer����buffer������ռ��channel��I/O���ߣ�û������buffer����flash����ռ��channel��I/O���ߣ����ǲ���buffer��
*  д����������request�ֳ�sub_request����������Ƕ�̬���䣬sub_request�ҵ�ssd->sub_request�ϣ���Ϊ��֪��Ҫ�ȹҵ��ĸ�channel��sub_request��
*          ����Ǿ�̬������sub_request�ҵ�channel��sub_request����,ͬʱ���ܶ�̬���仹�Ǿ�̬����sub_request��Ҫ�ҵ�request��sub_request����
*		   ��Ϊÿ������һ��request����Ҫ��traceoutput�ļ�������������request����Ϣ��������һ��sub_request,�ͽ����channel��sub_request��
*		   ��ssd��sub_request����ժ����������traceoutput�ļ����һ���������request��sub_request����
*		   sub_request����buffer����buffer����д�����ˣ����ҽ���sub_page�ᵽbuffer��ͷ(LRU)����û��������buffer�������Ƚ�buffer��β��sub_request
*		   д��flash(������һ��sub_requestд���󣬹ҵ��������request��sub_request���ϣ�ͬʱ�Ӷ�̬���仹�Ǿ�̬����ҵ�channel��ssd��
*		   sub_request����),�ڽ�Ҫд��sub_pageд��buffer��ͷ
***********************************************************************************************************************************************/
struct ssd_info *buffer_management(struct ssd_info *ssd)
{
	unsigned int j, lsn, lpn, last_lpn, first_lpn, index, complete_flag = 0, state, full_page;
	unsigned int flag = 0, need_distb_flag, lsn_flag, flag1 = 1, active_region_flag = 0;
	struct request *new_request;
	struct buffer_group *buffer_node, key;
	unsigned int mask = 0, offset1 = 0, offset2 = 0;

#ifdef DEBUG
	printf("enter buffer_management,  current time:%I64u\n", ssd->current_time);
#endif
	ssd->dram->current_time = ssd->current_time;
	full_page = ~(0xffffffff << ssd->parameter->subpage_page);

	new_request = ssd->request_tail;
	lsn = new_request->lsn;
	lpn = new_request->lsn / ssd->parameter->subpage_page;
	last_lpn = (new_request->lsn + new_request->size - 1) / ssd->parameter->subpage_page;
	first_lpn = new_request->lsn / ssd->parameter->subpage_page;   //����lpn

	//int��32λ��Ҳ����˵need_distr_flag ��һ����bitΪ��λ��״̬λ����
	new_request->need_distr_flag = (unsigned int*)malloc(sizeof(unsigned int)*((last_lpn - first_lpn + 1)*ssd->parameter->subpage_page / 32 + 1));
	alloc_assert(new_request->need_distr_flag, "new_request->need_distr_flag");
	memset(new_request->need_distr_flag, 0, sizeof(unsigned int)*((last_lpn - first_lpn + 1)*ssd->parameter->subpage_page / 32 + 1));

	if (new_request->operation == READ)
	{
		while (lpn <= last_lpn)
		{
			/************************************************************************************************
			*need_distb_flag��ʾ�Ƿ���Ҫִ��distribution������1��ʾ��Ҫִ�У�buffer��û�У�0��ʾ����Ҫִ��
			*��1��ʾ��Ҫ�ַ���0��ʾ����Ҫ�ַ�����Ӧ���ʼȫ����Ϊ1
			*************************************************************************************************/
			need_distb_flag = full_page;   //need_distb_flag��־��ǰlpn��ҳ��״̬λ
			key.group = lpn;
			buffer_node = (struct buffer_group*)avlTreeFind(ssd->dram->buffer, (TREE_NODE *)&key);		// buffer node 

			while ((buffer_node != NULL) && (lsn<(lpn + 1)*ssd->parameter->subpage_page) && (lsn <= (new_request->lsn + new_request->size - 1)))
			{
				lsn_flag = full_page;
				mask = 1 << (lsn%ssd->parameter->subpage_page);         //whileֻ��һ��ִ����һ����ҳ
				//if (mask>31)
				if (mask > 0x80000000)
				{
					printf("the subpage number is larger than 32!add some cases");   //ע������ָ������ҳ�����������ܳ���32
					getchar();
				}
				else if ((buffer_node->stored & mask) == mask)
				{
					flag = 1;
					lsn_flag = lsn_flag&(~mask);   //�ѵ�ǰ��buff�����е���ҳ״̬�޸�Ϊ0��������lsn_flag��
				}

				if (flag == 1)
				{	//�����buffer�ڵ㲻��buffer�Ķ��ף���Ҫ������ڵ��ᵽ���ף�ʵ����LRU�㷨�������һ��˫����С�		       		
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
					ssd->dram->buffer->read_hit++;
					new_request->complete_lsn_count++;
				}
				else if (flag == 0)
				{
					ssd->dram->buffer->read_miss_hit++;
				}

				need_distb_flag = need_distb_flag&lsn_flag;   //���浱ǰlpn����ҳ״̬��0��ʾ���У�1��ʾδ����

				flag = 0;
				lsn++;
			}

			//���λ�÷ǳ�����Ҫ
			index = (lpn - first_lpn) / (32 / ssd->parameter->subpage_page);  //index��ʾneed_distr_flag�кܶ��int�������־��index����������������
			new_request->need_distr_flag[index] = new_request->need_distr_flag[index] | (need_distb_flag << (((lpn - first_lpn) % (32 / ssd->parameter->subpage_page))*ssd->parameter->subpage_page));
			lpn++;

		}
	}
	else if (new_request->operation == WRITE)
	{
		while (lpn <= last_lpn)
		{
			need_distb_flag = full_page;
			mask = ~(0xffffffff << (ssd->parameter->subpage_page));
			state = mask;

			if (lpn == first_lpn)
			{
				offset1 = ssd->parameter->subpage_page - ((lpn + 1)*ssd->parameter->subpage_page - new_request->lsn);
				state = state&(0xffffffff << offset1);
			}
			if (lpn == last_lpn)
			{
				offset2 = ssd->parameter->subpage_page - ((lpn + 1)*ssd->parameter->subpage_page - (new_request->lsn + new_request->size));
				state = state&(~(0xffffffff << offset2));
			}

			ssd = insert2buffer(ssd, lpn, state, NULL, new_request);
			lpn++;
		}
	}
	complete_flag = 1;
	for (j = 0; j <= (last_lpn - first_lpn + 1)*ssd->parameter->subpage_page / 32; j++)
	{
		if (new_request->need_distr_flag[j] != 0)
		{
			complete_flag = 0;
		}
	}

	/*************************************************************
	*��������Ѿ���ȫ����buffer���񣬸�������Ա�ֱ����Ӧ��������
	*�������dram�ķ���ʱ��Ϊ1000ns
	**************************************************************/
	if ((complete_flag == 1) && (new_request->subs == NULL))
	{
		new_request->begin_time = ssd->current_time;
		new_request->response_time = ssd->current_time + 1000;
	}

	return ssd;
}


/*******************************************************************************
*insert2buffer���������ר��Ϊд�������������������buffer_management�б����á�
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

	/************************************************************************************************
	*û�����С�
	*��һ���������lpn�ж�����ҳ��Ҫд��buffer��ȥ����д�ص�lsn��Ϊ��lpn�ڳ�λ�ã�
	*���ȼ�Ҫ�����free sector����ʾ���ж��ٿ���ֱ��д��buffer�ڵ㣩��
	*���free_sector>=sector_count�����ж���Ŀռ乻lpn������д������Ҫ����д������
	*����û�ж���Ŀռ乩lpn������д����ʱ��Ҫ�ͷ�һ���ֿռ䣬����д�����󡣾�Ҫcreat_sub_request()
	*************************************************************************************************/
	if (buffer_node == NULL)
	{
		free_sector = ssd->dram->buffer->max_buffer_sector - ssd->dram->buffer->buffer_sector_count;
		if (free_sector >= sector_count)
		{
			flag = 1;
		}
		if (flag == 0)
		{
			write_back_count = sector_count - free_sector;
			ssd->dram->buffer->write_miss_hit = ssd->dram->buffer->write_miss_hit + write_back_count;
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
						ssd->dram->buffer->write_hit++;
						req->complete_lsn_count++;                                        /*�ؼ� ����buffer������ʱ ����req->complete_lsn_count++��ʾ��buffer��д�����ݡ�*/
					}
					else
					{
					}
				}
				else
				{
					/************************************************************************************************************
					*��lsnû�����У����ǽڵ���buffer�У���Ҫ�����lsn�ӵ�buffer�Ķ�Ӧ�ڵ���
					*��buffer��ĩ����һ���ڵ㣬��һ���Ѿ�д�ص�lsn�ӽڵ���ɾ��(����ҵ��Ļ�)����������ڵ��״̬��ͬʱ������µ�
					*lsn�ӵ���Ӧ��buffer�ڵ��У��ýڵ������bufferͷ�����ڵĻ��������Ƶ�ͷ�������û���ҵ��Ѿ�д�ص�lsn����buffer
					*�ڵ���һ��group����д�أ�����������������������ϡ�������ǰ����һ��channel�ϡ�
					*��һ��:��buffer��β���Ѿ�д�صĽڵ�ɾ��һ����Ϊ�µ�lsn�ڳ��ռ䣬������Ҫ�޸Ķ�βĳ�ڵ��stored״̬���ﻹ��Ҫ
					*       ���ӣ���û�п���֮��ɾ����lsnʱ����Ҫ�����µ�д������д��LRU���Ľڵ㡣
					*�ڶ���:���µ�lsn�ӵ�������buffer�ڵ��С�
					*************************************************************************************************************/
					ssd->dram->buffer->write_miss_hit++;

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
*�����������������������ֻ���������д�����Ѿ���buffer_management()�����д�����
*����������к�buffer���еļ�飬��ÿ������ֽ�������󣬽���������й���channel�ϣ�
*��ͬ��channel���Լ������������
**********************************************************************************/

struct ssd_info *distribute(struct ssd_info *ssd)
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

	req = ssd->request_tail;
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
			//�������һЩ��������Ҫ����
			if (req->complete_lsn_count != ssd->request_tail->size)
			{
				first_lsn = req->lsn;
				last_lsn = first_lsn + req->size;
				complt = req->need_distr_flag;    //��buff���Ѿ���ɵ���ҳ

				//start��end��ʾ����ҳΪ��λ������ʼ�ͽ���λ��
				start = first_lsn - first_lsn % ssd->parameter->subpage_page;
				end = (last_lsn / ssd->parameter->subpage_page + 1) * ssd->parameter->subpage_page;

				//�������ж��ٸ���ҳ�����ж��ٸ�int������־
				i = (end - start) / 32;


				while (i >= 0)
				{
					/*************************************************************************************
					*һ��32λ���������ݵ�ÿһλ����һ����ҳ��32/ssd->parameter->subpage_page�ͱ�ʾ�ж���ҳ��
					*�����ÿһҳ��״̬��������� req->need_distr_flag�У�Ҳ����complt�У�ͨ���Ƚ�complt��
					*ÿһ����full_page���Ϳ���֪������һҳ�Ƿ�����ɡ����û���������ͨ��creat_sub_request
					��������������
					*************************************************************************************/
					for (j = 0; j<32 / ssd->parameter->subpage_page; j++)
					{
						k = (complt[((end - start) / 32 - i)] >> (ssd->parameter->subpage_page*j)) & full_page;   //��buff�в���λ״̬�෴���Ѷ�Ӧ��lpn��λ״̬ȡ����
						if (k != 0)    //˵����ǰ��lpnû����buffe����ȫ���У���Ҫ���ж�����
						{
							lpn = start / ssd->parameter->subpage_page + ((end - start) / 32 - i) * 32 / ssd->parameter->subpage_page + j;
							sub_size = transfer_size(ssd, k, lpn, req);  //sub_size��Ϊ����Ҫȥ���������ҳ����
							if (sub_size == 0)
							{
								continue;
							}
							else
							{
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
	return ssd;
}


/*********************************************************************************************
*no_buffer_distribute()�����Ǵ���ssdû��dram��ʱ��
*���Ƕ�д����Ͳ�������Ҫ��buffer����Ѱ�ң�ֱ������creat_sub_request()���������������ٴ���
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
	req = ssd->request_tail;
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

/*********************************************************
*transfer_size()���������þ��Ǽ�������������Ҫ�����size
*�����е���������first_lpn��last_lpn�������ر��������Ϊ��
*��������º��п��ܲ��Ǵ���һ��ҳ���Ǵ���һҳ��һ���֣���
*Ϊlsn�п��ܲ���һҳ�ĵ�һ����ҳ��
*********************************************************/
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
*����ÿһҳ��״̬�����ÿһ��Ҫ�������ҳ����Ŀ��Ҳ����һ����������Ҫ�������ҳ��ҳ��
************************************************************************************/
unsigned int size(unsigned int stored)
{
	unsigned int i, total = 0, mask = 0x80000000;

#ifdef DEBUG
	printf("enter size\n");
#endif
	for (i = 1; i <= 32; i++)
	{
		if (stored & mask) total++;     //total������ʾ��ҳ�б�־λΪ0
		stored <<= 1;
	}
#ifdef DEBUG
	printf("leave size\n");
#endif
	return total;
}


/**********************************************
*��������Ĺ����Ǹ���lpn��size��state����������
**********************************************/
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

/**********************
*�������ֻ������д����
***********************/
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
		/***************************************
		*һ���Ƕ�̬����ļ������
		*0��ȫ��̬����
		*1����ʾchannel��package��die��plane��̬
		****************************************/
		//ֻ����ȫ��̬����
		switch (ssd->parameter->dynamic_allocation)
		{
			case 0:
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
					sub_req->update = update;
					sub_req->state = (sub_req->state | update->state);
					sub_req->size = size(sub_req->state);
				}

				break;
			}
		}
	}


	return SUCCESS;
}