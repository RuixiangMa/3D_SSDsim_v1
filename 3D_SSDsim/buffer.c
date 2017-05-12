/*****************************************************************************************************************************
This is a project on 3D_SSDsim, based on ssdsim under the framework of the completion of structures, the main function:
1.Support for 3D commands, for example:mutli plane\interleave\copyback\program suspend/Resume..etc
2.Multi - level parallel simulation
3.Clear hierarchical interface
4.4-layer structure

FileName�� ssd.c
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
	unsigned int page_type;
	unsigned int buff_free_count;
	unsigned int request_flag = 0;

	unsigned int lz_k;

#ifdef DEBUG
	printf("enter buffer_management,  current time:%I64u\n", ssd->current_time);
#endif

	ssd->dram->current_time = ssd->current_time;
	full_page = ~(0xffffffff << ssd->parameter->subpage_page);

	//new_request = ssd->request_tail;
	new_request = ssd->request_work;
	lsn = new_request->lsn;
	lpn = new_request->lsn / ssd->parameter->subpage_page;
	last_lpn = (new_request->lsn + new_request->size - 1) / ssd->parameter->subpage_page;
	first_lpn = new_request->lsn / ssd->parameter->subpage_page;   //����lpn

	if (new_request->operation == READ)
	{

		//int��32λ��Ҳ����˵need_distr_flag ��һ����bitΪ��λ��״̬λ����
		//_CrtSetBreakAlloc(34459);
		new_request->need_distr_flag = (unsigned int*)malloc(sizeof(unsigned int)*((last_lpn - first_lpn + 1)*ssd->parameter->subpage_page / 32 + 1));
		alloc_assert(new_request->need_distr_flag, "new_request->need_distr_flag");
		memset(new_request->need_distr_flag, 0, sizeof(unsigned int)*((last_lpn - first_lpn + 1)*ssd->parameter->subpage_page / 32 + 1));

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
					new_request->complete_lsn_count++;
				}
				else if (flag == 0)
				{
					//ssd->dram->buffer->read_miss_hit++;
				}

				need_distb_flag = need_distb_flag&lsn_flag;   //���浱ǰlpn����ҳ״̬��0��ʾ���У�1��ʾδ����
				flag = 0;
				lsn++;
			}

			if (need_distb_flag == 0x00000000)
				ssd->dram->buffer->read_hit++;
			else
				ssd->dram->buffer->read_miss_hit++;

			//���λ�÷ǳ�����Ҫ
			index = (lpn - first_lpn) / (32 / ssd->parameter->subpage_page);  //index��ʾneed_distr_flag�кܶ��int�������־��index����������������
			new_request->need_distr_flag[index] = new_request->need_distr_flag[index] | (need_distb_flag << (((lpn - first_lpn) % (32 / ssd->parameter->subpage_page))*ssd->parameter->subpage_page));
			lpn++;

		}
	}
	else if (new_request->operation == WRITE)
	{
		//if (new_request->lsn == 725320)
			//getchar();

		while (new_request->cmplt_flag == 0)
		{
			//���ȶԸ����������lpn���м��
			lpn = new_request->lsn / ssd->parameter->subpage_page;
			buff_free_count = ssd->dram->buffer->max_buffer_sector - ssd->dram->buffer->buffer_sector_count;
			while (lpn <= last_lpn)
			{
				//�����while���棬���ȼ��һ��buff���ʱ��ɲ����Է��£�����ܹ����£���insertbuff�����ܾ�����buff
				key.group = lpn;
				buffer_node = (struct buffer_group*)avlTreeFind(ssd->dram->buffer, (TREE_NODE *)&key);	    //��buff�м���Ƿ�����
				if (buffer_node == NULL)
				{
					if (buff_free_count >= ssd->parameter->subpage_page)
					{
						buff_free_count = buff_free_count - ssd->parameter->subpage_page;
					}
					else  //buff����������buff,����ѭ��
					{
						ssd->buffer_full_flag = 1;
						break;
					}
				}
				lpn++;
			}

			if (ssd->buffer_full_flag == 0)
			{
				//buff��ʱδ������buff��д������
				//lz_k++;
				lpn = new_request->lsn / ssd->parameter->subpage_page;
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

					//����lpn�����ͣ���lpn��full page����partial page
					if ((state&ssd->dram->map->map_entry[lpn].state) != ssd->dram->map->map_entry[lpn].state)
						page_type = 1;
					else
						page_type = 0;

					ssd = insert2buffer(ssd, lpn, state, page_type, NULL, new_request);
					lpn++;
				}
				lpn = 0;
				new_request->cmplt_flag = 1;			//�������Ѿ���ִ��
				//printf("write insert,%d\n", lz_k);
				//if (lz_k == 322931)
					//getchar();

			}
			else
			{
				//buff���ˣ���buff��ĩβ�滻����������ȥ
				getout2buffer(ssd, NULL, new_request);
				if (ssd->buffer_full_flag == 1)
					break;

				new_request->cmplt_flag = 0;			//������δ��ִ��
				//printf("write 2 lpn\n");
			}
		}
	}

	
	complete_flag = 1;
	if (new_request->operation == READ)
	{
		for (j = 0; j <= (last_lpn - first_lpn + 1)*ssd->parameter->subpage_page / 32; j++)
		{
			if (new_request->need_distr_flag[j] != 0)
			{
				complete_flag = 0;
			}
		}
	}

	/*************************************************************
	*��������Ѿ���ȫ����buffer���񣬸�������Ա�ֱ����Ӧ��������
	*�������dram�ķ���ʱ��Ϊ1000ns
	**************************************************************/
	if ((complete_flag == 1) && (new_request->subs == NULL))
	{
		new_request->cmplt_flag = 1;
		new_request->begin_time = ssd->current_time;
		new_request->response_time = ssd->current_time + 1000;
	}

	return ssd;
}



struct ssd_info * getout2buffer(struct ssd_info *ssd, struct sub_request *sub, struct request *req)
{
	unsigned int req_write_counts = 0;
	struct buffer_group *pt;
	unsigned int sub_req_state = 0, sub_req_size = 0, sub_req_lpn = 0, sub_req_type = 0;
	struct sub_request *sub_req = NULL;


	//ĩβ�ڵ��滻
	pt = ssd->dram->buffer->buffer_tail;
	if (pt->page_type == 0 && pt->LRU_link_pre->page_type == 0)
	{
		//��������full page,��ɾ���������ڵ�
		for (req_write_counts = 0; req_write_counts < 2; req_write_counts++)
		{
			//printf("write\n");
			ssd->dram->buffer->write_miss_hit++;
			pt = ssd->dram->buffer->buffer_tail;

			sub_req_state = pt->stored;
			sub_req_size = size(pt->stored);
			sub_req_lpn = pt->group;
			sub_req_type = pt->page_type;
			sub_req = NULL;
			sub_req = creat_sub_request(ssd, sub_req_lpn, sub_req_size, sub_req_state, sub_req_type, req, WRITE);

			//ɾ���ڵ�
			ssd->dram->buffer->buffer_sector_count = ssd->dram->buffer->buffer_sector_count - ssd->parameter->subpage_page;

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

		//ȡ������buff
		ssd->buffer_full_flag = 0;


	}
	else if (pt->page_type == 1)
	{
		//����һ��pre read
		//printf("update read\n");
		//getchar();
		sub_req_state = pt->stored;
		sub_req_size = size(pt->stored);
		sub_req_lpn = pt->group;
		sub_req_type = pt->page_type;
		sub_req = NULL;
		sub_req = creat_sub_request(ssd, sub_req_lpn, sub_req_size, sub_req_state, sub_req_type, req, UPDATE_READ);
	}
	else if (pt->LRU_link_pre->page_type == 1)
	{
		//����һ��pre read
		//printf("update read\n");
		//getchar();
		sub_req_state = pt->LRU_link_pre->stored;
		sub_req_size = size(pt->LRU_link_pre->stored);
		sub_req_lpn = pt->LRU_link_pre->group;
		sub_req_type = pt->LRU_link_pre->page_type;
		sub_req = NULL;
		sub_req = creat_sub_request(ssd, sub_req_lpn, sub_req_size, sub_req_state, sub_req_type, req, UPDATE_READ);
	}
	else
	{
		//��������pre read
		//printf("update read\n");
		//getchar();
		for (req_write_counts = 0; req_write_counts < 2; req_write_counts++)
		{
			sub_req_state = pt->stored;
			sub_req_size = size(pt->stored);
			sub_req_lpn = pt->group;
			sub_req_type = pt->page_type;
			sub_req = NULL;
			sub_req = creat_sub_request(ssd, sub_req_lpn, sub_req_size, sub_req_state, sub_req_type, req, UPDATE_READ);

			pt = pt->LRU_link_pre;
		}
	}
	return ssd;
}


/*******************************************************************************
*insert2buffer���������ר��Ϊд�������������������buffer_management�б����á�
********************************************************************************/
struct ssd_info * insert2buffer(struct ssd_info *ssd, unsigned int lpn, int state,unsigned int page_type, struct sub_request *sub, struct request *req )
{
	int write_back_count, flag = 0;                                                             /*flag��ʾΪд���������ڿռ��Ƿ���ɣ�0��ʾ��Ҫ��һ���ڣ�1��ʾ�Ѿ��ڿ�*/
	unsigned int i, lsn, add_flag, hit_flag, sector_count, active_region_flag = 0;
	unsigned int free_sector;
	unsigned int page_size=0;
	struct buffer_group *buffer_node = NULL, *new_node = NULL, key;
	struct sub_request *sub_req = NULL, *update = NULL;
	unsigned int req_write_counts = 0;
	unsigned int full_page_flag = 0;
	unsigned int page_flag = 0;


#ifdef DEBUG
	printf("enter insert2buffer,  current time:%I64u, lpn:%d, state:%d,\n", ssd->current_time, lpn, state);
#endif

	sector_count = size(state);                                                                /*��Ҫд��buffer��sector����*/
	key.group = lpn;
	buffer_node = (struct buffer_group*)avlTreeFind(ssd->dram->buffer, (TREE_NODE *)&key);    /*��ƽ���������Ѱ��buffer node*/
	page_size = ssd->parameter->subpage_page;
	flag = 0;

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
		if (free_sector >= page_size)
		{
			flag = 1;		//��ֱ����ӽ�buff����Ϊ�������Ѿ����
			//ssd->dram->buffer->write_hit++;
		}
		if (flag == 0)      //û�����У����Ҵ�ʱbuff��ʣ�������ռ䲻���������滻���滻lpn��������ȥ��
		{
			printf("Error! insert2buff error,no free space\n");
			getchar();
		}

			
		//û�����У�����lpnд�뵽buff��
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
		new_node->page_type = page_type;

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

		ssd->dram->buffer->buffer_sector_count += page_size;

	}
	/****************************************************************************************
	*��buffer�����е����,��ȫ������ֱ�ӷ��أ��������У��ϲ������󷵻�
	*****************************************************************************************/
	else
	{
		if (state == buffer_node->stored)
		{
			ssd->dram->buffer->write_hit++;
		}
		else
		{
			buffer_node->stored = buffer_node->stored | state;
			buffer_node->dirty_clean = buffer_node->dirty_clean | state;

			//�ϲ����жϴ�ʱbuff���ڵĽڵ�page����
			if ((buffer_node->stored&ssd->dram->map->map_entry[lpn].state) != ssd->dram->map->map_entry[lpn].state)
				buffer_node->page_type = 1;
			else
				buffer_node->page_type = 0;

			ssd->dram->buffer->write_hit++;
		}
		//������lpn�����ýڵ��ƶ���ͷ��
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
		req->complete_lsn_count++;
	}
	//printf("bufff_count,%d\n",avlTreeCount(ssd->dram->buffer));
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

	//req = ssd->request_tail;
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
			//�������һЩ��������Ҫ����
			//if (req->complete_lsn_count != ssd->request_tail->size)
			if (req->complete_lsn_count != ssd->request_work->size)
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
								printf("normal read\n");
								sub = creat_sub_request(ssd, lpn, sub_size, 0, 0, req, req->operation);
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
	req->cmplt_flag = 1;   //������ִ����
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
			sub = creat_sub_request(ssd, lpn, sub_size, sub_state,0, req, req->operation);
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

			sub = creat_sub_request(ssd, lpn, sub_size, state,0, req, req->operation);
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
struct sub_request * creat_sub_request(struct ssd_info * ssd, unsigned int lpn, int page_size, unsigned int state,unsigned int page_type, struct request *req, unsigned int operation)
{
	struct sub_request* sub = NULL, *sub_r = NULL;
	struct channel_info * p_ch = NULL;
	struct local * loc = NULL;
	unsigned int flag = 0;
	struct sub_request * update = NULL;
	struct local *location = NULL;

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


	//�Ѹ���������ڵ�ǰ��������
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
		sub->size = page_size;                                                               /*��Ҫ�������������������С*/

		p_ch = &ssd->channel_head[loc->channel];
		sub->ppn = ssd->dram->map->map_entry[lpn].pn;
		sub->operation = READ;
		sub->state = (ssd->dram->map->map_entry[lpn].state & 0x7fffffff);
		sub->update_read_flag = 0;

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
		//_CrtSetBreakAlloc(33673);
		sub->location = (struct local *)malloc(sizeof(struct local));
		alloc_assert(sub->location, "sub->location");
		memset(sub->location, 0, sizeof(struct local));

		sub->current_state = SR_WAIT;
		sub->current_time = ssd->current_time;
		sub->lpn = lpn;
		sub->size = page_size;
		sub->state = state;
		sub->begin_time = ssd->current_time;
		sub->update_read_flag = 0;

		//д����Ķ�̬����

		if (allocate_location(ssd, sub) == ERROR)
		{
			free(sub->location);
			sub->location = NULL;
			free(sub);
			sub = NULL;
			printf("allocate_location error \n");
			return NULL;
		}
	}
	else if (operation == UPDATE_READ)
	{
		//partial page��Ҫ�������¶�
		ssd->update_read_count++;

		/*
		update = (struct sub_request *)malloc(sizeof(struct sub_request));
		alloc_assert(update, "update");
		memset(update, 0, sizeof(struct sub_request));
		*/

		if (sub == NULL)
		{
			return NULL;
		}

		location = find_location(ssd, ssd->dram->map->map_entry[lpn].pn);
		sub->location = location;
		sub->begin_time = ssd->current_time;
		sub->current_state = SR_WAIT;
		sub->current_time = 0x7fffffffffffffff;
		sub->next_state = SR_R_C_A_TRANSFER;
		sub->next_state_predict_time = 0x7fffffffffffffff;
		sub->lpn = lpn;
		sub->state = ((ssd->dram->map->map_entry[lpn].state^state) & 0x7fffffff);
		sub->size = size(sub->state);
		sub->ppn = ssd->dram->map->map_entry[lpn].pn;
		sub->operation = READ;
		sub->update_read_flag = page_type;

		if (sub->update_read_flag == 0)
		{
			printf("not in partial page!\n");
			getchar();
		}

		if (ssd->channel_head[location->channel].subs_r_tail != NULL)            /*�����µĶ����󣬲��ҹҵ�channel��subs_r_tail����β*/
		{
			ssd->channel_head[location->channel].subs_r_tail->next_node = sub;
			ssd->channel_head[location->channel].subs_r_tail = sub;
		}
		else
		{
			ssd->channel_head[location->channel].subs_r_tail = sub;
			ssd->channel_head[location->channel].subs_r_head = sub;
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
		/***************************************
		*һ���Ƕ�̬����ļ������
		*0��ȫ��̬����
		*1����ʾchannel��package��die��plane��̬
		****************************************/
		//ֻ����ȫ��̬����
		if (ssd->parameter->dynamic_allocation == 0)
		{

			sub_req->location->channel = -1;
			sub_req->location->chip = -1;
			sub_req->location->die = -1;
			sub_req->location->plane = -1;
			sub_req->location->block = -1;
			sub_req->location->page = -1;        //������ssdinfo�ϣ���Ϊȫ��̬���䣬��֪��������ص��ĸ�channel����

			if (sub_req != NULL)	     //��д����Ϊ��ͨд���󣬹�������ͨд���������
			{
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
			}
			else
			{
				return ERROR;
			}
		}
	}
	return SUCCESS;
}