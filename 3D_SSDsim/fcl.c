/*****************************************************************************************************************************
This is a project on 3D_SSDsim, based on ssdsim under the framework of the completion of structures, the main function:
1.Support for 3D commands, for example:mutli plane\interleave\copyback\program suspend/Resume..etc
2.Multi - level parallel simulation
3.Clear hierarchical interface
4.4-layer structure

FileName�� ssd.c
Author: Zuo Lu 		Version: 1.0	Date:2017/04/06
Description:
fcl layer: remove other high-level commands, leaving only mutli plane;

History:
<contributor>     <time>        <version>       <desc>                   <e-mail>
Zuo Lu	        2017/04/06	      1.0		    Creat 3D_SSDsim       617376665@qq.com

*****************************************************************************************************************************/

#define _CRTDBG_MAP_ALLOC

#include <stdlib.h>
#include <crtdbg.h>

#include "flash.h"
#include "ssd.h"
#include "initialize.h"
#include "buffer.h"
#include "interface.h"
#include "ftl.h"
#include "fcl.h"

extern int plane_cmplt;
extern int buffer_full_flag;
/******************************************************
*�����Ĺ������ڸ�����channel��chip��die����Ѱ�Ҷ�������
*����������ppnҪ����Ӧ��plane�ļĴ��������ppn���
*******************************************************/
struct sub_request * find_read_sub_request(struct ssd_info * ssd, unsigned int channel, unsigned int chip, unsigned int die)
{
	unsigned int plane = 0;
	int address_ppn = 0;
	struct sub_request *sub = NULL, *p = NULL;

	for (plane = 0; plane<ssd->parameter->plane_die; plane++)
	{
		address_ppn = ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].add_reg_ppn;
		if (address_ppn != -1)
		{
			sub = ssd->channel_head[channel].subs_r_head;
			if (sub->ppn == address_ppn)
			{
				if (sub->next_node == NULL)
				{
					ssd->channel_head[channel].subs_r_head = NULL;
					ssd->channel_head[channel].subs_r_tail = NULL;
				}
				ssd->channel_head[channel].subs_r_head = sub->next_node;
			}
			while ((sub->ppn != address_ppn) && (sub->next_node != NULL))
			{
				if (sub->next_node->ppn == address_ppn)
				{
					p = sub->next_node;
					if (p->next_node == NULL)
					{
						sub->next_node = NULL;
						ssd->channel_head[channel].subs_r_tail = sub;
					}
					else
					{
						sub->next_node = p->next_node;
					}
					sub = p;
					break;
				}
				sub = sub->next_node;
			}
			if (sub->ppn == address_ppn)
			{
				sub->next_node = NULL;
				return sub;
			}
			else
			{
				printf("Error! Can't find the sub request.");
			}
		}
	}
	return NULL;
}

/*******************************************************************************
*�����Ĺ�����Ѱ��д������
*���������1��Ҫ������ȫ��̬�������ssd->subs_w_head��������
*2��Ҫ�ǲ�����ȫ��̬������ô����ssd->channel_head[channel].subs_w_head�����ϲ���
********************************************************************************/
struct sub_request * find_write_sub_request(struct ssd_info * ssd, unsigned int channel)
{
	struct sub_request * sub = NULL, *p = NULL;
	if ((ssd->parameter->allocation_scheme == 0) && (ssd->parameter->dynamic_allocation == 0))    /*����ȫ�Ķ�̬����*/
	{
		sub = ssd->subs_w_head;
		while (sub != NULL)
		{
			if (sub->current_state == SR_WAIT)
			{
				if (sub->update != NULL)                                                      /*�������Ҫ��ǰ������ҳ*/
				{
					if ((sub->update->current_state == SR_COMPLETE) || ((sub->update->next_state == SR_COMPLETE) && (sub->update->next_state_predict_time <= ssd->current_time)))   //�����µ�ҳ�Ѿ�������
					{
						break;
					}
				}
				else
				{
					break;
				}
			}
			p = sub;
			sub = sub->next_node;
		}

		if (sub == NULL)                                                                      /*���û���ҵ����Է�����������������forѭ��*/
		{
			return NULL;
		}

		if (sub != ssd->subs_w_head)
		{
			if (sub != ssd->subs_w_tail)
			{
				p->next_node = sub->next_node;
			}
			else
			{
				ssd->subs_w_tail = p;
				ssd->subs_w_tail->next_node = NULL;
			}
		}
		else
		{
			if (sub->next_node != NULL)
			{
				ssd->subs_w_head = sub->next_node;
			}
			else
			{
				ssd->subs_w_head = NULL;
				ssd->subs_w_tail = NULL;
			}
		}
		sub->next_node = NULL;
		if (ssd->channel_head[channel].subs_w_tail != NULL)
		{
			ssd->channel_head[channel].subs_w_tail->next_node = sub;
			ssd->channel_head[channel].subs_w_tail = sub;
		}
		else
		{
			ssd->channel_head[channel].subs_w_tail = sub;
			ssd->channel_head[channel].subs_w_head = sub;
		}
	}
	return sub;
}

/*********************************************************************************************
*ר��Ϊ�����������ĺ���
*1��ֻ�е���������ĵ�ǰ״̬��SR_R_C_A_TRANSFER
*2����������ĵ�ǰ״̬��SR_COMPLETE������һ״̬��SR_COMPLETE������һ״̬�����ʱ��ȵ�ǰʱ��С
**********************************************************************************************/
Status services_2_r_cmd_trans_and_complete(struct ssd_info * ssd)
{
	unsigned int i = 0;
	struct sub_request * sub = NULL, *p = NULL;
	

	for (i = 0; i<ssd->parameter->channel_number; i++)                                       /*���ѭ��������Ҫchannel��ʱ��(�������Ѿ�����chip��chip��ready��Ϊbusy)�������������ʱ�������channel�Ķ�����ȡ��*/
	{
		sub = ssd->channel_head[i].subs_r_head;

		while (sub != NULL)
		{
			if (sub->current_state == SR_R_C_A_TRANSFER)                                  /*���������ϣ�����Ӧ��die��Ϊbusy��ͬʱ�޸�sub��״̬; �������ר�Ŵ���������ɵ�ǰ״̬Ϊ�������Ϊdie��ʼbusy��die��ʼbusy����ҪchannelΪ�գ����Ե����г�*/
			{
				if (sub->next_state_predict_time <= ssd->current_time)
				{
					go_one_step(ssd, sub, NULL, SR_R_READ, NORMAL);                      /*״̬���䴦����*/

				}
			}
			else if ((sub->current_state == SR_COMPLETE) || ((sub->next_state == SR_COMPLETE) && (sub->next_state_predict_time <= ssd->current_time)))
			{
				if (sub != ssd->channel_head[i].subs_r_head)                             /*if the request is completed, we delete it from read queue */
				{
					p->next_node = sub->next_node;
				}
				else
				{
					if (ssd->channel_head[i].subs_r_head != ssd->channel_head[i].subs_r_tail)
					{
						ssd->channel_head[i].subs_r_head = sub->next_node;
					}
					else
					{
						ssd->channel_head[i].subs_r_head = NULL;
						ssd->channel_head[i].subs_r_tail = NULL;
					}
				}
			}
			p = sub;
			sub = sub->next_node;
		}
	}

	return SUCCESS;
}

/**************************************************************************
*�������Ҳ��ֻ����������󣬴���chip��ǰ״̬��CHIP_WAIT��
*������һ��״̬��CHIP_DATA_TRANSFER������һ״̬��Ԥ��ʱ��С�ڵ�ǰʱ���chip
***************************************************************************/
Status services_2_r_data_trans(struct ssd_info * ssd, unsigned int channel, unsigned int * channel_busy_flag, unsigned int * change_current_time_flag)
{
	int chip = 0;
	unsigned int die = 0, plane = 0, address_ppn = 0, die1 = 0;
	struct sub_request * sub = NULL, *p = NULL, *sub1 = NULL;
	struct sub_request * sub_twoplane_one = NULL, *sub_twoplane_two = NULL;
	struct sub_request * sub_interleave_one = NULL, *sub_interleave_two = NULL;
	for (chip = 0; chip<ssd->channel_head[channel].chip; chip++)
	{
		if ((ssd->channel_head[channel].chip_head[chip].current_state == CHIP_WAIT) || ((ssd->channel_head[channel].chip_head[chip].next_state == CHIP_DATA_TRANSFER) &&
			(ssd->channel_head[channel].chip_head[chip].next_state_predict_time <= ssd->current_time)))
		{
			for (die = 0; die<ssd->parameter->die_chip; die++)
			{
				sub = find_read_sub_request(ssd, channel, chip, die);                   /*��channel,chip,die���ҵ���������*/
				if (sub != NULL)
				{
					break;
				}
			}
			if (sub == NULL)
			{
				continue;
			} 

			/**************************************************************************************
			*���ssd֧�ָ߼������û���ǿ���һ����֧��AD_TWOPLANE_READ��AD_INTERLEAVE�Ķ�������
			*1���п��ܲ�����two plane����������������£���ͬһ��die�ϵ�����plane���������δ���
			*2���п��ܲ�����interleave����������������£�����ͬdie�ϵ�����plane���������δ���
			***************************************************************************************/
			if ((ssd->parameter->advanced_commands&AD_TWOPLANE_READ) == AD_TWOPLANE_READ)				/*�п��ܲ�����two plane����������������£���ͬһ��die�ϵ�����plane���������δ���*/
			{
				sub_twoplane_one = sub;
				sub_twoplane_two = NULL;
				/*Ϊ�˱�֤�ҵ���sub_twoplane_two��sub_twoplane_one��ͬ����add_reg_ppn=-1*/
				ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[sub->location->plane].add_reg_ppn = -1;
				sub_twoplane_two = find_read_sub_request(ssd, channel, chip, die);               /*����ͬ��channel,chip,die��Ѱ������һ����������*/

				/******************************************************
				*����ҵ�����ô��ִ��TWO_PLANE��״̬ת������go_one_step
				*���û�ҵ���ô��ִ����ͨ�����״̬ת������go_one_step
				******************************************************/
				if (sub_twoplane_two == NULL)
				{
					go_one_step(ssd, sub_twoplane_one, NULL, SR_R_DATA_TRANSFER, NORMAL);
					*change_current_time_flag = 0;
					*channel_busy_flag = 1;

				}
				else
				{
					go_one_step(ssd, sub_twoplane_one, sub_twoplane_two, SR_R_DATA_TRANSFER, TWO_PLANE);
					*change_current_time_flag = 0;
					*channel_busy_flag = 1;

				}
			}
			else                                                                                 /*���ssd��֧�ָ߼�������ô��ִ��һ��һ����ִ�ж�������*/
			{
				printf("r_data_trans:normal command !\n");
				getchar();
				go_one_step(ssd, sub, NULL, SR_R_DATA_TRANSFER, NORMAL);
				*change_current_time_flag = 0;
				*channel_busy_flag = 1;
			}
			break;
		}

		if (*channel_busy_flag == 1)
		{
			break;
		}
	}

	if (*channel_busy_flag == 0)
		//printf("\n");

	return SUCCESS;
}


/******************************************************
*�������Ҳ��ֻ����������󣬲��Ҵ��ڵȴ�״̬�Ķ�������
*******************************************************/
int services_2_r_wait(struct ssd_info * ssd, unsigned int channel, unsigned int * channel_busy_flag, unsigned int * change_current_time_flag)
{
	unsigned int plane = 0, address_ppn = 0;
	struct sub_request * sub = NULL, *p = NULL;
	struct sub_request * sub_twoplane_one = NULL, *sub_twoplane_two = NULL;
	struct sub_request * sub_interleave_one = NULL, *sub_interleave_two = NULL;


	sub = ssd->channel_head[channel].subs_r_head;

	if ((ssd->parameter->advanced_commands&AD_TWOPLANE_READ) == AD_TWOPLANE_READ)         /*to find whether there are two sub request can be served by two plane operation*/
	{
		sub_twoplane_one = NULL;
		sub_twoplane_two = NULL;
		/*Ѱ����ִ��two_plane��������������*/
		find_interleave_twoplane_sub_request(ssd, channel, &sub_twoplane_one, &sub_twoplane_two, TWO_PLANE);

		//find_interleave_twoplane_sub_request(ssd, channel, sub_twoplane_one, sub_twoplane_two, TWO_PLANE);

		if (sub_twoplane_two != NULL)                                                     /*����ִ��two plane read ����*/
		{

			go_one_step(ssd, sub_twoplane_one, sub_twoplane_two, SR_R_C_A_TRANSFER, TWO_PLANE);
			*change_current_time_flag = 0;
			*channel_busy_flag = 1;                                                       /*�Ѿ�ռ����������ڵ����ߣ�����ִ��die�����ݵĻش�*/
		}
		else if ((ssd->parameter->advanced_commands&AD_INTERLEAVE) != AD_INTERLEAVE)       /*û����������������page��������û��interleave read����ʱ��ֻ��ִ�е���page�Ķ�*/
		{
			while (sub != NULL)                                                            /*if there are read requests in queue, send one of them to target die*/
			{
				if (sub->current_state == SR_WAIT)
				{	   
					/*ע���¸�����ж�������services_2_r_data_trans���ж������Ĳ�ͬ*/
					if ((ssd->channel_head[sub->location->channel].chip_head[sub->location->chip].current_state == CHIP_IDLE) || ((ssd->channel_head[sub->location->channel].chip_head[sub->location->chip].next_state == CHIP_IDLE) &&
						(ssd->channel_head[sub->location->channel].chip_head[sub->location->chip].next_state_predict_time <= ssd->current_time)))
					{
						go_one_step(ssd, sub, NULL, SR_R_C_A_TRANSFER, NORMAL);

						*change_current_time_flag = 0;
						*channel_busy_flag = 1;                                           /*�Ѿ�ռ����������ڵ����ߣ�����ִ��die�����ݵĻش�*/
						break;
					}
					else
					{
						*channel_busy_flag = 0;
					}
				}
				sub = sub->next_node;
			}
		}

		if (*channel_busy_flag == 0)
		{
			//printf("chip busy,%d\n",channel);
		}

	}

	/*******************************
	*ssd����ִ��ִ�и߼�����������
	*******************************/
	if (((ssd->parameter->advanced_commands&AD_INTERLEAVE) != AD_INTERLEAVE) && ((ssd->parameter->advanced_commands&AD_TWOPLANE_READ) != AD_TWOPLANE_READ))
	{
		printf("r_wait:normal command !\n");
		getchar();
		while (sub != NULL)                                                               /*if there are read requests in queue, send one of them to target chip*/
		{
			if (sub->current_state == SR_WAIT)
			{
				if ((ssd->channel_head[sub->location->channel].chip_head[sub->location->chip].current_state == CHIP_IDLE) || ((ssd->channel_head[sub->location->channel].chip_head[sub->location->chip].next_state == CHIP_IDLE) &&
					(ssd->channel_head[sub->location->channel].chip_head[sub->location->chip].next_state_predict_time <= ssd->current_time)))
				{

					go_one_step(ssd, sub, NULL, SR_R_C_A_TRANSFER, NORMAL);

					*change_current_time_flag = 0;
					*channel_busy_flag = 1;                                              /*�Ѿ�ռ����������ڵ����ߣ�����ִ��die�����ݵĻش�*/
					break;
				}
				else
				{
					/*��Ϊdie��busy���µ�����*/
				}
			}
			sub = sub->next_node;
		}
	}

	return SUCCESS;
}

/*********************************************************************
*��һ��д�����������Ҫ�����������ɾ���������������ִ��������ܡ�
**********************************************************************/
int delete_w_sub_request(struct ssd_info * ssd, unsigned int channel, struct sub_request * sub)
{
	struct sub_request * p = NULL;
	if (sub == ssd->channel_head[channel].subs_w_head)                                   /*������������channel������ɾ��*/
	{
		if (ssd->channel_head[channel].subs_w_head != ssd->channel_head[channel].subs_w_tail)
		{
			ssd->channel_head[channel].subs_w_head = sub->next_node;
		}
		else
		{
			ssd->channel_head[channel].subs_w_head = NULL;
			ssd->channel_head[channel].subs_w_tail = NULL;
		}
	}
	else
	{
		p = ssd->channel_head[channel].subs_w_head;
		while (p->next_node != sub)
		{
			p = p->next_node;
		}

		if (sub->next_node != NULL)
		{
			p->next_node = sub->next_node;
		}
		else
		{
			p->next_node = NULL;
			ssd->channel_head[channel].subs_w_tail = p;
		}
	}

	return SUCCESS;
}




/********************
д������Ĵ�����
*********************/
Status services_2_write(struct ssd_info * ssd, unsigned int channel, unsigned int * channel_busy_flag, unsigned int * change_current_time_flag)
{
	int j = 0, chip = 0;
	unsigned int k = 0;
	unsigned int  old_ppn = 0, new_ppn = 0;
	unsigned int chip_token = 0, die_token = 0;
	//unsigned int  die = 0, plane = 0;
	long long time = 0;
	struct sub_request * sub = NULL, *p = NULL;
	struct sub_request * sub_twoplane_one = NULL, *sub_twoplane_two = NULL;

	/************************************************************************************************************************
	*�����Ƕ�̬���䣬���е�д���������ssd->subs_w_head��������ǰ��֪��д���ĸ�channel��
	*************************************************************************************************************************/
	if (ssd->subs_w_head != NULL)
	{
		if (ssd->parameter->allocation_scheme == 0)                                       /*��̬����*/
		{
			for (j = 0; j<ssd->channel_head[channel].chip; j++)							  //�������е�chip
			{
				if (ssd->subs_w_head == NULL)											  //д�������꼴ֹͣѭ��
				{
					break;
				}

				chip_token = ssd->channel_head[channel].token;                            /*����*/
				if (*channel_busy_flag == 0)
				{
					if ((ssd->channel_head[channel].chip_head[chip_token].current_state == CHIP_IDLE) || ((ssd->channel_head[channel].chip_head[chip_token].next_state == CHIP_IDLE) && (ssd->channel_head[channel].chip_head[chip_token].next_state_predict_time <= ssd->current_time)))
					{
						if ((ssd->channel_head[channel].subs_w_head == NULL) && (ssd->subs_w_head == NULL))
						{
							break;
						}
						if (dynamic_advanced_process(ssd, channel, chip_token) == NULL)
						{
							*channel_busy_flag = 0;
						}
						else
						{
							*channel_busy_flag = 1;                                 /*ִ����һ�����󣬴��������ݣ�ռ�������ߣ���Ҫ��������һ��channel*/
							//ssd->channel_head[channel].chip_head[chip_token].token = (ssd->channel_head[channel].chip_head[chip_token].token + 1) % ssd->parameter->die_chip;
							ssd->channel_head[channel].token = (ssd->channel_head[channel].token + 1) % ssd->parameter->chip_channel[channel];
							break;
						}
					}
				}
				ssd->channel_head[channel].token = (ssd->channel_head[channel].token + 1) % ssd->parameter->chip_channel[channel];  //����chip
			}
		}
	}
	return SUCCESS;
}




/****************************************************************************************************************************
*��ssd֧�ָ߼�����ʱ��������������þ��Ǵ���߼������д������
*��������ĸ���������ѡ�����ָ߼�����������ֻ����д���󣬶������Ѿ����䵽ÿ��channel��������ִ��ʱ֮�����ѡȡ��Ӧ�����
*****************************************************************************************************************************/
struct ssd_info *dynamic_advanced_process(struct ssd_info *ssd, unsigned int channel, unsigned int chip)
{
	//unsigned int die = 0, plane = 0;
	unsigned int subs_count = 0;
	unsigned int plane_count = 0;
	int flag;                                                                   /*record the max subrequest that can be executed in the same channel. it will be used when channel-level priority order is highest and allocation scheme is full dynamic allocation*/
	unsigned int plane_place;                                                             /*record which plane has sub request in static allocation*/
	struct sub_request *sub = NULL, *p = NULL, *sub0 = NULL, *sub1 = NULL, *sub2 = NULL, *sub3 = NULL, *sub0_rw = NULL, *sub1_rw = NULL, *sub2_rw = NULL, *sub3_rw = NULL;
	struct sub_request ** subs = NULL;
	unsigned int max_sub_num = 0;
	unsigned int die_token = 0, plane_token = 0;
	unsigned int * plane_bits = NULL;
	unsigned int interleaver_count = 0;

	unsigned int mask = 0x00000001;
	unsigned int i = 0, j = 0;

	plane_count = ssd->parameter->plane_die;
	//ssd->real_time_subreq = 0;
	max_sub_num = (ssd->parameter->die_chip)*(ssd->parameter->plane_die);    //���￼�ǵ���chip die֮��Ĳ����ԣ���max_sub_num��ʾһ��chip�����ͬʱִ�е�������
	//gate = max_sub_num;
	subs = (struct sub_request **)malloc(max_sub_num*sizeof(struct sub_request *));
	alloc_assert(subs, "sub_request");

	for (i = 0; i<max_sub_num; i++)
	{
		subs[i] = NULL;  //��ִ����������
	}

	if ((ssd->parameter->allocation_scheme == 0))                                           /*ȫ��̬���䣬��Ҫ��ssd->subs_w_head��ѡȡ�ȴ������������*/
	{
		if (ssd->parameter->dynamic_allocation == 0)
		{
			sub = ssd->subs_w_head;
		}
		else
		{
			sub = ssd->channel_head[channel].subs_w_head;
		}

		subs_count = 0;

		//while ((sub != NULL) && (subs_count<max_sub_num) && (subs_count<gate))
		while ((sub != NULL) && (subs_count<max_sub_num))
		{
			if (sub->current_state == SR_WAIT)
			{
				if ((sub->update == NULL) || ((sub->update != NULL) && ((sub->update->current_state == SR_COMPLETE) || ((sub->update->next_state == SR_COMPLETE) && (sub->update->next_state_predict_time <= ssd->current_time)))))    //û����Ҫ��ǰ������ҳ
				{
					subs[subs_count] = sub;			//��Ŀǰ״̬��wait�����������������ȥ
					subs_count++;					//Ŀǰ����ȴ�״̬��������ĸ���
				}
			}

			p = sub;
			sub = sub->next_node;
		}

		if (subs_count == 0)                                                               /*û��������Է��񣬷���NULL*/
		{
			for (i = 0; i<max_sub_num; i++)
			{
				subs[i] = NULL;
			}
			free(subs);

			subs = NULL;
			free(plane_bits);
			return NULL;
		}

		//д������ֹ�����������Խ��и߼�����
		if (subs_count >= 2)
		{	
			/*********************************************
			*two plane,interleave������ʹ��
			*�����channel�ϣ�ѡ��interleave_two_planeִ��
			**********************************************/
			if (ssd->parameter->dynamic_allocation_priority == 1)						//��̬���䷽ʽ��ѡ��
			{
				/*
				while (subs_count != 0)
				{
					if ((ssd->parameter->advanced_commands&AD_TWOPLANE) == AD_TWOPLANE && plane_cmplt == 0)
					{
						if (get_ppn_for_advanced_commands(ssd, channel, chip, subs, plane_count, TWO_PLANE) == SUCCESS)
						{
							for (i = 0; i < subs_count - plane_count; i++)
								subs[i] = subs[i + plane_count];
							for (i = subs_count - plane_count; i < subs_count; i++)
								subs[i] = NULL;
							subs_count = subs_count - plane_count;
						}
						else
						{
							for (i = 0; i < subs_count - 1; i++)
								subs[i] = subs[i + 1];
							for (i = subs_count - 1; i < subs_count; i++)
								subs[i] = NULL;
							subs_count = subs_count - 1;
						}
					}
					else
					{
						break;
					}
				}
				*/

				if ((ssd->parameter->advanced_commands&AD_TWOPLANE) == AD_TWOPLANE && plane_cmplt == 0)
				{
					if (subs_count>ssd->parameter->plane_die)
					{
						for (i = ssd->parameter->plane_die; i<subs_count; i++)
						{
							subs[i] = NULL;
						}
						subs_count = ssd->parameter->plane_die;
					}
					get_ppn_for_advanced_commands(ssd, channel, chip, subs, subs_count, TWO_PLANE);
				}
				else
				{

				}
			}
			else
			{
				if ( (ssd->parameter->advanced_commands&AD_TWOPLANE) == AD_TWOPLANE )
				{
					if (subs_count>ssd->parameter->plane_die)
					{
						for (i = ssd->parameter->plane_die; i<subs_count; i++)
						{
							subs[i] = NULL;
						}
						subs_count = ssd->parameter->plane_die;
					}
					get_ppn_for_advanced_commands(ssd, channel, chip, subs, subs_count, TWO_PLANE);
				}
				else
				{
					for (i = 1; i<subs_count; i++)
					{
						subs[i] = NULL;
					}
					subs_count = 1;
					get_ppn_for_normal_command(ssd, channel, chip, subs[0]);
				}
			}

		}//if(subs_count>=2)
		else if (subs_count == 1)     //only one request
		{
			get_ppn_for_normal_command(ssd, channel, chip, subs[0]);
			printf("lz:normal_wr_1\n");
		}

	}//if ((ssd->parameter->allocation_scheme==0)) 

	for (i = 0; i<max_sub_num; i++)
	{
		subs[i] = NULL;
	}
	free(subs);
	subs = NULL;
	free(plane_bits);
	return ssd;
}



/***********************************************
*��������������sub0��sub1��ppn���ڵ�pageλ����ͬ
************************************************/
Status make_level_page(struct ssd_info * ssd, struct sub_request * sub0, struct sub_request * sub1)
{
	unsigned int i = 0, j = 0, k = 0;
	unsigned int channel = 0, chip = 0, die = 0, plane0 = 0, plane1 = 0, block0 = 0, block1 = 0, page0 = 0, page1 = 0;
	unsigned int active_block0 = 0, active_block1 = 0;
	unsigned int old_plane_token = 0;

	if ((sub0 == NULL) || (sub1 == NULL) || (sub0->location == NULL))
	{
		return ERROR;
	}
	channel = sub0->location->channel;
	chip = sub0->location->chip;
	die = sub0->location->die;
	plane0 = sub0->location->plane;
	block0 = sub0->location->block;
	page0 = sub0->location->page;
	old_plane_token = ssd->channel_head[channel].chip_head[chip].die_head[die].token;

	/***********************************************************************************************
	*��̬����������
	*sub1��plane�Ǹ���sub0��ssd->channel_head[channel].chip_head[chip].die_head[die].token���ƻ�ȡ��
	*sub1��channel��chip��die��block��page����sub0����ͬ
	************************************************************************************************/
	if (ssd->parameter->allocation_scheme == DYNAMIC_ALLOCATION)
	{
		old_plane_token = ssd->channel_head[channel].chip_head[chip].die_head[die].token;
		for (i = 0; i<ssd->parameter->plane_die; i++)
		{
			plane1 = ssd->channel_head[channel].chip_head[chip].die_head[die].token;
			if (ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane1].add_reg_ppn == -1)
			{
				find_active_block(ssd, channel, chip, die, plane1);                               /*��plane1���ҵ���Ծ��*/
				block1 = ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane1].active_block;

				/*********************************************************************************************
				*ֻ���ҵ���block1��block0��ͬ�����ܼ�������Ѱ����ͬ��page
				*��Ѱ��pageʱ�Ƚϼ򵥣�ֱ����last_write_page����һ��д��page��+1�Ϳ����ˡ�
				*����ҵ���page����ͬ����ô���ssd����̰����ʹ�ø߼���������Ϳ�����С��page �����page��£
				*********************************************************************************************/
				if (block1 == block0)
				{
					page1 = ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane1].blk_head[block1].last_write_page + 1;
					if (page1 == page0)
					{
						break;
					}
					else if (page1<page0)
					{
						if (ssd->parameter->greed_MPW_ad == 1)                                  /*����̰����ʹ�ø߼�����*/
						{
							//make_same_level(ssd,channel,chip,die,plane1,active_block1,page0); /*С��page��ַ�����page��ַ��*/
							make_same_level(ssd, channel, chip, die, plane1, block1, page0);
							break;
						}
					}
				}//if(block1==block0)
			}
			ssd->channel_head[channel].chip_head[chip].die_head[die].token = (plane1 + 1) % ssd->parameter->plane_die;
		}//for(i=0;i<ssd->parameter->plane_die;i++)
		if (i<ssd->parameter->plane_die)
		{
			flash_page_state_modify(ssd, sub1, channel, chip, die, plane1, block1, page0);          /*������������þ��Ǹ���page1����Ӧ������ҳ�Լ�location����map��*/
			//flash_page_state_modify(ssd,sub1,channel,chip,die,plane1,block1,page1);
			ssd->channel_head[channel].chip_head[chip].die_head[die].token = (plane1 + 1) % ssd->parameter->plane_die;
			return SUCCESS;
		}
		else
		{
			ssd->channel_head[channel].chip_head[chip].die_head[die].token = old_plane_token;
			return FAILURE;
		}
	}
}

/******************************************************************************************************
*�����Ĺ�����Ϊtwo plane����Ѱ�ҳ�������ͬˮƽλ�õ�ҳ�������޸�ͳ��ֵ���޸�ҳ��״̬
*ע�������������һ������make_level_page����������make_level_page�����������sub1��sub0��pageλ����ͬ
*��find_level_page�������������ڸ�����channel��chip��die��������λ����ͬ��subA��subB��
*******************************************************************************************************/
Status find_level_page(struct ssd_info *ssd, unsigned int channel, unsigned int chip, unsigned int die, struct sub_request *subA, struct sub_request *subB)
{
	unsigned int i, planeA, planeB, active_blockA, active_blockB, pageA, pageB, aim_page, old_plane;
	struct gc_operation *gc_node;

	old_plane = ssd->channel_head[channel].chip_head[chip].die_head[die].token;

	/************************************************************
	*�ڶ�̬����������
	*planeA����ֵΪdie�����ƣ����planeA��ż����ôplaneB=planeA+1
	*planeA����������ôplaneA+1��Ϊż��������planeB=planeA+1
	*************************************************************/
	if (ssd->parameter->allocation_scheme == 0)
	{
		planeA = ssd->channel_head[channel].chip_head[chip].die_head[die].token;
		if (planeA % 2 == 0)
		{
			planeB = planeA + 1;
			ssd->channel_head[channel].chip_head[chip].die_head[die].token = (ssd->channel_head[channel].chip_head[chip].die_head[die].token + 2) % ssd->parameter->plane_die;
		}
		else
		{
			planeA = (planeA + 1) % ssd->parameter->plane_die;
			planeB = planeA + 1;
			ssd->channel_head[channel].chip_head[chip].die_head[die].token = (ssd->channel_head[channel].chip_head[chip].die_head[die].token + 3) % ssd->parameter->plane_die;
		}
	}
	find_active_block(ssd, channel, chip, die, planeA);                                          //*Ѱ��active_block
	find_active_block(ssd, channel, chip, die, planeB);
	active_blockA = ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[planeA].active_block;
	active_blockB = ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[planeB].active_block;

	/*****************************************************
	*���active_block��ͬ����ô������������������ͬ��page
	*����ʹ��̰���ķ����ҵ�������ͬ��page
	******************************************************/
	if (active_blockA == active_blockB)
	{
		pageA = ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[planeA].blk_head[active_blockA].last_write_page + 1;
		pageB = ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[planeB].blk_head[active_blockB].last_write_page + 1;
		if (pageA == pageB)                                                                    /*�������õ�ҳ������ͬһ��ˮƽλ����*/
		{
			flash_page_state_modify(ssd, subA, channel, chip, die, planeA, active_blockA, pageA);
			flash_page_state_modify(ssd, subB, channel, chip, die, planeB, active_blockB, pageB);
		}
		else
		{
			if (ssd->parameter->greed_MPW_ad == 1)                                             /*̰����ʹ�ø߼�����*/
			{
				if (pageA<pageB)
				{
					aim_page = pageB;
					make_same_level(ssd, channel, chip, die, planeA, active_blockA, aim_page);     /*С��page��ַ�����page��ַ��*/
				}
				else
				{
					aim_page = pageA;
					make_same_level(ssd, channel, chip, die, planeB, active_blockB, aim_page);
				}
				flash_page_state_modify(ssd, subA, channel, chip, die, planeA, active_blockA, aim_page);
				flash_page_state_modify(ssd, subB, channel, chip, die, planeB, active_blockB, aim_page);
			}
			else                                                                             /*����̰����ʹ�ø߼�����*/
			{
				subA = NULL;
				subB = NULL;
				ssd->channel_head[channel].chip_head[chip].die_head[die].token = old_plane;
				return FAILURE;
			}
		}
	}
	/*********************************
	*����ҵ�������active_block����ͬ
	**********************************/
	else
	{
		pageA = ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[planeA].blk_head[active_blockA].last_write_page + 1;
		pageB = ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[planeB].blk_head[active_blockB].last_write_page + 1;
		if (pageA<pageB)
		{
			if (ssd->parameter->greed_MPW_ad == 1)                                             /*̰����ʹ�ø߼�����*/
			{
				/*******************************************************************************
				*��planeA�У���active_blockB��ͬλ�õĵ�block�У���pageB��ͬλ�õ�page�ǿ��õġ�
				*Ҳ����palneA�е���Ӧˮƽλ���ǿ��õģ�������Ϊ��planeB�ж�Ӧ��ҳ��
				*��ô��Ҳ��planeA��active_blockB�е�page��pageB��£
				********************************************************************************/
				if (ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[planeA].blk_head[active_blockB].page_head[pageB].free_state == PG_SUB)
				{
					make_same_level(ssd, channel, chip, die, planeA, active_blockB, pageB);
					flash_page_state_modify(ssd, subA, channel, chip, die, planeA, active_blockB, pageB);
					flash_page_state_modify(ssd, subB, channel, chip, die, planeB, active_blockB, pageB);
				}
				/********************************************************************************
				*��planeA�У���active_blockB��ͬλ�õĵ�block�У���pageB��ͬλ�õ�page�ǿ��õġ�
				*��ô��Ҫ����Ѱ��block����Ҫ������ˮƽλ����ͬ��һ��ҳ
				*********************************************************************************/
				else
				{
					for (i = 0; i<ssd->parameter->block_plane; i++)
					{
						pageA = ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[planeA].blk_head[i].last_write_page + 1;
						pageB = ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[planeB].blk_head[i].last_write_page + 1;
						if ((pageA<ssd->parameter->page_block) && (pageB<ssd->parameter->page_block))
						{
							if (pageA<pageB)
							{
								if (ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[planeA].blk_head[i].page_head[pageB].free_state == PG_SUB)
								{
									aim_page = pageB;
									make_same_level(ssd, channel, chip, die, planeA, i, aim_page);
									break;
								}
							}
							else
							{
								if (ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[planeB].blk_head[i].page_head[pageA].free_state == PG_SUB)
								{
									aim_page = pageA;
									make_same_level(ssd, channel, chip, die, planeB, i, aim_page);
									break;
								}
							}
						}
					}//for (i=0;i<ssd->parameter->block_plane;i++)
					if (i<ssd->parameter->block_plane)
					{
						flash_page_state_modify(ssd, subA, channel, chip, die, planeA, i, aim_page);
						flash_page_state_modify(ssd, subB, channel, chip, die, planeB, i, aim_page);
					}
					else
					{
						subA = NULL;
						subB = NULL;
						ssd->channel_head[channel].chip_head[chip].die_head[die].token = old_plane;
						return FAILURE;
					}
				}
			}//if (ssd->parameter->greed_MPW_ad==1)  
			else
			{
				subA = NULL;
				subB = NULL;
				ssd->channel_head[channel].chip_head[chip].die_head[die].token = old_plane;
				return FAILURE;
			}
		}//if (pageA<pageB)
		else
		{
			if (ssd->parameter->greed_MPW_ad == 1)
			{
				if (ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[planeB].blk_head[active_blockA].page_head[pageA].free_state == PG_SUB)
				{
					make_same_level(ssd, channel, chip, die, planeB, active_blockA, pageA);
					flash_page_state_modify(ssd, subA, channel, chip, die, planeA, active_blockA, pageA);
					flash_page_state_modify(ssd, subB, channel, chip, die, planeB, active_blockA, pageA);
				}
				else
				{
					for (i = 0; i<ssd->parameter->block_plane; i++)
					{
						pageA = ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[planeA].blk_head[i].last_write_page + 1;
						pageB = ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[planeB].blk_head[i].last_write_page + 1;
						if ((pageA<ssd->parameter->page_block) && (pageB<ssd->parameter->page_block))
						{
							if (pageA<pageB)
							{
								if (ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[planeA].blk_head[i].page_head[pageB].free_state == PG_SUB)
								{
									aim_page = pageB;
									make_same_level(ssd, channel, chip, die, planeA, i, aim_page);
									break;
								}
							}
							else
							{
								if (ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[planeB].blk_head[i].page_head[pageA].free_state == PG_SUB)
								{
									aim_page = pageA;
									make_same_level(ssd, channel, chip, die, planeB, i, aim_page);
									break;
								}
							}
						}
					}//for (i=0;i<ssd->parameter->block_plane;i++)
					if (i<ssd->parameter->block_plane)
					{
						flash_page_state_modify(ssd, subA, channel, chip, die, planeA, i, aim_page);
						flash_page_state_modify(ssd, subB, channel, chip, die, planeB, i, aim_page);
					}
					else
					{
						subA = NULL;
						subB = NULL;
						ssd->channel_head[channel].chip_head[chip].die_head[die].token = old_plane;
						return FAILURE;
					}
				}
			} //if (ssd->parameter->greed_MPW_ad==1) 
			else
			{
				if ((pageA == pageB) && (pageA == 0))
				{
					/*******************************************************************************************
					*�������������
					*1��planeA��planeB�е�active_blockA��pageAλ�ö����ã���ô��ͬplane ����ͬλ�ã���blockAΪ׼
					*2��planeA��planeB�е�active_blockB��pageAλ�ö����ã���ô��ͬplane ����ͬλ�ã���blockBΪ׼
					********************************************************************************************/
					if ((ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[planeA].blk_head[active_blockA].page_head[pageA].free_state == PG_SUB)
						&& (ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[planeB].blk_head[active_blockA].page_head[pageA].free_state == PG_SUB))
					{
						flash_page_state_modify(ssd, subA, channel, chip, die, planeA, active_blockA, pageA);
						flash_page_state_modify(ssd, subB, channel, chip, die, planeB, active_blockA, pageA);
					}
					else if ((ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[planeA].blk_head[active_blockB].page_head[pageA].free_state == PG_SUB)
						&& (ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[planeB].blk_head[active_blockB].page_head[pageA].free_state == PG_SUB))
					{
						flash_page_state_modify(ssd, subA, channel, chip, die, planeA, active_blockB, pageA);
						flash_page_state_modify(ssd, subB, channel, chip, die, planeB, active_blockB, pageA);
					}
					else
					{
						subA = NULL;
						subB = NULL;
						ssd->channel_head[channel].chip_head[chip].die_head[die].token = old_plane;
						return FAILURE;
					}
				}
				else
				{
					subA = NULL;
					subB = NULL;
					ssd->channel_head[channel].chip_head[chip].die_head[die].token = old_plane;
					return ERROR;
				}
			}
		}
	}

	if (ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[planeA].free_page<(ssd->parameter->page_block*ssd->parameter->block_plane*ssd->parameter->gc_hard_threshold))
	{
		gc_node = (struct gc_operation *)malloc(sizeof(struct gc_operation));
		alloc_assert(gc_node, "gc_node");
		memset(gc_node, 0, sizeof(struct gc_operation));

		gc_node->next_node = NULL;
		gc_node->chip = chip;
		gc_node->die = die;
		gc_node->plane = planeA;
		gc_node->block = 0xffffffff;
		gc_node->page = 0;
		gc_node->state = GC_WAIT;
		gc_node->priority = GC_UNINTERRUPT;
		gc_node->next_node = ssd->channel_head[channel].gc_command;
		ssd->channel_head[channel].gc_command = gc_node;
		ssd->gc_request++;
	}
	if (ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[planeB].free_page<(ssd->parameter->page_block*ssd->parameter->block_plane*ssd->parameter->gc_hard_threshold))
	{
		gc_node = (struct gc_operation *)malloc(sizeof(struct gc_operation));
		alloc_assert(gc_node, "gc_node");
		memset(gc_node, 0, sizeof(struct gc_operation));

		gc_node->next_node = NULL;
		gc_node->chip = chip;
		gc_node->die = die;
		gc_node->plane = planeB;
		gc_node->block = 0xffffffff;
		gc_node->page = 0;
		gc_node->state = GC_WAIT;
		gc_node->priority = GC_UNINTERRUPT;
		gc_node->next_node = ssd->channel_head[channel].gc_command;
		ssd->channel_head[channel].gc_command = gc_node;
		ssd->gc_request++;
	}

	return SUCCESS;
}




/********************************************
*�����Ĺ��ܾ���������λ�ò�ͬ��pageλ����ͬ
*********************************************/
struct ssd_info *make_same_level(struct ssd_info *ssd, unsigned int channel, unsigned int chip, unsigned int die, unsigned int plane, unsigned int block, unsigned int aim_page)
{
	int i = 0, step, page;
	struct direct_erase *new_direct_erase, *direct_erase_node;

	page = ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].last_write_page + 1;                  /*��Ҫ�����ĵ�ǰ��Ŀ�дҳ��*/
	step = aim_page - page;
	while (i<step)
	{
		ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[page + i].valid_state = 0;     /*��ʾĳһҳʧЧ��ͬʱ���valid��free״̬��Ϊ0*/
		ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[page + i].free_state = 0;      /*��ʾĳһҳʧЧ��ͬʱ���valid��free״̬��Ϊ0*/
		ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[page + i].lpn = 0;

		ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].invalid_page_num++;

		ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].free_page_num--;

		ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].free_page--;

		i++;
	}

	ssd->waste_page_count += step;

	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].last_write_page = aim_page - 1;

	if (ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].invalid_page_num == ssd->parameter->page_block)    /*��block��ȫ��invalid��ҳ������ֱ��ɾ��*/
	{
		new_direct_erase = (struct direct_erase *)malloc(sizeof(struct direct_erase));
		alloc_assert(new_direct_erase, "new_direct_erase");
		memset(new_direct_erase, 0, sizeof(struct direct_erase));

		direct_erase_node = ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].erase_node;
		if (direct_erase_node == NULL)
		{
			ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].erase_node = new_direct_erase;
		}
		else
		{
			new_direct_erase->next_node = ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].erase_node;
			ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].erase_node = new_direct_erase;
		}
	}

	if (ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].last_write_page>63)
	{
		printf("error! the last write page larger than 64!!\n");
		while (1){}
	}

	return ssd;
}



/****************************************************************************
*�ڴ���߼������д������ʱ����������Ĺ��ܾ��Ǽ��㴦��ʱ���Լ������״̬ת��
*���ܻ����Ǻ����ƣ���Ҫ���ƣ��޸�ʱע��Ҫ��Ϊ��̬����Ͷ�̬�����������
*****************************************************************************/
struct ssd_info *compute_serve_time(struct ssd_info *ssd, unsigned int channel, unsigned int chip, unsigned int die, struct sub_request **subs, unsigned int subs_count, unsigned int command)
{
	unsigned int i = 0;
	unsigned int max_subs_num = 0;
	struct sub_request *sub = NULL, *p = NULL;
	struct sub_request * last_sub = NULL;
	max_subs_num = ssd->parameter->die_chip*ssd->parameter->plane_die;

	if (command == TWO_PLANE)
	{
		for (i = 0; i<max_subs_num; i++)
		{
			if (subs[i] != NULL)
			{

				subs[i]->current_state = SR_W_TRANSFER;
				if (last_sub == NULL)
				{
					subs[i]->current_time = ssd->current_time;
				}
				else
				{
					subs[i]->current_time = last_sub->complete_time + ssd->parameter->time_characteristics.tDBSY;
				}

				subs[i]->next_state = SR_COMPLETE;
				subs[i]->next_state_predict_time = subs[i]->current_time + 7 * ssd->parameter->time_characteristics.tWC + (subs[i]->size*ssd->parameter->subpage_capacity)*ssd->parameter->time_characteristics.tWC;
				subs[i]->complete_time = subs[i]->next_state_predict_time;
				last_sub = subs[i];

				delete_from_channel(ssd, channel, subs[i]);
			}
		}
		ssd->channel_head[channel].current_state = CHANNEL_TRANSFER;
		ssd->channel_head[channel].current_time = ssd->current_time;
		ssd->channel_head[channel].next_state = CHANNEL_IDLE;
		ssd->channel_head[channel].next_state_predict_time = last_sub->complete_time;

		ssd->channel_head[channel].chip_head[chip].current_state = CHIP_WRITE_BUSY;
		ssd->channel_head[channel].chip_head[chip].current_time = ssd->current_time;
		ssd->channel_head[channel].chip_head[chip].next_state = CHIP_IDLE;
		ssd->channel_head[channel].chip_head[chip].next_state_predict_time = ssd->channel_head[channel].next_state_predict_time + ssd->parameter->time_characteristics.tPROG;
	}
	else if (command == NORMAL)
	{
		subs[0]->current_state = SR_W_TRANSFER;
		subs[0]->current_time = ssd->current_time;
		subs[0]->next_state = SR_COMPLETE;
		subs[0]->next_state_predict_time = ssd->current_time + 7 * ssd->parameter->time_characteristics.tWC + (subs[0]->size*ssd->parameter->subpage_capacity)*ssd->parameter->time_characteristics.tWC;
		subs[0]->complete_time = subs[0]->next_state_predict_time;

		delete_from_channel(ssd, channel, subs[0]);

		ssd->channel_head[channel].current_state = CHANNEL_TRANSFER;
		ssd->channel_head[channel].current_time = ssd->current_time;
		ssd->channel_head[channel].next_state = CHANNEL_IDLE;
		ssd->channel_head[channel].next_state_predict_time = subs[0]->complete_time;

		ssd->channel_head[channel].chip_head[chip].current_state = CHIP_WRITE_BUSY;
		ssd->channel_head[channel].chip_head[chip].current_time = ssd->current_time;
		ssd->channel_head[channel].chip_head[chip].next_state = CHIP_IDLE;
		ssd->channel_head[channel].chip_head[chip].next_state_predict_time = ssd->channel_head[channel].next_state_predict_time + ssd->parameter->time_characteristics.tPROG;
	}
	else
	{
		return NULL;
	}

	return ssd;

}

/*****************************************************************************************
*�����Ĺ��ܾ��ǰ��������ssd->subs_w_head����ssd->channel_head[channel].subs_w_head��ɾ��
******************************************************************************************/
struct ssd_info *delete_from_channel(struct ssd_info *ssd, unsigned int channel, struct sub_request * sub_req)
{
	struct sub_request *sub, *p;

	/******************************************************************
	*��ȫ��̬�������������ssd->subs_w_head��
	*������ȫ��̬�������������ssd->channel_head[channel].subs_w_head��
	*******************************************************************/
	if ((ssd->parameter->allocation_scheme == 0) && (ssd->parameter->dynamic_allocation == 0))
	{
		sub = ssd->subs_w_head;
	}
	else
	{
		sub = ssd->channel_head[channel].subs_w_head;
	}
	p = sub;

	while (sub != NULL)
	{
		if (sub == sub_req)
		{
			if ((ssd->parameter->allocation_scheme == 0) && (ssd->parameter->dynamic_allocation == 0))
			{
				if (ssd->parameter->ad_priority2 == 0)
				{
					ssd->real_time_subreq--;
				}

				if (sub == ssd->subs_w_head)                                                     /*������������sub request������ɾ��*/
				{
					if (ssd->subs_w_head != ssd->subs_w_tail)
					{
						ssd->subs_w_head = sub->next_node;
						sub = ssd->subs_w_head;
						continue;
					}
					else
					{
						ssd->subs_w_head = NULL;
						ssd->subs_w_tail = NULL;
						p = NULL;
						break;
					}
				}//if (sub==ssd->subs_w_head) 
				else
				{
					if (sub->next_node != NULL)
					{
						p->next_node = sub->next_node;
						sub = p->next_node;
						continue;
					}
					else
					{
						ssd->subs_w_tail = p;
						ssd->subs_w_tail->next_node = NULL;
						break;
					}
				}
			}//if ((ssd->parameter->allocation_scheme==0)&&(ssd->parameter->dynamic_allocation==0)) 
		}//if (sub==sub_req)
		p = sub;
		sub = sub->next_node;
	}//while (sub!=NULL)

	return ssd;
}


/****************************************************************************************
*�����Ĺ������ڴ����������ĸ߼�����ʱ����Ҫ����one_page��ƥ�������һ��page��two_page
*û���ҵ����Ժ�one_pageִ��two plane����interleave������ҳ,��Ҫ��one_page�����һ���ڵ�
*****************************************************************************************/
struct sub_request *find_interleave_twoplane_page(struct ssd_info *ssd, struct sub_request *one_page, unsigned int command)
{
	struct sub_request *two_page;
	two_page = malloc(sizeof(*two_page));

	if (one_page->lpn == 13926)
	{
		//printf("\n");
	}




	if (one_page->lpn == 21057)
	{
		//printf("\n");
	}

	if (one_page->current_state != SR_WAIT)
	{
		return NULL;
	}
	if (((ssd->channel_head[one_page->location->channel].chip_head[one_page->location->chip].current_state == CHIP_IDLE) || ((ssd->channel_head[one_page->location->channel].chip_head[one_page->location->chip].next_state == CHIP_IDLE) &&
		(ssd->channel_head[one_page->location->channel].chip_head[one_page->location->chip].next_state_predict_time <= ssd->current_time))))
	{
		two_page = one_page->next_node;
		if (command == TWO_PLANE)
		{
			while (two_page != NULL)
			{
				if (two_page->current_state != SR_WAIT)
				{
					two_page = two_page->next_node;
				}
				else if ((one_page->location->chip == two_page->location->chip) && (one_page->location->die == two_page->location->die) && (one_page->location->block == two_page->location->block) && (one_page->location->page == two_page->location->page))
				{
					if (one_page->location->plane != two_page->location->plane)
					{
						return two_page;                                                       /*�ҵ�����one_page����ִ��two plane������ҳ*/
					}
					else
					{
						two_page = two_page->next_node;
					}
				}
				else
				{
					two_page = two_page->next_node;
				}
			}//while (two_page!=NULL)
			if (two_page == NULL)                                                               /*û���ҵ����Ժ�one_pageִ��two_plane������ҳ,��Ҫ��one_page�����һ���ڵ�*/
			{
				return NULL;
			}
		}//if(command==TWO_PLANE)		
	}
	else
	{
		return NULL;
	}
}


/*************************************************************************
*�ڴ����������߼�����ʱ������������ǲ��ҿ���ִ�и߼������sub_request
**************************************************************************/
int find_interleave_twoplane_sub_request(struct ssd_info * ssd, unsigned int channel, struct sub_request ** sub_request_one, struct sub_request ** sub_request_two, unsigned int command)
{
	*sub_request_one = ssd->channel_head[channel].subs_r_head;


	while ((*sub_request_one) != NULL)
	{
		(*sub_request_two) = find_interleave_twoplane_page(ssd, *sub_request_one, command);                //*�ҳ�����������two_plane����interleave��read�����󣬰���λ��������ʱ������

		if (*sub_request_two == NULL)
		{
			*sub_request_one = (*sub_request_one)->next_node;
		}
		else if (*sub_request_two != NULL)                                                            //*�ҵ�����������ִ��two plane������ҳ
		{
			break;
		}
	}


	if (*sub_request_two != NULL)
	{
		return SUCCESS;
	}
	else
	{
		return FAILURE;
	}

}


/**************************************************************************
*��������ǳ���Ҫ�����������״̬ת�䣬�Լ�ʱ��ļ��㶼ͨ���������������
*����д�������ִ����ͨ����ʱ��״̬���Լ�ʱ��ļ���Ҳ��ͨ����������������
****************************************************************************/
Status go_one_step(struct ssd_info * ssd, struct sub_request * sub1, struct sub_request *sub2, unsigned int aim_state, unsigned int command)
{
	unsigned int i = 0, j = 0, k = 0, m = 0;
	long long time = 0;
	struct sub_request * sub = NULL;
	struct sub_request * sub_twoplane_one = NULL, *sub_twoplane_two = NULL;
	struct sub_request * sub_interleave_one = NULL, *sub_interleave_two = NULL;
	struct local * location = NULL;

	struct buffer_group *update_buffer_node = NULL, key;

	if (sub1 == NULL)
	{
		return ERROR;
	}

	/***************************************************************************************************
	*������ͨ����ʱ�����������Ŀ��״̬��Ϊ���¼������SR_R_READ��SR_R_C_A_TRANSFER��SR_R_DATA_TRANSFER
	*д�������Ŀ��״ֻ̬��SR_W_TRANSFER
	****************************************************************************************************/
	if (command == NORMAL)
	{
		sub = sub1;
		location = sub1->location;

		switch (aim_state)
		{
		case SR_R_READ:
		{
			/*****************************************************************************************************
			*���Ŀ��״̬��ָflash���ڶ����ݵ�״̬��sub����һ״̬��Ӧ���Ǵ�������SR_R_DATA_TRANSFER
			*��ʱ��channel�޹أ�ֻ��chip�й�����Ҫ�޸�chip��״̬ΪCHIP_READ_BUSY����һ��״̬����CHIP_DATA_TRANSFER
			******************************************************************************************************/
			sub->current_time = ssd->current_time;
			sub->current_state = SR_R_READ;
			sub->next_state = SR_R_DATA_TRANSFER;
			sub->next_state_predict_time = ssd->current_time + ssd->parameter->time_characteristics.tR;

			ssd->channel_head[location->channel].chip_head[location->chip].current_state = CHIP_READ_BUSY;
			ssd->channel_head[location->channel].chip_head[location->chip].current_time = ssd->current_time;
			ssd->channel_head[location->channel].chip_head[location->chip].next_state = CHIP_DATA_TRANSFER;
			ssd->channel_head[location->channel].chip_head[location->chip].next_state_predict_time = ssd->current_time + ssd->parameter->time_characteristics.tR;

			break;
		}
		case SR_R_C_A_TRANSFER:
		{
			/*******************************************************************************************************
			*Ŀ��״̬�������ַ����ʱ��sub����һ��״̬����SR_R_READ
			*���״̬��channel��chip�йأ�����Ҫ�޸�channel��chip��״̬�ֱ�ΪCHANNEL_C_A_TRANSFER��CHIP_C_A_TRANSFER
			*��һ״̬�ֱ�ΪCHANNEL_IDLE��CHIP_READ_BUSY
			*******************************************************************************************************/
			sub->current_time = ssd->current_time;
			sub->current_state = SR_R_C_A_TRANSFER;
			sub->next_state = SR_R_READ;
			sub->next_state_predict_time = ssd->current_time + 7 * ssd->parameter->time_characteristics.tWC;
			sub->begin_time = ssd->current_time;

			ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].add_reg_ppn = sub->ppn;
			//printf("r_data_trans read\n");
			ssd->read_count++;
			ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_read_count++;

			ssd->channel_head[location->channel].current_state = CHANNEL_C_A_TRANSFER;
			ssd->channel_head[location->channel].current_time = ssd->current_time;
			ssd->channel_head[location->channel].next_state = CHANNEL_IDLE;
			ssd->channel_head[location->channel].next_state_predict_time = ssd->current_time + 7 * ssd->parameter->time_characteristics.tWC;

			ssd->channel_head[location->channel].chip_head[location->chip].current_state = CHIP_C_A_TRANSFER;
			ssd->channel_head[location->channel].chip_head[location->chip].current_time = ssd->current_time;
			ssd->channel_head[location->channel].chip_head[location->chip].next_state = CHIP_READ_BUSY;
			ssd->channel_head[location->channel].chip_head[location->chip].next_state_predict_time = ssd->current_time + 7 * ssd->parameter->time_characteristics.tWC;

			break;

		}
		case SR_R_DATA_TRANSFER:
		{
			/**************************************************************************************************************
			*Ŀ��״̬�����ݴ���ʱ��sub����һ��״̬�������״̬SR_COMPLETE
			*���״̬�Ĵ���Ҳ��channel��chip�йأ�����channel��chip�ĵ�ǰ״̬��ΪCHANNEL_DATA_TRANSFER��CHIP_DATA_TRANSFER
			*��һ��״̬�ֱ�ΪCHANNEL_IDLE��CHIP_IDLE��
			***************************************************************************************************************/
			sub->current_time = ssd->current_time;
			sub->current_state = SR_R_DATA_TRANSFER;
			sub->next_state = SR_COMPLETE;
			sub->next_state_predict_time = ssd->current_time + (sub->size*ssd->parameter->subpage_capacity)*ssd->parameter->time_characteristics.tRC;
			sub->complete_time = sub->next_state_predict_time;


			if (sub->update_read_flag == 1)
			{
				sub->update_read_flag = 0;
				//����buff��Ĳ���д��������С
				key.group = sub->lpn;
				update_buffer_node = (struct buffer_group*)avlTreeFind(ssd->dram->buffer, (TREE_NODE *)&key);    /*��ƽ���������Ѱ��buffer node*/
				update_buffer_node->stored = sub->state | update_buffer_node->stored;
				update_buffer_node->dirty_clean = sub->state | update_buffer_node->stored;
				update_buffer_node->page_type = 0;
				buffer_full_flag = 0;   //���buff������

			}


			ssd->channel_head[location->channel].current_state = CHANNEL_DATA_TRANSFER;
			ssd->channel_head[location->channel].current_time = ssd->current_time;
			ssd->channel_head[location->channel].next_state = CHANNEL_IDLE;
			ssd->channel_head[location->channel].next_state_predict_time = sub->next_state_predict_time;

			ssd->channel_head[location->channel].chip_head[location->chip].current_state = CHIP_DATA_TRANSFER;
			ssd->channel_head[location->channel].chip_head[location->chip].current_time = ssd->current_time;
			ssd->channel_head[location->channel].chip_head[location->chip].next_state = CHIP_IDLE;
			ssd->channel_head[location->channel].chip_head[location->chip].next_state_predict_time = sub->next_state_predict_time;

			ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].add_reg_ppn = -1;

			break;
		}
		case SR_W_TRANSFER:
		{
			/******************************************************************************************************
			*���Ǵ���д������ʱ��״̬��ת���Լ�ʱ��ļ���
			*��Ȼд������Ĵ���״̬Ҳ�����������ô�࣬����д�����Ǵ�����plane�д�������
			*�����Ϳ��԰Ѽ���״̬��һ��״̬�������͵���SR_W_TRANSFER���״̬������sub����һ��״̬�������״̬��
			*��ʱchannel��chip�ĵ�ǰ״̬��ΪCHANNEL_TRANSFER��CHIP_WRITE_BUSY
			*��һ��״̬��ΪCHANNEL_IDLE��CHIP_IDLE
			*******************************************************************************************************/
			sub->current_time = ssd->current_time;
			sub->current_state = SR_W_TRANSFER;
			sub->next_state = SR_COMPLETE;
			sub->next_state_predict_time = ssd->current_time + 7 * ssd->parameter->time_characteristics.tWC + (sub->size*ssd->parameter->subpage_capacity)*ssd->parameter->time_characteristics.tWC;
			sub->complete_time = sub->next_state_predict_time;
			time = sub->complete_time;

			ssd->channel_head[location->channel].current_state = CHANNEL_TRANSFER;
			ssd->channel_head[location->channel].current_time = ssd->current_time;
			ssd->channel_head[location->channel].next_state = CHANNEL_IDLE;
			ssd->channel_head[location->channel].next_state_predict_time = time;

			ssd->channel_head[location->channel].chip_head[location->chip].current_state = CHIP_WRITE_BUSY;
			ssd->channel_head[location->channel].chip_head[location->chip].current_time = ssd->current_time;
			ssd->channel_head[location->channel].chip_head[location->chip].next_state = CHIP_IDLE;
			ssd->channel_head[location->channel].chip_head[location->chip].next_state_predict_time = time + ssd->parameter->time_characteristics.tPROG;

			break;
		}
		default:  return ERROR;

		}//switch(aim_state)	
	}//if(command==NORMAL)

	else if (command == TWO_PLANE)
	{
		/**********************************************************************************************
		*�߼�����TWO_PLANE�Ĵ��������TWO_PLANE�߼������Ƕ�������ĸ߼�����
		*״̬ת������ͨ����һ������ͬ������SR_R_C_A_TRANSFERʱ����ʱ���Ǵ��еģ���Ϊ����һ��ͨ��channel
		*����SR_R_DATA_TRANSFERҲ�ǹ���һ��ͨ��
		**********************************************************************************************/
		if ((sub1 == NULL) || (sub2 == NULL))
		{
			return ERROR;
		}
		sub_twoplane_one = sub1;
		sub_twoplane_two = sub2;
		location = sub1->location;

		switch (aim_state)
		{
		case SR_R_C_A_TRANSFER:
		{
			sub_twoplane_one->current_time = ssd->current_time;
			sub_twoplane_one->current_state = SR_R_C_A_TRANSFER;
			sub_twoplane_one->next_state = SR_R_READ;
			sub_twoplane_one->next_state_predict_time = ssd->current_time + 14 * ssd->parameter->time_characteristics.tWC;
			sub_twoplane_one->begin_time = ssd->current_time;

			ssd->channel_head[sub_twoplane_one->location->channel].chip_head[sub_twoplane_one->location->chip].die_head[sub_twoplane_one->location->die].plane_head[sub_twoplane_one->location->plane].add_reg_ppn = sub_twoplane_one->ppn;
			ssd->read_count++;
			ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_read_count++;

			sub_twoplane_two->current_time = ssd->current_time;
			sub_twoplane_two->current_state = SR_R_C_A_TRANSFER;
			sub_twoplane_two->next_state = SR_R_READ;
			sub_twoplane_two->next_state_predict_time = sub_twoplane_one->next_state_predict_time;
			sub_twoplane_two->begin_time = ssd->current_time;

			ssd->channel_head[sub_twoplane_two->location->channel].chip_head[sub_twoplane_two->location->chip].die_head[sub_twoplane_two->location->die].plane_head[sub_twoplane_two->location->plane].add_reg_ppn = sub_twoplane_two->ppn;
			ssd->read_count++;
			ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_read_count++;
			ssd->m_plane_read_count++;

			ssd->channel_head[location->channel].current_state = CHANNEL_C_A_TRANSFER;
			ssd->channel_head[location->channel].current_time = ssd->current_time;
			ssd->channel_head[location->channel].next_state = CHANNEL_IDLE;
			ssd->channel_head[location->channel].next_state_predict_time = ssd->current_time + 14 * ssd->parameter->time_characteristics.tWC;

			ssd->channel_head[location->channel].chip_head[location->chip].current_state = CHIP_C_A_TRANSFER;
			ssd->channel_head[location->channel].chip_head[location->chip].current_time = ssd->current_time;
			ssd->channel_head[location->channel].chip_head[location->chip].next_state = CHIP_READ_BUSY;
			ssd->channel_head[location->channel].chip_head[location->chip].next_state_predict_time = ssd->current_time + 14 * ssd->parameter->time_characteristics.tWC;


			break;
		}
		case SR_R_DATA_TRANSFER:
		{
			sub_twoplane_one->current_time = ssd->current_time;
			sub_twoplane_one->current_state = SR_R_DATA_TRANSFER;
			sub_twoplane_one->next_state = SR_COMPLETE;
			sub_twoplane_one->next_state_predict_time = ssd->current_time + (sub_twoplane_one->size*ssd->parameter->subpage_capacity)*ssd->parameter->time_characteristics.tRC;
			sub_twoplane_one->complete_time = sub_twoplane_one->next_state_predict_time;

			sub_twoplane_two->current_time = sub_twoplane_one->next_state_predict_time;
			sub_twoplane_two->current_state = SR_R_DATA_TRANSFER;
			sub_twoplane_two->next_state = SR_COMPLETE;
			sub_twoplane_two->next_state_predict_time = sub_twoplane_two->current_time + (sub_twoplane_two->size*ssd->parameter->subpage_capacity)*ssd->parameter->time_characteristics.tRC;
			sub_twoplane_two->complete_time = sub_twoplane_two->next_state_predict_time;


			if (sub_twoplane_one->update_read_flag == 1)
			{
				sub_twoplane_one->update_read_flag = 0;
				//����buff��Ĳ���д��������С
				key.group = sub_twoplane_one->lpn;
				update_buffer_node = (struct buffer_group*)avlTreeFind(ssd->dram->buffer, (TREE_NODE *)&key);    /*��ƽ���������Ѱ��buffer node*/
				update_buffer_node->stored = sub_twoplane_one->state | update_buffer_node->stored;
				update_buffer_node->dirty_clean = sub_twoplane_one->state | update_buffer_node->stored;
				update_buffer_node->page_type = 0;
				buffer_full_flag = 0;
			}
			else if (sub_twoplane_two->update_read_flag == 1)
			{
				sub_twoplane_two->update_read_flag = 0;
				//����buff��Ĳ���д��������С
				key.group = sub_twoplane_two->lpn;
				update_buffer_node = (struct buffer_group*)avlTreeFind(ssd->dram->buffer, (TREE_NODE *)&key);    /*��ƽ���������Ѱ��buffer node*/
				update_buffer_node->stored = sub_twoplane_two->state | update_buffer_node->stored;
				update_buffer_node->dirty_clean = sub_twoplane_two->state | update_buffer_node->stored;
				update_buffer_node->page_type = 0;
				buffer_full_flag = 0;
			}

			ssd->channel_head[location->channel].current_state = CHANNEL_DATA_TRANSFER;
			ssd->channel_head[location->channel].current_time = ssd->current_time;
			ssd->channel_head[location->channel].next_state = CHANNEL_IDLE;
			ssd->channel_head[location->channel].next_state_predict_time = sub_twoplane_one->next_state_predict_time;

			ssd->channel_head[location->channel].chip_head[location->chip].current_state = CHIP_DATA_TRANSFER;
			ssd->channel_head[location->channel].chip_head[location->chip].current_time = ssd->current_time;
			ssd->channel_head[location->channel].chip_head[location->chip].next_state = CHIP_IDLE;
			ssd->channel_head[location->channel].chip_head[location->chip].next_state_predict_time = sub_twoplane_one->next_state_predict_time;

			//��״̬ת����ɣ���ʱplane�Ĵ���ֵ��Ϊ��ʼֵ
			ssd->channel_head[sub_twoplane_one->location->channel].chip_head[sub_twoplane_one->location->chip].die_head[sub_twoplane_one->location->die].plane_head[sub_twoplane_one->location->plane].add_reg_ppn = -1;
			ssd->channel_head[sub_twoplane_two->location->channel].chip_head[sub_twoplane_two->location->chip].die_head[sub_twoplane_two->location->die].plane_head[sub_twoplane_two->location->plane].add_reg_ppn = -1;

			break;
		}
		default:  return ERROR;
		}//switch(aim_state)	
	}//else if(command==TWO_PLANE)
	else
	{
		printf("\nERROR: Unexpected command !\n");
		return ERROR;
	}

	return SUCCESS;
}