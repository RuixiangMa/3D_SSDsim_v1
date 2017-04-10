/*****************************************************************************************************************************
This is a project on 3D_SSDsim, based on ssdsim under the framework of the completion of structures, the main function:
1.Support for 3D commands, for example:mutli plane\interleave\copyback\program suspend/Resume..etc
2.Multi - level parallel simulation
3.Clear hierarchical interface
4.4-layer structure

FileName�� ssd.c
Author: Zuo Lu 		Version: 1.0	Date:2017/04/06
Description:
flash layer: the original ssdsim this layer is not a specific description, it was their own package to achieve, not completed.

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


/*********************************************************************************************************************
* ��־�� ��2011��7��28���޸�
*�����Ĺ��ܾ���erase_operation������������channel��chip��die��plane�µ�block������
*Ҳ���ǳ�ʼ�����block����ز�����eg��free_page_num=page_block��invalid_page_num=0��last_write_page=-1��erase_count++
*�������block�����ÿ��page����ز���ҲҪ�޸ġ�
*********************************************************************************************************************/

Status erase_operation(struct ssd_info * ssd, unsigned int channel, unsigned int chip, unsigned int die, unsigned int plane, unsigned int block)
{
	unsigned int i = 0;
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].free_page_num = ssd->parameter->page_block;
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].invalid_page_num = 0;
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].last_write_page = -1;
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].erase_count++;

	for (i = 0; i<ssd->parameter->page_block; i++)
	{
		ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[i].free_state = PG_SUB;
		ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[i].valid_state = 0;
		ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[i].lpn = -1;
	}
	ssd->erase_count++;
	ssd->channel_head[channel].erase_count++;
	ssd->channel_head[channel].chip_head[chip].erase_count++;
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].free_page += ssd->parameter->page_block;

	return SUCCESS;

}




Status move_page(struct ssd_info * ssd, struct local *location, unsigned int * transfer_size)
{
	struct local *new_location = NULL;
	unsigned int free_state = 0, valid_state = 0;
	unsigned int lpn = 0, old_ppn = 0, ppn = 0;

	lpn = ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].lpn;
	valid_state = ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].valid_state;
	free_state = ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].free_state;
	old_ppn = find_ppn(ssd, location->channel, location->chip, location->die, location->plane, location->block, location->page);      /*��¼�����Ч�ƶ�ҳ��ppn���Ա�map���߶���ӳ���ϵ�е�ppn������ɾ������Ӳ���*/
	ppn = get_ppn_for_gc(ssd, location->channel, location->chip, location->die, location->plane);                /*�ҳ�����ppnһ�����ڷ���gc������plane��,����ʹ��copyback������Ϊgc������ȡppn*/

	new_location = find_location(ssd, ppn);                                                                   /*�����»�õ�ppn��ȡnew_location*/

	(*transfer_size) += size(valid_state);

	ssd->channel_head[new_location->channel].chip_head[new_location->chip].die_head[new_location->die].plane_head[new_location->plane].blk_head[new_location->block].page_head[new_location->page].free_state = free_state;
	ssd->channel_head[new_location->channel].chip_head[new_location->chip].die_head[new_location->die].plane_head[new_location->plane].blk_head[new_location->block].page_head[new_location->page].lpn = lpn;
	ssd->channel_head[new_location->channel].chip_head[new_location->chip].die_head[new_location->die].plane_head[new_location->plane].blk_head[new_location->block].page_head[new_location->page].valid_state = valid_state;


	ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].free_state = 0;
	ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].lpn = 0;
	ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].valid_state = 0;
	ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].invalid_page_num++;

	if (old_ppn == ssd->dram->map->map_entry[lpn].pn)                                                     /*�޸�ӳ���*/
	{
		ssd->dram->map->map_entry[lpn].pn = ppn;
	}

	free(new_location);
	new_location = NULL;

	return SUCCESS;
}


/*****************
*��̬д������ʵ��
******************/
Status static_write(struct ssd_info * ssd, unsigned int channel, unsigned int chip, unsigned int die, struct sub_request * sub)
{
	long long time = 0;
	if (ssd->dram->map->map_entry[sub->lpn].state != 0)                                    /*˵������߼�ҳ֮ǰ��д������Ҫʹ���ȶ���������д��ȥ������ֱ��д��ȥ����*/
	{
		if ((sub->state&ssd->dram->map->map_entry[sub->lpn].state) == ssd->dram->map->map_entry[sub->lpn].state)   /*���Ը���*/
		{
			sub->next_state_predict_time = ssd->current_time + 7 * ssd->parameter->time_characteristics.tWC + (sub->size*ssd->parameter->subpage_capacity)*ssd->parameter->time_characteristics.tWC;
		}
		else
		{
			sub->next_state_predict_time = ssd->current_time + 7 * ssd->parameter->time_characteristics.tWC + ssd->parameter->time_characteristics.tR + (size((ssd->dram->map->map_entry[sub->lpn].state^sub->state)))*ssd->parameter->time_characteristics.tRC + (sub->size*ssd->parameter->subpage_capacity)*ssd->parameter->time_characteristics.tWC;
			ssd->read_count++;
			ssd->update_read_count++;
		}
	}
	else
	{
		sub->next_state_predict_time = ssd->current_time + 7 * ssd->parameter->time_characteristics.tWC + (sub->size*ssd->parameter->subpage_capacity)*ssd->parameter->time_characteristics.tWC;
	}
	sub->complete_time = sub->next_state_predict_time;
	time = sub->complete_time;

	get_ppn(ssd, sub->location->channel, sub->location->chip, sub->location->die, sub->location->plane, sub);

	ssd->channel_head[channel].current_state = CHANNEL_TRANSFER;
	ssd->channel_head[channel].current_time = ssd->current_time;
	ssd->channel_head[channel].next_state = CHANNEL_IDLE;
	ssd->channel_head[channel].next_state_predict_time = time;

	ssd->channel_head[channel].chip_head[chip].current_state = CHIP_WRITE_BUSY;
	ssd->channel_head[channel].chip_head[chip].current_time = ssd->current_time;
	ssd->channel_head[channel].chip_head[chip].next_state = CHIP_IDLE;
	ssd->channel_head[channel].chip_head[chip].next_state_predict_time = time + ssd->parameter->time_characteristics.tPROG;

	return SUCCESS;
}


/*************************************************
*��������Ĺ��ܾ���һ��ģ��һ��ʵʵ���ڵ�д����
*���Ǹ������page����ز������Լ�����ssd��ͳ�Ʋ���
**************************************************/
Status write_page(struct ssd_info *ssd, unsigned int channel, unsigned int chip, unsigned int die, unsigned int plane, unsigned int active_block, unsigned int *ppn)
{
	int last_write_page = 0;
	last_write_page = ++(ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].last_write_page);
	if (last_write_page >= (int)(ssd->parameter->page_block))
	{
		ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].last_write_page = 0;
		printf("error! the last write page larger than 64!!\n");
		return ERROR;
	}

	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].free_page_num--;
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].free_page--;
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].page_head[last_write_page].written_count++;
	ssd->write_flash_count++;
	*ppn = find_ppn(ssd, channel, chip, die, plane, active_block, last_write_page);
	
	return SUCCESS;
}

/*
*�����Ĺ������޸��ҵ���pageҳ��״̬�Լ���Ӧ��dram��ӳ����ֵ
*/
struct ssd_info *flash_page_state_modify(struct ssd_info *ssd, struct sub_request *sub, unsigned int channel, unsigned int chip, unsigned int die, unsigned int plane, unsigned int block, unsigned int page)
{
	unsigned int ppn, full_page;
	struct local *location;
	struct direct_erase *new_direct_erase, *direct_erase_node;

	full_page = ~(0xffffffff << ssd->parameter->subpage_page);
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].last_write_page = page;
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].free_page_num--;

	if (ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].last_write_page>63)
	{
		printf("error! the last write page larger than 64!!\n");
		while (1){}
	}

	if (ssd->dram->map->map_entry[sub->lpn].state == 0)                                          /*this is the first logical page*/
	{
		ssd->dram->map->map_entry[sub->lpn].pn = find_ppn(ssd, channel, chip, die, plane, block, page);
		ssd->dram->map->map_entry[sub->lpn].state = sub->state;
	}
	else                                                                                      /*����߼�ҳ�����˸��£���Ҫ��ԭ����ҳ��ΪʧЧ*/
	{
		ppn = ssd->dram->map->map_entry[sub->lpn].pn;
		location = find_location(ssd, ppn);
		ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].valid_state = 0;        //��ʾĳһҳʧЧ��ͬʱ���valid��free״̬��Ϊ0
		ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].free_state = 0;         //��ʾĳһҳʧЧ��ͬʱ���valid��free״̬��Ϊ0
		ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].lpn = 0;
		ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].invalid_page_num++;
		if (ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].invalid_page_num == ssd->parameter->page_block)    //��block��ȫ��invalid��ҳ������ֱ��ɾ��
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
		ssd->dram->map->map_entry[sub->lpn].pn = find_ppn(ssd, channel, chip, die, plane, block, page);
		ssd->dram->map->map_entry[sub->lpn].state = (ssd->dram->map->map_entry[sub->lpn].state | sub->state);
	}

	sub->ppn = ssd->dram->map->map_entry[sub->lpn].pn;
	sub->location->channel = channel;
	sub->location->chip = chip;
	sub->location->die = die;
	sub->location->plane = plane;
	sub->location->block = block;
	sub->location->page = page;

	ssd->program_count++;
	ssd->channel_head[channel].program_count++;
	ssd->channel_head[channel].chip_head[chip].program_count++;
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].free_page--;
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[page].lpn = sub->lpn;
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[page].valid_state = sub->state;
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[page].free_state = ((~(sub->state))&full_page);
	ssd->write_flash_count++;

	return ssd;
}