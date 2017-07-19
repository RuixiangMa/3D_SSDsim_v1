/*****************************************************************************************************************************
This is a project on 3D_SSDsim, based on ssdsim under the framework of the completion of structures, the main function:
1.Support for 3D commands, for example:mutli plane\interleave\copyback\program suspend/Resume..etc
2.Multi - level parallel simulation
3.Clear hierarchical interface
4.4-layer structure

FileName�� buffer.c
Author: Zuo Lu 		Version: 1.5	Date:2017/07/07
Description: 
buff layer: only contains data cache (minimum processing size for the sector, that is, unit = 512B), mapping table (page-level);

History:
<contributor>     <time>        <version>       <desc>											<e-mail>
Zuo Lu	        2017/04/06	      1.0		    Creat 3D_SSDsim									617376665@qq.com
Zuo Lu			2017/05/12		  1.1			Support advanced commands:mutli plane			617376665@qq.com
Zuo Lu			2017/06/12		  1.2			Support advanced commands:half page read		617376665@qq.com
Zuo Lu			2017/06/16		  1.3			Support advanced commands:one shot program		617376665@qq.com
Zuo Lu			2017/06/22		  1.4			Support advanced commands:one shot read			617376665@qq.com
Zuo Lu			2017/07/07		  1.5			Support advanced commands:erase suspend/resume  617376665@qq.com
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

extern int secno_num_per_page, secno_num_sub_page;

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
	struct request *new_request;

#ifdef DEBUG
	printf("enter buffer_management,  current time:%I64u\n", ssd->current_time);
#endif


	ssd->dram->current_time = ssd->current_time;
	new_request = ssd->request_work;   //ȡ�����Ϲ���ָ�������
	
	if (new_request->operation == READ)
	{
		//�ȴ���д���棬�ڴ��������
		handle_write_buffer(ssd, new_request);
		//handle_read_cache(ssd, new_request);
	}
	else if (new_request->operation == WRITE)
	{   
		//�ȴ�������棬�ڴ���д����
		//handle_read_cache(ssd, new_request);
		handle_write_buffer(ssd, new_request);
	}
	//��ȫ���У��������ʱ
	if (new_request->subs == NULL)
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
	unsigned int mask;
	unsigned int state,offset1 = 0, offset2 = 0, flag = 0;

	//����4kb����
	req->size = ((req->lsn + req->size - 1) / secno_num_sub_page - (req->lsn) / secno_num_sub_page + 1) * secno_num_sub_page;
	req->lsn /= secno_num_sub_page;
	req->lsn *= secno_num_sub_page;

	full_page = ~(0xffffffff << ssd->parameter->subpage_page);    //ȫҳ����ʾlpn����ҳ��������
	lsn = req->lsn;
	lpn = req->lsn / secno_num_per_page;
	last_lpn = (req->lsn + req->size - 1) / secno_num_per_page;
	first_lpn = req->lsn / secno_num_per_page;   //����lpn

	while (lpn <= last_lpn)
	{
		mask = ~(0xffffffff << (ssd->parameter->subpage_page));   //�����ʾ������ҳ������
		state = mask;

		if (lpn == first_lpn)
		{
			//offset��ʾstate��0�ĸ�����Ҳ���ǵ�һ��ҳ��ȱʧ�Ĳ���
			offset1 = ssd->parameter->subpage_page - (((lpn + 1)*secno_num_per_page - req->lsn)/secno_num_sub_page);
			state = state&(0xffffffff << offset1);
		}
		if (lpn == last_lpn)
		{
			offset2 = ssd->parameter->subpage_page - ((lpn + 1)*secno_num_per_page - (req->lsn + req->size)) / secno_num_sub_page;
			state = state&(~(0xffffffff << offset2));
		}

		if (req->operation == READ)
			ssd = check_w_buff(ssd, lpn, state, NULL, req);
		else if (req->operation == WRITE)
			ssd = insert2buffer(ssd, lpn, state, NULL, req);

		lpn++;
	}
	
	return ssd;
}

struct ssd_info *handle_read_cache(struct ssd_info *ssd, struct request *req)
{
	return ssd;
}

struct ssd_info * check_w_buff(struct ssd_info *ssd, unsigned int lpn, int state, struct sub_request *sub, struct request *req)
{
	unsigned int sub_req_state = 0, sub_req_size = 0, sub_req_lpn = 0;
	struct buffer_group *buffer_node, key;
	struct sub_request *sub_req = NULL;

	key.group = lpn;
	buffer_node = (struct buffer_group*)avlTreeFind(ssd->dram->buffer, (TREE_NODE *)&key);		// buffer node 

	if (buffer_node == NULL)         //δ���У�ȥflash�϶�
	{
		sub_req = NULL;
		sub_req_state = state;
		sub_req_size = size(state);
		sub_req_lpn = lpn;
		sub_req = creat_sub_request(ssd, sub_req_lpn, sub_req_size, sub_req_state, req, READ);

		ssd->dram->buffer->read_miss_hit++;
	}
	else
	{
		if ((state&buffer_node->stored) == state)   //��ȫ����
		{
			ssd->dram->buffer->read_hit++;
		}
		else
		{
			sub_req = NULL;
			sub_req_state = (state | buffer_node->stored) ^ buffer_node->stored;
			sub_req_size = size(sub_req_state);
			sub_req_lpn = lpn;
			sub_req = creat_sub_request(ssd, sub_req_lpn, sub_req_size, sub_req_state, req, READ);

			ssd->dram->buffer->read_miss_hit++;
		}
	}
	return ssd;
}


/*******************************************************************************
*The function is to write data to the buffer,Called by buffer_management()
********************************************************************************/
struct ssd_info * insert2buffer(struct ssd_info *ssd, unsigned int lpn, int state, struct sub_request *sub, struct request *req)
{
	int write_back_count, flag = 0;                                                             /*flag��ʾΪд���������ڿռ��Ƿ���ɣ�0��ʾ��Ҫ��һ���ڣ�1��ʾ�Ѿ��ڿ�*/
	unsigned int sector_count, active_region_flag = 0, free_sector = 0;
	struct buffer_group *buffer_node = NULL, *pt, *new_node = NULL, key;
	struct sub_request *sub_req = NULL, *update = NULL;

	unsigned int sub_req_state = 0, sub_req_size = 0, sub_req_lpn = 0;
	unsigned int add_size;

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
		}
		if (flag == 0)
		{
			write_back_count = sector_count - free_sector;
			while (write_back_count>0)
			{
				sub_req = NULL;
				sub_req_state = ssd->dram->buffer->buffer_tail->stored;
				sub_req_size = size(ssd->dram->buffer->buffer_tail->stored);
				sub_req_lpn = ssd->dram->buffer->buffer_tail->group;
				
				/*
				if (ssd->parameter->allocation_scheme == DYNAMIC_ALLOCATION)
					insert2_command_buffer(ssd, sub_req_lpn, sub_req_size, sub_req_state, req, WRITE);
				else
					sub_req = creat_sub_request(ssd, sub_req_lpn, sub_req_size, sub_req_state, req, WRITE);
				*/
				//��������䵽command_buffer
				distribute2_command_buffer(ssd, sub_req_lpn, sub_req_size, sub_req_state, req, WRITE);

				ssd->dram->buffer->write_miss_hit++;
				ssd->dram->buffer->buffer_sector_count = ssd->dram->buffer->buffer_sector_count - sub_req_size;
				//ssd->dram->buffer->buffer_sector_count = ssd->dram->buffer->buffer_sector_count - sub_req->size;
				
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

				write_back_count = write_back_count - sub_req_size;                            /*��Ϊ������ʵʱд�ز�������Ҫ������д�ز�����������*/
				//write_back_count = write_back_count - sub_req->size;
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
		ssd->dram->buffer->write_hit++;
	}
	else
	{
		ssd->dram->buffer->write_hit++;
		if ((state&buffer_node->stored) == state)   //��ȫ����
		{
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
				req->complete_lsn_count += size(state);                                       
			}
		}
		else
		{
			add_size = size((state | buffer_node->stored) ^ buffer_node->stored);					 //��Ҫ����д�뻺���
			while((ssd->dram->buffer->buffer_sector_count + add_size) > ssd->dram->buffer->max_buffer_sector)
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
				//д��β�ڵ�
				sub_req = NULL;
				sub_req_state = ssd->dram->buffer->buffer_tail->stored;
				sub_req_size = size(ssd->dram->buffer->buffer_tail->stored);
				sub_req_lpn = ssd->dram->buffer->buffer_tail->group;
				/*
				if (ssd->parameter->allocation_scheme == DYNAMIC_ALLOCATION)
					insert2_command_buffer(ssd, sub_req_lpn, sub_req_size, sub_req_state, req, WRITE);
				else
					sub_req = creat_sub_request(ssd, sub_req_lpn, sub_req_size, sub_req_state, req, WRITE);*/
				
				//��������䵽command_buffer
				distribute2_command_buffer(ssd, sub_req_lpn, sub_req_size, sub_req_state, req, WRITE);

				ssd->dram->buffer->write_miss_hit++;
				//ɾ��β�ڵ�
				pt = ssd->dram->buffer->buffer_tail;
				avlTreeDel(ssd->dram->buffer, (TREE_NODE *)pt);
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

				ssd->dram->buffer->buffer_sector_count = ssd->dram->buffer->buffer_sector_count - sub_req_size;
				//ssd->dram->buffer->buffer_sector_count = ssd->dram->buffer->buffer_sector_count - sub_req->size;
			}
			/*�����buffer�ڵ㲻��buffer�Ķ��ף���Ҫ������ڵ��ᵽ����*/
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
			buffer_node->stored = buffer_node->stored | state;
			buffer_node->dirty_clean = buffer_node->dirty_clean | state;
			ssd->dram->buffer->buffer_sector_count += add_size;
		}
	}
	return ssd;
}


struct ssd_info * getout2buffer(struct ssd_info *ssd, struct sub_request *sub, struct request *req)
{
	unsigned int req_write_counts = 0;
	struct buffer_group *pt;
	unsigned int sub_req_state = 0, sub_req_size = 0, sub_req_lpn = 0, sub_req_type = 0;
	struct sub_request *sub_req = NULL;

	//�ȴӶ����������滻ҳ�������ٴ�һ����������ҳ
	if (ssd->dram->command_buffer->command_buff_page != 0)
	{
		pt = ssd->dram->command_buffer->buffer_tail;
		sub_req_state = pt->stored;
		sub_req_size = size(pt->stored);
		sub_req_lpn = pt->group;
		sub_req = NULL;
		sub_req = creat_sub_request(ssd, sub_req_lpn, sub_req_size, sub_req_state, req, WRITE);

		//Delete the node
		ssd->dram->command_buffer->command_buff_page--;

		if (pt == ssd->dram->command_buffer->buffer_tail)
		{
			if (ssd->dram->command_buffer->buffer_head->LRU_link_next == NULL){
				ssd->dram->command_buffer->buffer_head = NULL;
				ssd->dram->command_buffer->buffer_tail = NULL;
			}
			else{
				ssd->dram->command_buffer->buffer_tail = pt->LRU_link_pre;
				ssd->dram->command_buffer->buffer_tail->LRU_link_next = NULL;
			}
		}
		else
		{
			printf("buffer_tail delete failed\n");
			getchar();
		}

		avlTreeDel(ssd->dram->command_buffer, (TREE_NODE *)pt);
		pt->LRU_link_next = NULL;
		pt->LRU_link_pre = NULL;
		AVL_TREENODE_FREE(ssd->dram->command_buffer, (TREE_NODE *)pt);
		pt = NULL;
	}
	else
	{
		//tail node replacement
		pt = ssd->dram->buffer->buffer_tail;
		sub_req_state = pt->stored;
		sub_req_size = size(pt->stored);
		sub_req_lpn = pt->group;
		sub_req = NULL;
		sub_req = creat_sub_request(ssd, sub_req_lpn, sub_req_size, sub_req_state, req, WRITE);
		ssd->dram->buffer->write_miss_hit++;

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
	}


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
	unsigned int flag = 0, active_region_flag = 0;           //to indicate the lsn is hitted or not
	struct request *req = NULL;
	struct sub_request *sub = NULL, *sub_r = NULL, *update = NULL;
	struct local *loc = NULL;
	struct channel_info *p_ch = NULL;


	unsigned int mask = 0;
	unsigned int offset1 = 0, offset2 = 0;
	unsigned int sub_size = 0;
	unsigned int sub_state = 0;

	ssd->dram->current_time = ssd->current_time;
	req = ssd->request_work;
	lsn = req->lsn;
	lpn = req->lsn / secno_num_per_page;
	last_lpn = (req->lsn + req->size - 1) / secno_num_per_page;
	first_lpn = req->lsn / secno_num_per_page;

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
			if (lpn == first_lpn)
			{
				//offset��ʾstate��0�ĸ�����Ҳ���ǵ�һ��ҳ��ȱʧ�Ĳ���
				offset1 = ssd->parameter->subpage_page - (((lpn + 1)*secno_num_per_page - req->lsn) / secno_num_sub_page);
				state = state&(0xffffffff << offset1);
			}
			if (lpn == last_lpn)
			{
				offset2 = ssd->parameter->subpage_page - ((lpn + 1)*secno_num_per_page - (req->lsn + req->size)) / secno_num_sub_page;
				state = state&(~(0xffffffff << offset2));
			}

			sub_size = size(state);
			sub = creat_sub_request(ssd, lpn, sub_size, state, req, req->operation);
			lpn++;
		}
	}

	return ssd;
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
		if (stored & mask) total++;     
		stored <<= 1;
	}
#ifdef DEBUG
	printf("leave size\n");
#endif
	return total;
}


/*
struct ssd_info * insert2_command_buffer(struct ssd_info * ssd, unsigned int lpn, int size_count, unsigned int state, struct request * req, unsigned int operation)
{
	unsigned int i = 0;
	unsigned int sub_req_state = 0, sub_req_size = 0, sub_req_lpn = 0;
	struct buffer_group *command_buffer_node = NULL, *pt, *new_node = NULL, key;
	struct sub_request *sub_req = NULL;

	//��������Ľڵ㣬�ж��Ƿ�������
	key.group = lpn;
	command_buffer_node = (struct buffer_group*)avlTreeFind(ssd->dram->command_buffer, (TREE_NODE *)&key);
	
	if (command_buffer_node == NULL)
	{
		//����Ǹ���д������ֱ��д��flash��
		
		//if ((state&ssd->dram->map->map_entry[lpn].state) != ssd->dram->map->map_entry[lpn].state)
		//{
			//creat_sub_request(ssd, lpn, size_count, state, req, operation);
			//return ssd;
		//}
		

		//���� һ��buff_node,�������ҳ������ֱ�ֵ��������Ա��������ӵ�����
		new_node = NULL;
		new_node = (struct buffer_group *)malloc(sizeof(struct buffer_group));
		alloc_assert(new_node, "buffer_group_node");
		memset(new_node, 0, sizeof(struct buffer_group));

		new_node->group = lpn;
		new_node->stored = state;
		new_node->dirty_clean = state;
		new_node->LRU_link_pre = NULL;
		new_node->LRU_link_next = ssd->dram->command_buffer->buffer_head;
		if (ssd->dram->command_buffer->buffer_head != NULL){
			ssd->dram->command_buffer->buffer_head->LRU_link_pre = new_node;
		}
		else{
			ssd->dram->command_buffer->buffer_tail = new_node;
		}
		ssd->dram->command_buffer->buffer_head = new_node;
		new_node->LRU_link_pre = NULL;
		avlTreeAdd(ssd->dram->command_buffer, (TREE_NODE *)new_node);
		ssd->dram->command_buffer->command_buff_page++;

		//���������������ʱ����flush��������������ڴ�һ����flush��������
		if (ssd->dram->command_buffer->command_buff_page == ssd->dram->command_buffer->max_command_buff_page)
		{
			printf("begin to flush command_buffer\n");
			for (i = 0; i < ssd->dram->command_buffer->max_command_buff_page; i++)
			{
				sub_req = NULL;
				sub_req_state = ssd->dram->command_buffer->buffer_tail->stored;
				sub_req_size = size(ssd->dram->command_buffer->buffer_tail->stored);
				sub_req_lpn = ssd->dram->command_buffer->buffer_tail->group;
				sub_req = creat_sub_request(ssd, sub_req_lpn, sub_req_size, sub_req_state, req, operation);

				//ɾ��buff�еĽڵ�
				pt = ssd->dram->command_buffer->buffer_tail;
				avlTreeDel(ssd->dram->command_buffer, (TREE_NODE *)pt);
				if (ssd->dram->command_buffer->buffer_head->LRU_link_next == NULL){
					ssd->dram->command_buffer->buffer_head = NULL;
					ssd->dram->command_buffer->buffer_tail = NULL;
				}
				else{
					ssd->dram->command_buffer->buffer_tail = ssd->dram->command_buffer->buffer_tail->LRU_link_pre;
					ssd->dram->command_buffer->buffer_tail->LRU_link_next = NULL;
				}
				pt->LRU_link_next = NULL;
				pt->LRU_link_pre = NULL;
				AVL_TREENODE_FREE(ssd->dram->command_buffer, (TREE_NODE *)pt);
				pt = NULL;

				ssd->dram->command_buffer->command_buff_page--;
			}
			if (ssd->dram->command_buffer->command_buff_page != 0)
			{
				printf("command buff flush failed\n");
				getchar();
			}
		}
	}
	else  //���е�������ϲ�������
	{
		if (ssd->dram->command_buffer->buffer_head != command_buffer_node)
		{
			if (ssd->dram->command_buffer->buffer_tail == command_buffer_node)
			{
				command_buffer_node->LRU_link_pre->LRU_link_next = NULL;
				ssd->dram->command_buffer->buffer_tail = command_buffer_node->LRU_link_pre;
			}
			else
			{
				command_buffer_node->LRU_link_pre->LRU_link_next = command_buffer_node->LRU_link_next;
				command_buffer_node->LRU_link_next->LRU_link_pre = command_buffer_node->LRU_link_pre;
			}
			command_buffer_node->LRU_link_next = ssd->dram->command_buffer->buffer_head;
			ssd->dram->command_buffer->buffer_head->LRU_link_pre = command_buffer_node;
			command_buffer_node->LRU_link_pre = NULL;
			ssd->dram->command_buffer->buffer_head = command_buffer_node;
		}

		command_buffer_node->stored = command_buffer_node->stored | state;
		command_buffer_node->dirty_clean = command_buffer_node->dirty_clean | state;
	}

	return ssd;
}
*/


struct ssd_info * distribute2_command_buffer(struct ssd_info * ssd, unsigned int lpn, int size_count, unsigned int state, struct request * req, unsigned int operation)
{
	unsigned int channel, chip, die, plane;
	unsigned int channel_num, chip_num, die_num, plane_num;
	unsigned int aim_plane;
	struct buffer_info * aim_command_buffer;

	if (ssd->parameter->allocation_scheme == STATIC_ALLOCATION)
	{
		channel_num = ssd->parameter->channel_number;
		chip_num = ssd->parameter->chip_channel[0];
		die_num = ssd->parameter->die_chip;
		plane_num = ssd->parameter->plane_die;

		//��̬�������ȼ���plane>channel>chip>die
		plane = lpn % plane_num;
		channel = (lpn / plane_num) % channel_num;
		chip = (lpn / (plane_num*channel_num)) % chip_num;
		die = (lpn / (plane_num*channel_num*chip_num)) % die_num;

		//���䵽��Ӧ��plane_buffer��
		aim_plane = channel * (plane_num*die_num*chip_num) + chip *(plane_num*die_num) + die*plane_num + plane;
		aim_command_buffer = ssd->dram->static_plane_buffer[aim_plane];
	}
	else if (ssd->parameter->allocation_scheme == DYNAMIC_ALLOCATION)
	{
		aim_command_buffer = ssd->dram->command_buffer;
	}
	
	//��������뵽��Ӧ�Ļ�����
	insert2_command_buffer(ssd, aim_command_buffer, lpn, size_count, state, req, operation);
}

struct ssd_info * insert2_command_buffer(struct ssd_info * ssd, struct buffer_info * command_buffer, unsigned int lpn, int size_count, unsigned int state, struct request * req, unsigned int operation)
{
	unsigned int i = 0;
	unsigned int sub_req_state = 0, sub_req_size = 0, sub_req_lpn = 0;
	struct buffer_group *command_buffer_node = NULL, *pt, *new_node = NULL, key;
	struct sub_request *sub_req = NULL;

	//��������Ľڵ㣬�ж��Ƿ�������
	key.group = lpn;
	command_buffer_node = (struct buffer_group*)avlTreeFind(command_buffer, (TREE_NODE *)&key);

	if (command_buffer_node == NULL)
	{
		//����Ǹ���д������ֱ��д��flash��
		/*
		if ((state&ssd->dram->map->map_entry[lpn].state) != ssd->dram->map->map_entry[lpn].state)
		{
		creat_sub_request(ssd, lpn, size_count, state, req, operation);
		return ssd;
		}
		*/

		//���� һ��buff_node,�������ҳ������ֱ�ֵ��������Ա��������ӵ�����
		new_node = NULL;
		new_node = (struct buffer_group *)malloc(sizeof(struct buffer_group));
		alloc_assert(new_node, "buffer_group_node");
		memset(new_node, 0, sizeof(struct buffer_group));

		new_node->group = lpn;
		new_node->stored = state;
		new_node->dirty_clean = state;
		new_node->LRU_link_pre = NULL;
		new_node->LRU_link_next = command_buffer->buffer_head;
		if (command_buffer->buffer_head != NULL){
			command_buffer->buffer_head->LRU_link_pre = new_node;
		}
		else{
			command_buffer->buffer_tail = new_node;
		}
		command_buffer->buffer_head = new_node;
		new_node->LRU_link_pre = NULL;
		avlTreeAdd(command_buffer, (TREE_NODE *)new_node);
		command_buffer->command_buff_page++;

		//���������������ʱ����flush��������������ڴ�һ����flush��������
		if (command_buffer->command_buff_page == command_buffer->max_command_buff_page)
		{
			printf("begin to flush command_buffer\n");
			for (i = 0; i < command_buffer->max_command_buff_page; i++)
			{
				sub_req = NULL;
				sub_req_state = command_buffer->buffer_tail->stored;
				sub_req_size = size(command_buffer->buffer_tail->stored);
				sub_req_lpn = command_buffer->buffer_tail->group;
				sub_req = creat_sub_request(ssd, sub_req_lpn, sub_req_size, sub_req_state, req, operation);

				//ɾ��buff�еĽڵ�
				pt = command_buffer->buffer_tail;
				avlTreeDel(command_buffer, (TREE_NODE *)pt);
				if (command_buffer->buffer_head->LRU_link_next == NULL){
					command_buffer->buffer_head = NULL;
					command_buffer->buffer_tail = NULL;
				}
				else{
					command_buffer->buffer_tail = command_buffer->buffer_tail->LRU_link_pre;
					command_buffer->buffer_tail->LRU_link_next = NULL;
				}
				pt->LRU_link_next = NULL;
				pt->LRU_link_pre = NULL;
				AVL_TREENODE_FREE(command_buffer, (TREE_NODE *)pt);
				pt = NULL;

				command_buffer->command_buff_page--;
			}
			if (command_buffer->command_buff_page != 0)
			{
				printf("command buff flush failed\n");
				getchar();
			}
		}
	}
	else  //���е�������ϲ�������
	{
		if (command_buffer->buffer_head != command_buffer_node)
		{
			if (command_buffer->buffer_tail == command_buffer_node)
			{
				command_buffer_node->LRU_link_pre->LRU_link_next = NULL;
				command_buffer->buffer_tail = command_buffer_node->LRU_link_pre;
			}
			else
			{
				command_buffer_node->LRU_link_pre->LRU_link_next = command_buffer_node->LRU_link_next;
				command_buffer_node->LRU_link_next->LRU_link_pre = command_buffer_node->LRU_link_pre;
			}
			command_buffer_node->LRU_link_next = command_buffer->buffer_head;
			command_buffer->buffer_head->LRU_link_pre = command_buffer_node;
			command_buffer_node->LRU_link_pre = NULL;
			command_buffer->buffer_head = command_buffer_node;
		}

		command_buffer_node->stored = command_buffer_node->stored | state;
		command_buffer_node->dirty_clean = command_buffer_node->dirty_clean | state;
	}

	return ssd;
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

	//��������������������ϣ�Ϊ�����ִ�������׼��
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
		sub->suspend_req_flag = NORMAL_TYPE;

		p_ch = &ssd->channel_head[loc->channel];
		sub->ppn = ssd->dram->map->map_entry[lpn].pn;
		sub->operation = READ;
		sub->state = state;
		sub_r = ssd->channel_head[loc->channel].subs_r_head;

		
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
			if (ssd->channel_head[loc->channel].subs_r_tail != NULL)
			{
				ssd->channel_head[loc->channel].subs_r_tail->next_node = sub;
				ssd->channel_head[loc->channel].subs_r_tail = sub;
			}
			else
			{
				ssd->channel_head[loc->channel].subs_r_head = sub;
				ssd->channel_head[loc->channel].subs_r_tail = sub;
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
*Write request allocation mount
***************************************/
Status allocate_location(struct ssd_info * ssd, struct sub_request *sub_req)
{
	struct sub_request * update = NULL, *sub_r = NULL;
	unsigned int channel_num = 0, chip_num = 0, die_num = 0, plane_num = 0 ,flag;
	unsigned int channel, chip, die, plane;
	struct local *location = NULL;

	channel_num = ssd->parameter->channel_number;
	chip_num = ssd->parameter->chip_channel[0];
	die_num = ssd->parameter->die_chip;
	plane_num = ssd->parameter->plane_die;

	//�ж��Ƿ���������д����������д����Ҫ�ȶ���д
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
			update->suspend_req_flag = NORMAL_TYPE;

			sub_r = ssd->channel_head[location->channel].subs_r_head;
			flag = 0;
			while (sub_r != NULL)
			{
				if (sub_r->ppn == update->ppn)
				{
					flag = 1;
					break;
				}
				sub_r = sub_r->next_node;
			}

			if (flag == 0)
			{
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
			else
			{
				update->current_state = SR_R_DATA_TRANSFER;
				update->current_time = ssd->current_time;
				update->next_state = SR_COMPLETE;
				update->next_state_predict_time = ssd->current_time + 1000;
				update->complete_time = ssd->current_time + 1000;
			}
		}
	}

	//�־�̬���Ƕ�̬���䣬��̬����ֱ�ӷ�����ssd��
	if (ssd->parameter->allocation_scheme == DYNAMIC_ALLOCATION)                                          /*��̬��������*/
	{
		if (ssd->parameter->dynamic_allocation == FULL_ALLOCATION)
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
	else if (ssd->parameter->allocation_scheme == STATIC_ALLOCATION)							//������plane��
	{
		if (ssd->parameter->static_allocation == 0)
		{
			sub_req->location->channel = (sub_req->lpn / plane_num) % channel_num;
			sub_req->location->chip = (sub_req->lpn / (plane_num*channel_num)) % chip_num;
			sub_req->location->die = (sub_req->lpn / (plane_num*channel_num*chip_num)) % die_num;
			sub_req->location->plane = sub_req->lpn % plane_num;
		}

		channel = sub_req->location->channel;
		chip = sub_req->location->chip;
		die = sub_req->location->die;
		plane = sub_req->location->plane;
		
		//�������������������channel�ϣ�ͬʱ������plane��
		/*
		if (ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].subs_w_tail != NULL)
		{
			ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].subs_w_tail->next_node = sub_req;
			ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].subs_w_tail = sub_req;
		}
		else
		{
			ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].subs_w_tail = sub_req;
			ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].subs_w_head = sub_req;
		}
		*/	

		if (ssd->channel_head[channel].subs_w_tail != NULL)
		{
			ssd->channel_head[channel].subs_w_tail->next_node = sub_req;
			ssd->channel_head[channel].subs_w_tail = sub_req;
		}
		else
		{
			ssd->channel_head[channel].subs_w_tail = sub_req;
			ssd->channel_head[channel].subs_w_head = sub_req;
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

	return SUCCESS;
}

