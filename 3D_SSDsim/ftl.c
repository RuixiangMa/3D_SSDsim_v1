/*****************************************************************************************************************************
This is a project on 3D_SSDsim, based on ssdsim under the framework of the completion of structures, the main function:
1.Support for 3D commands, for example:mutli plane\interleave\copyback\program suspend/Resume..etc
2.Multi - level parallel simulation
3.Clear hierarchical interface
4.4-layer structure

FileName�� ssd.c
Author: Zuo Lu 		Version: 1.0	Date:2017/04/06
Description: 
ftl layer: can not interrupt the global gc operation, gc operation to migrate valid pages using ordinary read and write operations, remove support copyback operation;

History:
<contributor>     <time>        <version>       <desc>                   <e-mail>
Zuo Lu	        2017/04/06	      1.0		    Creat 3D_SSDsim       617376665@qq.com

*****************************************************************************************************************************/

#define _CRTDBG_MAP_ALLOC

#include <stdlib.h>
#include <crtdbg.h>

#include "initialize.h"
#include "ssd.h"
#include "flash.h"
#include "buffer.h"
#include "interface.h"
#include "ftl.h"
#include "fcl.h"

extern int plane_cmplt;

/******************************************************************************************������ftl��map����******************************************************************************************/

/**************************************************
*������Ԥ����������������������ҳ����û������ʱ��
*��ҪԤ��������ҳ����д���ݣ��Ա�֤�ܶ�������
***************************************************/
struct ssd_info *pre_process_page(struct ssd_info *ssd)
{
	int fl = 0;
	unsigned int device, lsn, size, ope, lpn, full_page;
	unsigned int largest_lsn, sub_size, ppn, add_size = 0;
	unsigned int i = 0, j, k, p;
	int map_entry_new, map_entry_old, modify;
	int flag = 0;
	char buffer_request[200];
	struct local *location;
	long double time0;
	//unsigned long time1;
	__int64 time;
	errno_t err;


	printf("\n");
	printf("begin pre_process_page.................\n");

	if ((err = fopen_s(&(ssd->tracefile), ssd->tracefilename, "r")) != 0)      /*��trace�ļ����ж�ȡ����*/
	{
		printf("the trace file can't open\n");
		return NULL;
	}

	full_page = ~(0xffffffff << (ssd->parameter->subpage_page));
	/*��������ssd������߼�������*/
	largest_lsn = (unsigned int)((ssd->parameter->chip_num*ssd->parameter->die_chip*ssd->parameter->plane_die*ssd->parameter->block_plane*ssd->parameter->page_block*ssd->parameter->subpage_page)*(1 - ssd->parameter->overprovide));

	while (fgets(buffer_request, 200, ssd->tracefile))
	{
		//sscanf_s(buffer_request,"%Lf %d %d %d %d",&time0,&device,&lsn,&size,&ope);
		//time_count++;
		//printf("count=%d\n", time_count);
		//time1 = (unsigned long)(time0 * 100000.0);
		//time = (__int64)(time0 * 1000000.0);

		sscanf_s(buffer_request, "%I64u %d %d %d %d", &time, &device, &lsn, &size, &ope);
		fl++;
		trace_assert(time, device, lsn, size, ope);                         /*���ԣ���������time��device��lsn��size��ope���Ϸ�ʱ�ͻᴦ��*/

		add_size = 0;                                                     /*add_size����������Ѿ�Ԥ����Ĵ�С*/

		if (ope == 1)                                                      /*����ֻ�Ƕ������Ԥ������Ҫ��ǰ����Ӧλ�õ���Ϣ������Ӧ�޸�*/
		{
			while (add_size<size)
			{
				lsn = lsn%largest_lsn;                                    /*��ֹ��õ�lsn������lsn����*/
				sub_size = ssd->parameter->subpage_page - (lsn%ssd->parameter->subpage_page);
				if (add_size + sub_size >= size)                             /*ֻ�е�һ������Ĵ�СС��һ��page�Ĵ�Сʱ�����Ǵ���һ����������һ��pageʱ������������*/
				{
					sub_size = size - add_size;
					add_size += sub_size;
				}

				if ((sub_size>ssd->parameter->subpage_page) || (add_size>size))/*��Ԥ����һ���Ӵ�Сʱ�������С����һ��page�����Ѿ�����Ĵ�С����size�ͱ���*/
				{
					printf("pre_process sub_size:%d\n", sub_size);
				}

				/*******************************************************************************************************
				*�����߼�������lsn������߼�ҳ��lpn
				*�ж����dram��ӳ���map����lpnλ�õ�״̬
				*A�����״̬==0����ʾ��ǰû��д����������Ҫֱ�ӽ�ub_size��С����ҳд��ȥд��ȥ
				*B�����״̬>0����ʾ����ǰ��д��������Ҫ��һ���Ƚ�״̬����Ϊ��д��״̬��������ǰ��״̬���ص��������ĵط�
				********************************************************************************************************/
				lpn = lsn / ssd->parameter->subpage_page;
				if (ssd->dram->map->map_entry[lpn].state == 0)                 /*״̬Ϊ0�����*/
				{
					/**************************************************************
					*�������get_ppn_for_pre_process�������ppn���ٵõ�location
					*�޸�ssd����ز�����dram��ӳ���map���Լ�location�µ�page��״̬
					***************************************************************/
					ppn = get_ppn_for_pre_process(ssd, lsn);
					location = find_location(ssd, ppn);

					//ssd->program_count++;
					//ssd->channel_head[location->channel].program_count++;
					//ssd->channel_head[location->channel].chip_head[location->chip].program_count++;
					ssd->pre_all_write++;
					ssd->dram->map->map_entry[lpn].pn = ppn;
					ssd->dram->map->map_entry[lpn].state = set_entry_state(ssd, lsn, sub_size);   //0001
					ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].pre_write_count++;
					ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].lpn = lpn;
					ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].valid_state = ssd->dram->map->map_entry[lpn].state;
					ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].free_state = ((~ssd->dram->map->map_entry[lpn].state)&full_page);
					
					free(location);
					location = NULL;
				}//if(ssd->dram->map->map_entry[lpn].state==0)
				else if (ssd->dram->map->map_entry[lpn].state>0)           /*״̬��Ϊ0�����*/
				{
					map_entry_new = set_entry_state(ssd, lsn, sub_size);      /*�õ��µ�״̬������ԭ����״̬���ĵ�һ��״̬*/
					map_entry_old = ssd->dram->map->map_entry[lpn].state;
					modify = map_entry_new | map_entry_old;
					ppn = ssd->dram->map->map_entry[lpn].pn;				/*����û�б�Ҫ�����µ�״̬ˢ��������ȥ����Ϊ��ʱ���Ѿ����������ˣ��������¼��״̬���µļ���*/
					location = find_location(ssd, ppn);

					//ssd->program_count++;
					//ssd->channel_head[location->channel].program_count++;
					//ssd->channel_head[location->channel].chip_head[location->chip].program_count++;
					//ssd->pre_all_write++;
					ssd->dram->map->map_entry[lsn / ssd->parameter->subpage_page].state = modify;
					//ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].pre_write_count++;
					ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].valid_state = modify;
					ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].free_state = ((~modify)&full_page);

					free(location);
					location = NULL;
				}//else if(ssd->dram->map->map_entry[lpn].state>0)
				lsn = lsn + sub_size;                                         /*�¸����������ʼλ��*/
				add_size += sub_size;                                       /*�Ѿ������˵�add_size��С�仯*/
			}//while(add_size<size)
		}//if(ope==1) 
	}

	printf("\n");
	printf("pre_process is complete!\n");

	fclose(ssd->tracefile);

	for (i = 0; i < ssd->parameter->channel_number; i++)
		for (p = 0; p < ssd->parameter->chip_channel[i]; p++)
			for (j = 0; j<ssd->parameter->die_chip; j++)
				for (k = 0; k<ssd->parameter->plane_die; k++)
				{
		fprintf(ssd->outputfile, "chip:%d,die:%d,plane:%d have free page: %d\n", p, j, k, ssd->channel_head[i].chip_head[p].die_head[j].plane_head[k].free_page);
		fflush(ssd->outputfile);
				}

	return ssd;
}


/********************************
*���������ǻ��һ�����������״̬
*********************************/
int set_entry_state(struct ssd_info *ssd, unsigned int lsn, unsigned int size)
{
	int temp, state, move;

	temp = ~(0xffffffff << size);
	move = lsn%ssd->parameter->subpage_page;
	state = temp << move;

	return state;
}

/**************************************
*����������ΪԤ��������ȡ����ҳ��ppn
*��ȡҳ�ŷ�Ϊ��̬��ȡ�;�̬��ȡ
**************************************/
unsigned int get_ppn_for_pre_process(struct ssd_info *ssd, unsigned int lsn)
{
	unsigned int channel = 0, chip = 0, die = 0, plane = 0;
	unsigned int ppn, lpn;
	unsigned int active_block;
	unsigned int channel_num = 0, chip_num = 0, die_num = 0, plane_num = 0;

#ifdef DEBUG
	printf("enter get_psn_for_pre_process\n");
#endif

	channel_num = ssd->parameter->channel_number;
	chip_num = ssd->parameter->chip_channel[0];
	die_num = ssd->parameter->die_chip;
	plane_num = ssd->parameter->plane_die;
	lpn = lsn / ssd->parameter->subpage_page;

	if (ssd->parameter->allocation_scheme == 0)                           /*��̬��ʽ�»�ȡppn*/
	{
		//��������ȼ���channel>die>plane
		/*
		if (ssd->parameter->dynamic_allocation == 0)                      //*��ʾȫ��̬��ʽ�£�Ҳ����channel��chip��die��plane��block�ȶ��Ƕ�̬����
		{
			channel = ssd->token;
			ssd->token = (ssd->token + 1) % ssd->parameter->channel_number;
			chip = ssd->channel_head[channel].token;
			ssd->channel_head[channel].token = (chip + 1) % ssd->parameter->chip_channel[0];
			die = ssd->channel_head[channel].chip_head[chip].token;
			ssd->channel_head[channel].chip_head[chip].token = (die + 1) % ssd->parameter->die_chip;
			plane = ssd->channel_head[channel].chip_head[chip].die_head[die].token;
			ssd->channel_head[channel].chip_head[chip].die_head[die].token = (plane + 1) % ssd->parameter->plane_die;
		}
		*/
		
		//�����޸����ȼ���plane>channel>die
		if (ssd->parameter->dynamic_allocation == 0)                      /*��ʾȫ��̬��ʽ�£�Ҳ����channel��chip��die��plane��block�ȶ��Ƕ�̬����*/
		{
			channel = ssd->token;
			chip = ssd->channel_head[channel].token;
			die = ssd->channel_head[channel].chip_head[chip].token;
			plane = ssd->channel_head[channel].chip_head[chip].die_head[die].token;
			ssd->channel_head[channel].chip_head[chip].die_head[die].token = (plane + 1) % ssd->parameter->plane_die;     //�ȴ������е�plane����ô��Ϊ�˱�֤mutli plane��ʵ����

			if (plane == (ssd->parameter->plane_die - 1))
			{
				ssd->token = (ssd->token + 1) % ssd->parameter->channel_number;											   //plane������ɺ󣬴���channel
				
				if (ssd->token == 0)																					   //1-0������channel������ɣ��¸�������Ҫ��die																				
				{
					//channel = ssd->token;
					//chip = ssd->channel_head[channel].token;
					ssd->channel_head[ssd->token].chip_head[ssd->channel_head[channel].token].token = (die + 1) % ssd->parameter->die_chip;  //����die
				}
				else																									 //0--1��channelδ������ɣ����������channel����ʱ���ı�die
				{
					ssd->channel_head[ssd->token].chip_head[ssd->channel_head[channel].token].token = die;      //������die
				}	
			}
		}
	}

	/******************************************************************************
	*�����������䷽���ҵ�channel��chip��die��plane��������������ҵ�active_block
	*���Ż��ppn
	******************************************************************************/

	
	if (find_active_block(ssd, channel, chip, die, plane) == FAILURE)
	{
		//�������⣬����Ԥ������ͬһ��plane�в�ͬ��д��
		printf("the read operation is expand the capacity of SSD");
		getchar();
		return 0;
	}
	active_block = ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].active_block;
	if (write_page(ssd, channel, chip, die, plane, active_block, &ppn) == ERROR)
	{
		return 0;
	}
	

	return ppn;
}


/***************************************************************************************************
*������������������channel��chip��die��plane�����ҵ�һ��active_blockȻ���������block�����ҵ�һ��ҳ��
*������find_ppn�ҵ�ppn��
****************************************************************************************************/
struct ssd_info *get_ppn(struct ssd_info *ssd, unsigned int channel, unsigned int chip, unsigned int die, unsigned int plane, struct sub_request *sub)
{
	int old_ppn = -1;
	unsigned int ppn, lpn, full_page;
	unsigned int active_block;
	unsigned int block;
	unsigned int page, flag = 0, flag1 = 0;
	unsigned int old_state = 0, state = 0, copy_subpage = 0;
	struct local *location;
	struct direct_erase *direct_erase_node, *new_direct_erase;
	struct gc_operation *gc_node;

	unsigned int i = 0, j = 0, k = 0, l = 0, m = 0, n = 0;

#ifdef DEBUG
	printf("enter get_ppn,channel:%d, chip:%d, die:%d, plane:%d\n", channel, chip, die, plane);
#endif

	full_page = ~(0xffffffff << (ssd->parameter->subpage_page));
	lpn = sub->lpn;

	/*************************************************************************************
	*���ú���find_active_block��channel��chip��die��plane�ҵ���Ծblock
	*�����޸����channel��chip��die��plane��active_block�µ�last_write_page��free_page_num
	**************************************************************************************/
	if (find_active_block(ssd, channel, chip, die, plane) == FAILURE)
	{
		printf("ERROR :there is no free page in channel:%d, chip:%d, die:%d, plane:%d\n", channel, chip, die, plane);
		return ssd;
	}

	active_block = ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].active_block;
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].last_write_page++;
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].free_page_num--;

	if (ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].last_write_page>63)
	{
		printf("error! the last write page larger than 64!!\n");
		while (1){}
	}

	block = active_block;
	page = ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].last_write_page; 


	if (ssd->dram->map->map_entry[lpn].state == 0)                                       /*this is the first logical page*/
	{
		if (ssd->dram->map->map_entry[lpn].pn != 0)
		{
			printf("Error in get_ppn()\n");
			//getchar();
		}
		ssd->dram->map->map_entry[lpn].pn = find_ppn(ssd, channel, chip, die, plane, block, page);
		ssd->dram->map->map_entry[lpn].state = sub->state;
	}
	else                                                                            /*����߼�ҳ�����˸��£���Ҫ��ԭ����ҳ��ΪʧЧ*/
	{
		ppn = ssd->dram->map->map_entry[lpn].pn;
		location = find_location(ssd, ppn);
		if (ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].lpn != lpn)
		{

			printf("\nError in get_ppn()\n");
			//getchar();
		}

		ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].valid_state = 0;             /*��ʾĳһҳʧЧ��ͬʱ���valid��free״̬��Ϊ0*/
		ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].free_state = 0;              /*��ʾĳһҳʧЧ��ͬʱ���valid��free״̬��Ϊ0*/
		ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].lpn = 0;
		ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].invalid_page_num++;

		/*******************************************************************************************
		*��block��ȫ��invalid��ҳ������ֱ��ɾ�������ڴ���һ���ɲ����Ľڵ㣬����location�µ�plane����
		********************************************************************************************/
		if (ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].invalid_page_num == ssd->parameter->page_block)
		{
			new_direct_erase = (struct direct_erase *)malloc(sizeof(struct direct_erase));
			alloc_assert(new_direct_erase, "new_direct_erase");
			memset(new_direct_erase, 0, sizeof(struct direct_erase));

			new_direct_erase->block = location->block;
			new_direct_erase->next_node = NULL;
			direct_erase_node = ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].erase_node;
			if (direct_erase_node == NULL)
			{
				ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].erase_node = new_direct_erase;
			}
			else
			{
				new_direct_erase->next_node = ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].erase_node;
				ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].erase_node = new_direct_erase;
			}
		}

		free(location);
		location = NULL;
		ssd->dram->map->map_entry[lpn].pn = find_ppn(ssd, channel, chip, die, plane, block, page);
		ssd->dram->map->map_entry[lpn].state = (ssd->dram->map->map_entry[lpn].state | sub->state);
	}


	sub->ppn = ssd->dram->map->map_entry[lpn].pn;                                      /*�޸�sub�������ppn��location�ȱ���*/
	sub->location->channel = channel;
	sub->location->chip = chip;
	sub->location->die = die;
	sub->location->plane = plane;
	sub->location->block = active_block;
	sub->location->page = page;

	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].page_write_count++;
	ssd->program_count++;                                                           /*�޸�ssd��program_count,free_page�ȱ���*/
	//ssd->channel_head[channel].program_count++;
	//ssd->channel_head[channel].chip_head[chip].program_count++;
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].free_page--;
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].page_head[page].lpn = lpn;
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].page_head[page].valid_state = sub->state;
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].page_head[page].free_state = ((~(sub->state))&full_page);
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].page_head[page].written_count++;
	ssd->write_flash_count++;

	if (ssd->parameter->active_write == 0)                                            /*���û���������ԣ�ֻ����gc_hard_threshold�������޷��ж�GC����*/
	{                                                                               /*���plane�е�free_page����Ŀ����gc_hard_threshold���趨����ֵ�Ͳ���gc����*/
		if (ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].free_page<(ssd->parameter->page_block*ssd->parameter->block_plane*ssd->parameter->gc_hard_threshold))
		{
			gc_node = (struct gc_operation *)malloc(sizeof(struct gc_operation));
			alloc_assert(gc_node, "gc_node");
			memset(gc_node, 0, sizeof(struct gc_operation));

			gc_node->next_node = NULL;
			gc_node->chip = chip;
			gc_node->die = die;
			gc_node->plane = plane;
			gc_node->block = 0xffffffff;
			gc_node->page = 0;
			gc_node->state = GC_WAIT;
			gc_node->priority = GC_UNINTERRUPT;
			gc_node->next_node = ssd->channel_head[channel].gc_command;
			ssd->channel_head[channel].gc_command = gc_node;
			ssd->gc_request++;
		}
	}

	return ssd;
}


/*****************************************************************************
*��������Ĺ����Ǹ��ݲ���channel��chip��die��plane��block��page���ҵ�������ҳ��
*�����ķ���ֵ�����������ҳ��
******************************************************************************/
unsigned int find_ppn(struct ssd_info * ssd, unsigned int channel, unsigned int chip, unsigned int die, unsigned int plane, unsigned int block, unsigned int page)
{
	unsigned int ppn = 0;
	unsigned int i = 0;
	int page_plane = 0, page_die = 0, page_chip = 0;
	int page_channel[100];                  /*��������ŵ���ÿ��channel��page��Ŀ*/

#ifdef DEBUG
	printf("enter find_psn,channel:%d, chip:%d, die:%d, plane:%d, block:%d, page:%d\n", channel, chip, die, plane, block, page);
#endif

	/*********************************************
	*�����plane��die��chip��channel�е�page����Ŀ
	**********************************************/
	page_plane = ssd->parameter->page_block*ssd->parameter->block_plane;
	page_die = page_plane*ssd->parameter->plane_die;
	page_chip = page_die*ssd->parameter->die_chip;
	while (i<ssd->parameter->channel_number)
	{
		page_channel[i] = ssd->parameter->chip_channel[i] * page_chip;
		i++;
	}

	/****************************************************************************
	*��������ҳ��ppn��ppn��channel��chip��die��plane��block��page��page�������ܺ�
	*****************************************************************************/
	i = 0;
	while (i<channel)
	{
		ppn = ppn + page_channel[i];
		i++;
	}
	ppn = ppn + page_chip*chip + page_die*die + page_plane*plane + block*ssd->parameter->page_block + page;

	return ppn;
}


/************************************************************************************
*�����Ĺ����Ǹ�������ҳ��ppn���Ҹ�����ҳ���ڵ�channel��chip��die��plane��block��page
*�õ���channel��chip��die��plane��block��page���ڽṹlocation�в���Ϊ����ֵ
*************************************************************************************/
struct local *find_location(struct ssd_info *ssd, unsigned int ppn)
{
	struct local *location = NULL;
	unsigned int i = 0;
	int pn, ppn_value = ppn;
	int page_plane = 0, page_die = 0, page_chip = 0, page_channel = 0;

	pn = ppn;

#ifdef DEBUG
	printf("enter find_location\n");
#endif

	location = (struct local *)malloc(sizeof(struct local));
	alloc_assert(location, "location");
	memset(location, 0, sizeof(struct local));

	page_plane = ssd->parameter->page_block*ssd->parameter->block_plane;
	page_die = page_plane*ssd->parameter->plane_die;
	page_chip = page_die*ssd->parameter->die_chip;
	page_channel = page_chip*ssd->parameter->chip_channel[0];

	/*******************************************************************************
	*page_channel��һ��channel��page����Ŀ�� ppn/page_channel�͵õ������ĸ�channel��
	*��ͬ���İ취���Եõ�chip��die��plane��block��page
	********************************************************************************/
	location->channel = ppn / page_channel;
	location->chip = (ppn%page_channel) / page_chip;
	location->die = ((ppn%page_channel) % page_chip) / page_die;
	location->plane = (((ppn%page_channel) % page_chip) % page_die) / page_plane;
	location->block = ((((ppn%page_channel) % page_chip) % page_die) % page_plane) / ssd->parameter->page_block;
	location->page = (((((ppn%page_channel) % page_chip) % page_die) % page_plane) % ssd->parameter->page_block) % ssd->parameter->page_block;

	return location;
}

/****************************************
*ִ��д������ʱ��Ϊ��ͨ��д�������ȡppn
*****************************************/
Status get_ppn_for_normal_command(struct ssd_info * ssd, unsigned int channel, unsigned int chip, struct sub_request * sub)
{
	unsigned int die = 0;
	unsigned int plane = 0;
	if (sub == NULL)
	{
		return ERROR;
	}

	if (ssd->parameter->allocation_scheme == DYNAMIC_ALLOCATION)
	{
		die = ssd->channel_head[channel].chip_head[chip].token;
		plane = ssd->channel_head[channel].chip_head[chip].die_head[die].token;
		get_ppn(ssd, channel, chip, die, plane, sub);

		if (ssd->parameter->dynamic_allocation_priority == 1)				//��̬��������ȼ�
		{
			//������die plane������ֵ
			if (plane == (ssd->parameter->plane_die - 1))
			{
				plane_cmplt = 1;
				//ssd->channel_head[channel].chip_head[chip].token = (die + 1) % ssd->parameter->die_chip;
			}
			else
			{
				plane_cmplt = 0;
				//ssd->channel_head[channel].chip_head[chip].token = die;
			}
			ssd->channel_head[channel].chip_head[chip].die_head[die].token = (plane + 1) % ssd->parameter->plane_die;
		}
		else
		{
			ssd->channel_head[channel].chip_head[chip].die_head[die].token = (plane + 1) % ssd->parameter->plane_die;
			ssd->channel_head[channel].chip_head[chip].token = (die + 1) % ssd->parameter->die_chip;
		}
		compute_serve_time(ssd, channel, chip, die, &sub, 1, NORMAL);
 		return SUCCESS;
	}
}

/************************************************************************************************
*Ϊ�߼������ȡppn
*���ݲ�ͬ����������ͬһ��block��˳��д��Ҫ��ѡȡ���Խ���д������ppn��������ppnȫ����ΪʧЧ��
*��ʹ��two plane����ʱ��Ϊ��Ѱ����ͬˮƽλ�õ�ҳ��������Ҫֱ���ҵ�������ȫ�հ׵Ŀ飬���ʱ��ԭ��
*�Ŀ�û�����ֻ꣬�ܷ����⣬�ȴ��´�ʹ�ã�ͬʱ�޸Ĳ��ҿհ�page�ķ���������ǰ����Ѱ��free���Ϊ��ֻ
*Ҫinvalid block!=64���ɡ�
*except find aim page, we should modify token and decide gc operation
*************************************************************************************************/
Status get_ppn_for_advanced_commands(struct ssd_info *ssd, unsigned int channel, unsigned int chip, struct sub_request * * subs, unsigned int subs_count, unsigned int command)
{
	unsigned int die = 0, plane = 0;
	unsigned int die_token = 0, plane_token = 0;
	struct sub_request * sub = NULL;
	unsigned int i = 0, j = 0, k = 0;
	unsigned int unvalid_subs_count = 0;
	unsigned int valid_subs_count = 0;
	unsigned int interleave_flag = FALSE;
	unsigned int multi_plane_falg = FALSE;
	unsigned int max_subs_num = 0;
	struct sub_request * first_sub_in_chip = NULL;
	struct sub_request * first_sub_in_die = NULL;
	struct sub_request * second_sub_in_die = NULL;
	unsigned int state = SUCCESS;
	unsigned int multi_plane_flag = FALSE;

	max_subs_num = ssd->parameter->die_chip*ssd->parameter->plane_die;

	if (ssd->parameter->allocation_scheme == DYNAMIC_ALLOCATION)                         /*��̬�������*/
	{
		if (command == TWO_PLANE)
		{
			if (subs_count<2)
			{
				return ERROR;
			}
			die = ssd->channel_head[channel].chip_head[chip].token;
			for (j = 0; j<subs_count; j++)
			{
				if (j == 1)
				{
					state = find_level_page(ssd, channel, chip, die, subs[0], subs[1]);        /*Ѱ����subs[0]��ppnλ����ͬ��subs[1]��ִ��TWO_PLANE�߼�����*/
					if (state != SUCCESS)
					{
						get_ppn_for_normal_command(ssd, channel, chip, subs[0]);           /*û�ҵ�����ô�͵���ͨ����������*/
						printf("lz:normal_wr_2\n");
						return FAILURE;
					}
					else
					{
						plane_cmplt = 1;
						valid_subs_count = 2;
					}
				}
				else if (j>1)		//��������������
				{
					state = make_level_page(ssd, subs[0], subs[j]);                         /*Ѱ����subs[0]��ppnλ����ͬ��subs[j]��ִ��TWO_PLANE�߼�����*/
					if (state != SUCCESS)
					{
						for (k = j; k<subs_count; k++)
						{
							subs[k] = NULL;
						}
						subs_count = j;
						break;
					}
					else
					{
						valid_subs_count++;
					}
				}
			}//for(j=0;j<subs_count;j++)
			ssd->m_plane_prog_count++;
			if (ssd->parameter->dynamic_allocation == 0)
			{
				ssd->channel_head[channel].chip_head[chip].token = (die + 1) % ssd->parameter->die_chip;
			}
			compute_serve_time(ssd, channel, chip, die, subs, valid_subs_count, TWO_PLANE);
			printf("lz:mutli_plane_wr_3\n");
			return SUCCESS;
		}//else if(command==TWO_PLANE)
		else
		{
			return ERROR;
		}
	}//if (ssd->parameter->allocation_scheme==DYNAMIC_ALLOCATION)
}



/******************************************************************************************������ftl��gc����******************************************************************************************/
/************************************************************************************************************
*flag�������gc��������ssd��������idle������±����õģ�1��������ȷ����channel��chip��die��plane�����ã�0��
*����gc��������Ҫ�ж��Ƿ��ǲ����жϵ�gc����������ǣ���Ҫ��һ����Ŀ��block��ȫ�����������ɣ�����ǿ��жϵģ�
*�ڽ���GC����ǰ����Ҫ�жϸ�channel��die�Ƿ����������ڵȴ����������û����ʼһ��һ���Ĳ������ҵ�Ŀ��
*���һ��ִ��һ��copyback����������gc��������ʱ����ǰ�ƽ���������һ��copyback����erase����
*����gc������һ����Ҫ����gc��������Ҫ����һ�����жϣ�������Ӳ��ֵ����ʱ���������gc����������������ֵ����ʱ��
*��Ҫ�жϣ������channel���Ƿ����������ڵȴ�(��д������ȴ��Ͳ��У�gc��Ŀ��die����busy״̬Ҳ����)�����
*�оͲ�ִ��gc���������������ִ��һ������
************************************************************************************************************/
unsigned int gc(struct ssd_info *ssd, unsigned int channel, unsigned int flag)
{
	unsigned int i;
	int flag_direct_erase = 1, flag_gc = 1, flag_invoke_gc = 1;
	unsigned int flag_priority = 0;
	struct gc_operation *gc_node = NULL, *gc_p = NULL;

	//����gc
	if (flag == 1)                                                                       /*����ssd����IDEL�����*/
	{
		for (i = 0; i<ssd->parameter->channel_number; i++)
		{
			flag_priority = 0;
			flag_direct_erase = 1;
			flag_gc = 1;
			flag_invoke_gc = 1;
			gc_node = NULL;
			gc_p = NULL;
			if ((ssd->channel_head[i].current_state == CHANNEL_IDLE) || (ssd->channel_head[i].next_state == CHANNEL_IDLE&&ssd->channel_head[i].next_state_predict_time <= ssd->current_time))
			{
				channel = i;
				if (ssd->channel_head[channel].gc_command != NULL)
				{
					gc_for_channel(ssd, channel);
				}
			}
		}
		return SUCCESS;

	}
	//����gc
	else                                                                               /*ֻ�����ĳ���ض���channel��chip��die����gc����Ĳ���(ֻ���Ŀ��die�����ж������ǲ���idle��*/
	{
		//ֻ����ȫ�ַ��䣬�ʾ�̬����ֱ��ȥ��
		if ((ssd->parameter->allocation_scheme == 0) && (ssd->parameter->dynamic_allocation == 0))
			//if ((ssd->parameter->allocation_scheme==1)||((ssd->parameter->allocation_scheme==0)&&(ssd->parameter->dynamic_allocation==1)))
		{
			if ((ssd->channel_head[channel].subs_r_head != NULL) || (ssd->channel_head[channel].subs_w_head != NULL))    /*�������������ȷ�������*/
			{
				return 0;
			}
		}
		gc_for_channel(ssd, channel);
		return SUCCESS;
	}
}


/***************************************
*��������Ĺ����Ǵ���channel��ÿ��gc����
****************************************/
Status gc_for_channel(struct ssd_info *ssd, unsigned int channel)
{
	int flag_direct_erase = 1, flag_gc = 1, flag_invoke_gc = 1;
	unsigned int chip, die, plane, flag_priority = 0;
	unsigned int current_state = 0, next_state = 0;
	long long next_state_predict_time = 0;
	struct gc_operation *gc_node = NULL, *gc_p = NULL;

	/*******************************************************************************************
	*����ÿһ��gc_node����ȡgc_node���ڵ�chip�ĵ�ǰ״̬���¸�״̬���¸�״̬��Ԥ��ʱ��
	*�����ǰ״̬�ǿ��У������¸�״̬�ǿ��ж��¸�״̬��Ԥ��ʱ��С�ڵ�ǰʱ�䣬�����ǲ����жϵ�gc
	*��ô��flag_priority��Ϊ1������Ϊ0
	********************************************************************************************/
	gc_node = ssd->channel_head[channel].gc_command;
	while (gc_node != NULL)
	{
		current_state = ssd->channel_head[channel].chip_head[gc_node->chip].current_state;
		next_state = ssd->channel_head[channel].chip_head[gc_node->chip].next_state;
		next_state_predict_time = ssd->channel_head[channel].chip_head[gc_node->chip].next_state_predict_time;
		if ((current_state == CHIP_IDLE) || ((next_state == CHIP_IDLE) && (next_state_predict_time <= ssd->current_time)))
		{
			if (gc_node->priority == GC_UNINTERRUPT)                                     /*���gc�����ǲ����жϵģ����ȷ������gc����*/
			{
				flag_priority = 1;
				//printf("lz:gc_uninterrupt\n");
				break;
			}
		}
		gc_node = gc_node->next_node;
	}

	if (gc_node == NULL)
	{
		return FAILURE;
	}

	chip = gc_node->chip;
	die = gc_node->die;
	plane = gc_node->plane;

	if (gc_node->priority == GC_UNINTERRUPT)
	{
		flag_direct_erase = gc_direct_erase(ssd, channel, chip, die, plane);
		if (flag_direct_erase != SUCCESS)
		{
			flag_gc = uninterrupt_gc(ssd, channel, chip, die, plane);                         /*��һ��������gc�������ʱ���Ѿ�����һ���飬������һ��������flash�ռ䣩������1����channel����Ӧ��gc��������ڵ�ɾ��*/
			if (flag_gc == 1)
			{
				delete_gc_node(ssd, channel, gc_node);
			}
		}
		else
		{
			delete_gc_node(ssd, channel, gc_node);
		}
		return SUCCESS;
	}
}



/*******************************************************************************************************************
*GC������ĳ��plane��free��������ֵ���д�������ĳ��plane������ʱ��GC����ռ�����plane���ڵ�die����Ϊdie��һ��������Ԫ��
*��һ��die��GC���������������ĸ�planeͬʱerase������interleave erase������GC����Ӧ������������ʱֹͣ���ƶ����ݺͲ���
*ʱ���У����Ǽ�϶ʱ�����ֹͣGC���������Է����µ�������󣬵��������������������϶ʱ�䣬����GC������������������
*GC��ֵ��һ������ֵ��һ��Ӳ��ֵ������ֵ��ʾ�������ֵ�󣬿��Կ�ʼ������GC���������ü�Ъʱ�䣬GC���Ա��µ��������жϣ�
*������Ӳ��ֵ��ǿ����ִ��GC�������Ҵ�GC�������ܱ��жϣ�ֱ���ص�Ӳ��ֵ���ϡ�
*������������棬�ҳ����die���е�plane�У���û�п���ֱ��ɾ����block��Ҫ���еĻ�������interleave two plane���ɾ��
*��Щblock�������ж���plane������ֱ��ɾ����block��ͬʱɾ�������еĻ��������ǵ������plane����ɾ��������Ҳ������Ļ���
*ֱ����������gc_parallelism�������н�һ��GC�������ú���Ѱ��ȫ��Ϊinvalid�Ŀ飬ֱ��ɾ�����ҵ���ֱ��ɾ���ķ���1��û����
*������-1��
*********************************************************************************************************************/
int gc_direct_erase(struct ssd_info *ssd, unsigned int channel, unsigned int chip, unsigned int die, unsigned int plane)
{
	unsigned int lv_die = 0, lv_plane = 0;
	unsigned int interleaver_flag = FALSE, muilt_plane_flag = FALSE;
	unsigned int normal_erase_flag = TRUE;

	struct direct_erase * direct_erase_node1 = NULL;
	struct direct_erase * direct_erase_node2 = NULL;

	direct_erase_node1 = ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].erase_node;
	if (direct_erase_node1 == NULL)
	{
		return FAILURE;
	}

	/********************************************************************************************************
	*���ܴ���TWOPLANE�߼�����ʱ��������Ӧ��channel��chip��die��������ͬ��plane�ҵ�����ִ��TWOPLANE������block
	*����muilt_plane_flagΪTRUE
	*********************************************************************************************************/
	if ((normal_erase_flag == TRUE))                              /*����ÿ��plane���п���ֱ��ɾ����block��ֻ�Ե�ǰplane������ͨ��erase����������ֻ��ִ����ͨ����*/
	{
		if (erase_planes(ssd, channel, chip, die, plane, NORMAL) == SUCCESS)
		{
			return SUCCESS;
		}
		else
		{
			return FAILURE;                                     /*Ŀ���planeû�п���ֱ��ɾ����block����ҪѰ��Ŀ����������ʵʩ��������*/
		}
	}
	return SUCCESS;
}


/*******************************************************************************************************************************************
*Ŀ���planeû�п���ֱ��ɾ����block����ҪѰ��Ŀ����������ʵʩ�������������ڲ����жϵ�gc�����У��ɹ�ɾ��һ���飬����1��û��ɾ��һ���鷵��-1
*����������У����ÿ���Ŀ��channel,die�Ƿ��ǿ��е�,����invalid_page_num����block
********************************************************************************************************************************************/
int uninterrupt_gc(struct ssd_info *ssd, unsigned int channel, unsigned int chip, unsigned int die, unsigned int plane)
{
	unsigned int i = 0, invalid_page = 0;
	unsigned int block, active_block, transfer_size, free_page, page_move_count = 0;                           /*��¼ʧЧҳ���Ŀ��*/
	struct local *  location = NULL;
	unsigned int total_invalid_page_num = 0;

	if (find_active_block(ssd, channel, chip, die, plane) != SUCCESS)                                           /*��ȡ��Ծ��*/
	{
		printf("\n\n Error in uninterrupt_gc().\n");
		return ERROR;
	}

	active_block = ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].active_block;

	invalid_page = 0;
	transfer_size = 0;
	block = -1;
	for (i = 0; i<ssd->parameter->block_plane; i++)                                                           /*�������invalid_page�Ŀ�ţ��Լ�����invalid_page_num*/
	{
		total_invalid_page_num += ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[i].invalid_page_num;
		if ((active_block != i) && (ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[i].invalid_page_num>invalid_page))
		{
			invalid_page = ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[i].invalid_page_num;
			block = i;
		}
	}
	if (block == -1)
	{
		return 1;
	}

	//if(invalid_page<5)
	//{
	//printf("\ntoo less invalid page. \t %d\t %d\t%d\t%d\t%d\t%d\t\n",invalid_page,channel,chip,die,plane,block);
	//}

	free_page = 0;
	for (i = 0; i<ssd->parameter->page_block; i++)		                                                     /*������ÿ��page���������Ч���ݵ�page��Ҫ�ƶ��������ط��洢*/
	{
		if ((ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[i].free_state&PG_SUB) == 0x0000000f)
		{
			free_page++;
		}
		if (free_page != 0)
		{
			printf("\ntoo much free page. \t %d\t .%d\t%d\t%d\t%d\t\n", free_page, channel, chip, die, plane);
		}
		if (ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[i].valid_state>0) /*��ҳ����Чҳ����Ҫcopyback����*/
		{
			location = (struct local *)malloc(sizeof(struct local));
			alloc_assert(location, "location");
			memset(location, 0, sizeof(struct local));

			location->channel = channel;
			location->chip = chip;
			location->die = die;
			location->plane = plane;
			location->block = block;
			location->page = i;
			move_page(ssd, location, &transfer_size);                                                   /*��ʵ��move_page����*/
			page_move_count++;

			free(location);
			location = NULL;
		}
	}
	erase_operation(ssd, channel, chip, die, plane, block);	                                              /*ִ����move_page�����󣬾�����ִ��block�Ĳ�������*/

	ssd->channel_head[channel].current_state = CHANNEL_GC;
	ssd->channel_head[channel].current_time = ssd->current_time;
	ssd->channel_head[channel].next_state = CHANNEL_IDLE;
	ssd->channel_head[channel].chip_head[chip].current_state = CHIP_ERASE_BUSY;
	ssd->channel_head[channel].chip_head[chip].current_time = ssd->current_time;
	ssd->channel_head[channel].chip_head[chip].next_state = CHIP_IDLE;

	/***************************************************************
	*�ڿ�ִ��COPYBACK�߼������벻��ִ��COPYBACK�߼���������������£�
	*channel�¸�״̬ʱ��ļ��㣬�Լ�chip�¸�״̬ʱ��ļ���
	***************************************************************/
	ssd->channel_head[channel].next_state_predict_time = ssd->current_time + page_move_count*(7 * ssd->parameter->time_characteristics.tWC + ssd->parameter->time_characteristics.tR + 7 * ssd->parameter->time_characteristics.tWC + ssd->parameter->time_characteristics.tPROG) + transfer_size*SECTOR*(ssd->parameter->time_characteristics.tWC + ssd->parameter->time_characteristics.tRC);
	ssd->channel_head[channel].chip_head[chip].next_state_predict_time = ssd->channel_head[channel].next_state_predict_time + ssd->parameter->time_characteristics.tBERS;

	return 1;
}

/*****************************************************************************************
*�������������Ϊgc����Ѱ���µ�ppn����Ϊ��gc��������Ҫ�ҵ��µ��������ԭ��������ϵ�����
*��gc��Ѱ���������ĺ�������������ѭ����gc����
******************************************************************************************/
unsigned int get_ppn_for_gc(struct ssd_info *ssd, unsigned int channel, unsigned int chip, unsigned int die, unsigned int plane)
{
	unsigned int ppn;
	unsigned int active_block, block, page;

#ifdef DEBUG
	printf("enter get_ppn_for_gc,channel:%d, chip:%d, die:%d, plane:%d\n", channel, chip, die, plane);
#endif

	if (find_active_block(ssd, channel, chip, die, plane) != SUCCESS)
	{
		printf("\n\n Error int get_ppn_for_gc().\n");
		return 0xffffffff;
	}

	active_block = ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].active_block;

	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].last_write_page++;
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].free_page_num--;

	if (ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].last_write_page>63)
	{
		printf("error! the last write page larger than 64!!\n");
		while (1){}
	}

	block = active_block;
	page = ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].last_write_page;

	ppn = find_ppn(ssd, channel, chip, die, plane, block, page);

	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].page_write_count++;
	ssd->program_count++;
	//ssd->channel_head[channel].program_count++;
	//ssd->channel_head[channel].chip_head[chip].program_count++;
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].free_page--;
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].page_head[page].written_count++;
	ssd->write_flash_count++;

	return ppn;

}
/*************************************************************
*�����Ĺ����ǵ�������һ��gc����ʱ����Ҫ��gc���ϵ�gc_nodeɾ����
**************************************************************/
int delete_gc_node(struct ssd_info *ssd, unsigned int channel, struct gc_operation *gc_node)
{
	struct gc_operation *gc_pre = NULL;
	if (gc_node == NULL)
	{
		return ERROR;
	}

	if (gc_node == ssd->channel_head[channel].gc_command)
	{
		ssd->channel_head[channel].gc_command = gc_node->next_node;
	}
	else
	{
		gc_pre = ssd->channel_head[channel].gc_command;
		while (gc_pre->next_node != NULL)
		{
			if (gc_pre->next_node == gc_node)
			{
				gc_pre->next_node = gc_node->next_node;
				break;
			}
			gc_pre = gc_pre->next_node;
		}
	}
	free(gc_node);
	gc_node = NULL;
	ssd->gc_request--;
	return SUCCESS;
}


/**************************************************************************************
*��������Ĺ����Ǵ���INTERLEAVE_TWO_PLANE��INTERLEAVE��TWO_PLANE��NORMAL�µĲ����Ĳ�����
***************************************************************************************/
Status erase_planes(struct ssd_info * ssd, unsigned int channel, unsigned int chip, unsigned int die1, unsigned int plane1, unsigned int command)
{
	unsigned int die = 0;
	unsigned int plane = 0;
	unsigned int block = 0;
	struct direct_erase *direct_erase_node = NULL;
	unsigned int block0 = 0xffffffff;
	unsigned int block1 = 0;

	//�����Ǹ߼�����
	if ((ssd->channel_head[channel].chip_head[chip].die_head[die1].plane_head[plane1].erase_node == NULL) || (command != NORMAL))
		//if((ssd->channel_head[channel].chip_head[chip].die_head[die1].plane_head[plane1].erase_node==NULL)||((command!=INTERLEAVE_TWO_PLANE)&&(command!=INTERLEAVE)&&(command!=TWO_PLANE)&&(command!=NORMAL)))     /*���û�в�������������command���ԣ����ش���*/           
	{
		return ERROR;
	}

	/************************************************************************************************************
	*�����������ʱ������Ҫ���Ͳ����������channel��chip���ڴ��������״̬����CHANNEL_TRANSFER��CHIP_ERASE_BUSY
	*��һ״̬��CHANNEL_IDLE��CHIP_IDLE��
	*************************************************************************************************************/
	block1 = ssd->channel_head[channel].chip_head[chip].die_head[die1].plane_head[plane1].erase_node->block;

	ssd->channel_head[channel].current_state = CHANNEL_TRANSFER;
	ssd->channel_head[channel].current_time = ssd->current_time;
	ssd->channel_head[channel].next_state = CHANNEL_IDLE;

	ssd->channel_head[channel].chip_head[chip].current_state = CHIP_ERASE_BUSY;
	ssd->channel_head[channel].chip_head[chip].current_time = ssd->current_time;
	ssd->channel_head[channel].chip_head[chip].next_state = CHIP_IDLE;

	if (command == NORMAL)																	/*��ͨ����NORMAL�Ĵ���*/
	{
		direct_erase_node = ssd->channel_head[channel].chip_head[chip].die_head[die1].plane_head[plane1].erase_node;
		block = direct_erase_node->block;
		ssd->channel_head[channel].chip_head[chip].die_head[die1].plane_head[plane1].erase_node = direct_erase_node->next_node;
		free(direct_erase_node);
		direct_erase_node = NULL;
		erase_operation(ssd, channel, chip, die1, plane1, block);

		ssd->direct_erase_count++;
		ssd->channel_head[channel].next_state_predict_time = ssd->current_time + 5 * ssd->parameter->time_characteristics.tWC;
		ssd->channel_head[channel].chip_head[chip].next_state_predict_time = ssd->channel_head[channel].next_state_predict_time + ssd->parameter->time_characteristics.tWB + ssd->parameter->time_characteristics.tBERS;
	}
	else
	{
		return ERROR;
	}

	direct_erase_node = ssd->channel_head[channel].chip_head[chip].die_head[die1].plane_head[plane1].erase_node;

	if (((direct_erase_node) != NULL) && (direct_erase_node->block == block1))
	{
		return FAILURE;
	}
	else
	{
		return SUCCESS;
	}
}

/**************************************************************************************
*�����Ĺ�����Ѱ�һ�Ծ�죬ӦΪÿ��plane�ж�ֻ��һ����Ծ�飬ֻ�������Ծ���в��ܽ��в���
***************************************************************************************/
Status  find_active_block(struct ssd_info *ssd, unsigned int channel, unsigned int chip, unsigned int die, unsigned int plane)
{
	unsigned int active_block = 0;
	unsigned int free_page_num = 0;
	unsigned int count = 0;
	//	int i, j, k, p, t;
	int lz = 0;

	active_block = ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].active_block;
	free_page_num = ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].free_page_num;
	//last_write_page=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].free_page_num;
	while ((free_page_num == 0) && (count<ssd->parameter->block_plane))
	{
		active_block = (active_block + 1) % ssd->parameter->block_plane;
		free_page_num = ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].free_page_num;
		count++;
	}

	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].active_block = active_block;

	if (count<ssd->parameter->block_plane)
	{
		return SUCCESS;
	}
	else
	{
		return FAILURE;
	}
}


