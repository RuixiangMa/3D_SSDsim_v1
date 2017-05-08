/*****************************************************************************************************************************
This is a project on 3D_SSDsim, based on ssdsim under the framework of the completion of structures, the main function:
1.Support for 3D commands, for example:mutli plane\interleave\copyback\program suspend/Resume..etc
2.Multi - level parallel simulation
3.Clear hierarchical interface
4.4-layer structure

FileName�� ssd.c
Author: Zuo Lu 		Version: 1.0	Date:2017/04/06
Description: System main function c file, Contains the basic flow of simulation.
Mainly includes: initialization, make_aged, pre_process_page three parts

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
#include <crtdbg.h>  


#include "ssd.h"
#include "initialize.h"
#include "flash.h"
#include "buffer.h"
#include "interface.h"
#include "ftl.h"
#include "fcl.h"


//Global variable
int make_age_free_page = 0;
int plane_cmplt = 0;
int buffer_full_flag = 0;
__int64 request_lz_count = 0;
int trace_over_flag = 0;
int lz_k=0;
__int64 compare_time = 0;



/********************************************************************************************************************************
1��main������initiatio()����������ʼ��ssd,��2��make_aged()����ʹSSD��Ϊaged��aged��ssd�൱��ʹ�ù�һ��ʱ���ssd��������ʧЧҳ��
non_aged��ssd���µ�ssd����ʧЧҳ��ʧЧҳ�ı��������ڳ�ʼ�����������ã�3��pre_process_page()������ǰɨһ������󣬰Ѷ�����
��lpn<--->ppnӳ���ϵ���Ƚ����ã�д�����lpn<--->ppnӳ���ϵ��д��ʱ���ٽ�����Ԥ����trace��ֹ�������Ƕ��������ݣ�4��simulate()��
���Ĵ�������trace�ļ��Ӷ�������������ɶ��������������ɣ�5��statistic_output()������ssd�ṹ�е���Ϣ���������ļ����������
ͳ�����ݺ�ƽ�����ݣ�����ļ���С��trace_output�ļ���ܴ����ϸ��6��free_all_node()�����ͷ�����main����������Ľڵ�
*********************************************************************************************************************************/

void main()
{
	unsigned  int i,j,k,p,m,n;
	struct ssd_info *ssd;

	#ifdef DEBUG
	printf("enter main\n"); 
	#endif

	ssd=(struct ssd_info*)malloc(sizeof(struct ssd_info));
	alloc_assert(ssd,"ssd");
	memset(ssd,0, sizeof(struct ssd_info));

	ssd=initiation(ssd);

	make_aged(ssd);

	pre_process_page(ssd);

	if (ssd->parameter->aged == 1)
	{
		pre_process_write(ssd);   //����Ч���е�free_pageȫ����Ϊ��Ч����֤���һ����Ч���а�����free page,����ʵ��ssd�Ļ���
	}

	//��ʱԤ�������֮��Ӧ�ñ�֤ÿ��plane��ҳƫ�Ƶ�ַ��һ�µ�
	
	for (i=0;i<ssd->parameter->channel_number;i++)
	{
		for (m = 0; m < ssd->parameter->chip_channel[i]; m++)
		{
			for (j = 0; j < ssd->parameter->die_chip; j++)
			{
				for (k = 0; k < ssd->parameter->plane_die; k++)
				{	
					/*
					for (p = 0; p < ssd->parameter->block_plane; p++)
					{
						printf("%d,0,%d,%d,%d,%d:  %5d\n", i, m, j, k, p, ssd->channel_head[i].chip_head[m].die_head[j].plane_head[k].blk_head[p].last_write_page);
					}
					*/
					printf("%d,0,%d,%d,%d:  %5d\n", i, m, j, k, ssd->channel_head[i].chip_head[m].die_head[j].plane_head[k].free_page);
				}
			}
		}
	}

	fprintf(ssd->outputfile,"\t\t\t\t\t\t\t\t\tOUTPUT\n");
	fprintf(ssd->outputfile,"****************** TRACE INFO ******************\n");

	ssd=simulate(ssd);
	statistic_output(ssd);  
	free_all_node(ssd);

	printf("\n");
	printf("the simulation is completed!\n");

	system("pause");
 	_CrtDumpMemoryLeaks();
}


/******************simulate() *********************************************************************
*simulate()�Ǻ��Ĵ���������Ҫʵ�ֵĹ��ܰ���
*1,��trace�ļ��л�ȡһ�����󣬹ҵ�ssd->request
*2������ssd�Ƿ���dram�ֱ�������������󣬰���Щ�������Ϊ��д�����󣬹ҵ�ssd->channel����ssd��
*3�������¼����Ⱥ���������Щ��д������
*4�����ÿ������������󶼴������������Ϣ��outputfile�ļ���
**************************************************************************************************/
struct ssd_info *simulate(struct ssd_info *ssd)
{
	int flag=1,flag1=0;
	double output_step=0;
	unsigned int a=0,b=0;
	errno_t err;

	
	unsigned int channel_num = 0, chip_num = 0, die_num = 0;
	unsigned int i, j, k,m,p;

	printf("\n");
	printf("begin simulating.......................\n");
	printf("\n");
	printf("\n");
	printf("   ^o^    OK, please wait a moment, and enjoy music and coffee   ^o^    \n");

	//���Ԥ����ʱ���������
	channel_num = ssd->parameter->channel_number;
	chip_num = ssd->parameter->chip_channel[0];
	die_num = ssd->parameter->die_chip;
	ssd->token = 0;
	for (i = 0; i < channel_num; i++)
	{
		for (j = 0; j < chip_num; j++)
		{
			for (k = 0; k < die_num; k++)
			{
				ssd->channel_head[i].chip_head[j].die_head[k].token = 0;
			}
			ssd->channel_head[i].chip_head[j].token = 0;
		}
		ssd->channel_head[i].token = 0;
	}


	if((err=fopen_s(&(ssd->tracefile),ssd->tracefilename,"r"))!=0)
	{  
		printf("the trace file can't open\n");
		return NULL;
	}

	fprintf(ssd->outputfile,"      arrive           lsn     size ope     begin time    response time    process time\n");	
	fflush(ssd->outputfile);

	while(flag!=100)      
	{        
		/*interface��*/
		flag = get_requests(ssd);        
		
		/*buffer��*/
		if (flag == 1 || (flag == 0 && ssd->request_work != NULL))
		{   
			//printf("once\n");
			if (ssd->parameter->dram_capacity!=0)
			{
				if (buffer_full_flag == 0)				//buffδ����״̬����ִ��buff����
				{
					buffer_management(ssd);
					distribute(ssd);
				}
			} 
			else
			{
				no_buffer_distribute(ssd);
			}

			if (ssd->request_work->cmplt_flag == 1)
			{
				if (ssd->request_work != ssd->request_tail)
					ssd->request_work = ssd->request_work->next_node;
				else
					ssd->request_work = NULL;
			}
		}
		

		/*ftl+fcl+flash��*/
		process(ssd);    
		trace_output(ssd);
		
		/*
		if (trace_over_flag == 1)
			flag = 0;
		*/

		if (flag == 0 && ssd->request_queue == NULL)
			flag = 100;
	}

	fclose(ssd->tracefile);
	return ssd;
}


/********************************************************
*�����������Ҫ���������ض��������д�������״̬�仯����
*********************************************************/

struct ssd_info *process(struct ssd_info *ssd)
{

	/*********************************************************************************************************
	*flag_die��ʾ�Ƿ���Ϊdie��busy��������ʱ��ǰ����-1��ʾû�У���-1��ʾ��������
	*flag_die��ֵ��ʾdie��,old ppn��¼��copyback֮ǰ������ҳ�ţ������ж�copyback�Ƿ���������ż��ַ�����ƣ�
	*two_plane_bit[8],two_plane_place[8]�����Ա��ʾͬһ��channel��ÿ��die��������������
	*chg_cur_time_flag��Ϊ�Ƿ���Ҫ������ǰʱ��ı�־λ������Ϊchannel����busy������������ʱ����Ҫ������ǰʱ�䣻
	*��ʼ��Ϊ��Ҫ��������Ϊ1�����κ�һ��channel�����˴��������������ʱ�����ֵ��Ϊ0����ʾ����Ҫ������
	**********************************************************************************************************/
	int old_ppn = -1, flag_die = -1;
	unsigned int i,j,k, chan, random_num;
	unsigned int flag = 0, new_write = 0, chg_cur_time_flag = 1, flag2 = 0, flag_gc = 0;
	__int64 time, channel_time = 0x7fffffffffffffff;
	struct sub_request *sub;
	
	unsigned int  m, p;
	unsigned int channel = 0, chip = 0, die = 0;

#ifdef DEBUG
	printf("enter process,  current time:%I64u\n", ssd->current_time);
#endif


	/*********************************************************
	*�ж��Ƿ��ж�д�������������ôflag��Ϊ0��û��flag��Ϊ1
	*��flagΪ1ʱ����ssd����gc������ʱ�Ϳ���ִ��gc����
	**********************************************************/

	/*ftl��*/
	//����gc�����ڱ��������е�channel������gc������Ч��Ĳ���ƫ��һ�����ٴ�д��ʱ��plane��ƫ�Ƶ�ַ����һ����
	for (i = 0; i<ssd->parameter->channel_number; i++)
	{
		if ((ssd->channel_head[i].subs_r_head == NULL) && (ssd->channel_head[i].subs_w_head == NULL) && (ssd->subs_w_head == NULL))
		{
			flag = 1;
		}
		else
		{
			flag = 0;
			break;
		}
	}


	if (flag == 1)
	{
		ssd->flag = 1;
		if (ssd->gc_request>0)                                                            /*SSD����gc����������*/
		{
			gc(ssd, 0, 1);                                                                  /*���gcҪ������channel�����������*/
		}
		return ssd;
	}
	else
	{
		ssd->flag = 0;
	}

	/*fcl+flash��*/
	time = ssd->current_time;
	services_2_r_cmd_trans_and_complete(ssd);                                            /*����ǰ״̬��SR_R_C_A_TRANSFER���ߵ�ǰ״̬��SR_COMPLETE��������һ״̬��SR_COMPLETE������һ״̬Ԥ��ʱ��С�ڵ�ǰ״̬ʱ��*/
	
	/*****************************************
	*ѭ����������channel�ϵĶ�д������
	*���������������д���ݣ�����Ҫռ�����ߣ�
	******************************************/
	random_num = ssd->program_count%ssd->parameter->channel_number;                        /*����һ�����������֤ÿ�δӲ�ͬ��channel��ʼ��ѯ*/
	for (chan = 0; chan<ssd->parameter->channel_number; chan++)
	{
		i = (random_num + chan) % ssd->parameter->channel_number;
		flag = 0;
		flag_gc = 0;																		/*ÿ�ν���channelʱ����gc�ı�־λ��Ϊ0��Ĭ����Ϊû�н���gc����*/

		//������״̬�ƽ���Ҫ�����������channel�ƽ�״̬
		if ((ssd->channel_head[i].current_state == CHANNEL_IDLE) || (ssd->channel_head[i].next_state == CHANNEL_IDLE&&ssd->channel_head[i].next_state_predict_time <= ssd->current_time))
		{
			
			//*ftl��
			if (ssd->gc_request>0)                                                       //��gc��������Ҫ����һ�����ж�
			{
				if (ssd->channel_head[i].gc_command != NULL)
				{
					flag_gc = gc(ssd, i, 0);                                                 //gc��������һ��ֵ����ʾ�Ƿ�ִ����gc���������ִ����gc���������channel�����ʱ�̲��ܷ�������������
				}
				if (flag_gc == 1)                                                          //ִ�й�gc��������Ҫ�����˴�ѭ��
				{
					continue;
				}
			}
			

			/*fcl+flash��*/
			sub = ssd->channel_head[i].subs_r_head;                                        /*�ȴ��������*/

			services_2_r_wait(ssd, i, &flag, &chg_cur_time_flag);                           /*�����ڵȴ�״̬�Ķ�������*/

			if ((flag == 0) && (ssd->channel_head[i].subs_r_head != NULL))                      /*if there are no new read request and data is ready in some dies, send these data to controller and response this request*/
			{
				services_2_r_data_trans(ssd, i, &flag, &chg_cur_time_flag);

			}


			//��ʼ����д�����״̬ת��
			if (flag == 0)                                                                  /*if there are no read request to take channel, we can serve write requests*/
			{			
				if (ssd->parameter->dynamic_allocation_priority == 1)
				{
					//����������ش����ʱһ��channel�ϵ�superpage д��ɣ�������һ��channel 
					channel = ssd->token;
					chip = ssd->channel_head[channel].token;
					die = ssd->channel_head[channel].chip_head[chip].token;

					//��ʾ��ǰд�������
					services_2_write(ssd, channel, &flag, &chg_cur_time_flag);

					//�����planeͬʱд��֮�󣬲��ܽ���channel����
					if (plane_cmplt == 1)
					{
						ssd->token = (ssd->token + 1) % ssd->parameter->channel_number;
						plane_cmplt = 0;
						if (channel == (ssd->parameter->channel_number - 1))
							ssd->channel_head[ssd->token].chip_head[chip].token = (die + 1) % ssd->parameter->die_chip;
						else
							ssd->channel_head[ssd->token].chip_head[chip].token = die;
					}
					//printf("aaa\n");
					
				}
				else
				{
					services_2_write(ssd, i, &flag, &chg_cur_time_flag); 
				}
				
			}
		}

		/*��ʱ�����鿴plane�ڵ�ƫ�Ƶ�ַ�Ƿ���ͬ���Ӷ���֤���Ǵ������Ч��*/
		/*
		for (j = 0; j < ssd->parameter->die_chip; j++)
		{
			for (i = 0; i<ssd->parameter->channel_number; i++)
			{
				for (m = 0; m < ssd->parameter->chip_channel[i]; m++)
				{
					for (k = 0; k < ssd->parameter->plane_die; k++)
					{
						for (p = 0; p < ssd->parameter->block_plane; p++)
						{
							if ((ssd->channel_head[i].chip_head[m].die_head[j].plane_head[k].blk_head[p].free_page_num > 0) && (ssd->channel_head[i].chip_head[m].die_head[j].plane_head[k].blk_head[p].free_page_num < ssd->parameter->page_block))
							{
								printf("%d %d %d %d %d,%5d,%5d\n", i, m, j, k, p, ssd->channel_head[i].chip_head[m].die_head[j].plane_head[k].blk_head[p].last_write_page, plane_cmplt);
								//getchar();
							}
						}
					}
				}
			}
		}*/

	}
	return ssd;
}



/**********************************************************************
*trace_output()��������ÿһ����������������󾭹�process()�����������
*��ӡ�����ص����н����outputfile�ļ��У�����Ľ����Ҫ�����е�ʱ��
**********************************************************************/
void trace_output(struct ssd_info* ssd){
	int flag = 1;	
	__int64 start_time, end_time;
	struct request *req, *pre_node;
	struct sub_request *sub, *tmp;
	unsigned int i;

#ifdef DEBUG
	printf("enter trace_output,  current time:%I64u\n",ssd->current_time);
#endif

	pre_node=NULL;
	req = ssd->request_queue;
	start_time = 0;
	end_time = 0;

	if(req == NULL)
		return;

	while(req != NULL)	
	{
		sub = req->subs;
		flag = 1;
		start_time = 0;
		end_time = 0;
		if (req->response_time != 0 && req->cmplt_flag == 1)
		{
			fprintf(ssd->outputfile, "%16lld %10d %6d %2d %16lld %16lld %10lld\n", req->time, req->lsn, req->size, req->operation, req->begin_time, req->response_time, req->response_time - req->time);
			fflush(ssd->outputfile);

			if (req->response_time - req->begin_time == 0)
			{
				printf("the response time is 0?? \n");
				getchar();
			}

			if (req->operation == READ)
			{
				ssd->read_request_count++;
				ssd->read_avg = ssd->read_avg + (req->response_time - req->time);
			}
			else
			{
				ssd->write_request_count++;
				ssd->write_avg = ssd->write_avg + (req->response_time - req->time);
			}

			if (pre_node == NULL)
			{
				if (req->next_node == NULL)
				{
					free(req->need_distr_flag);
					req->need_distr_flag = NULL;
					free(req);
					req = NULL;
					ssd->request_queue = NULL;
					ssd->request_tail = NULL;
					ssd->request_queue_length--;
				}
				else
				{
					ssd->request_queue = req->next_node;
					pre_node = req;
					req = req->next_node;
					free(pre_node->need_distr_flag);
					pre_node->need_distr_flag = NULL;
					free((void *)pre_node);
					pre_node = NULL;
					ssd->request_queue_length--;
				}
			}
			else
			{
				if (req->next_node == NULL)
				{
					pre_node->next_node = NULL;
					free(req->need_distr_flag);
					req->need_distr_flag = NULL;
					free(req);
					req = NULL;
					ssd->request_tail = pre_node;
					ssd->request_queue_length--;
				}
				else
				{
					pre_node->next_node = req->next_node;
					free(req->need_distr_flag);
					req->need_distr_flag = NULL;
					free((void *)req);
					req = pre_node->next_node;
					ssd->request_queue_length--;
				}
			}
		}
		else if (req->response_time == 0 && req->cmplt_flag == 1)
		{
			flag = 1;
			while (sub != NULL)
			{
				if ( (sub->lpn == 13992 | sub->lpn == 13991)  &&  (req->lsn == 91389))
					printf("lz\n");

				if (req->lsn == 91389)
					printf("lz\n");

				if (start_time == 0)
					start_time = sub->begin_time;
				if (start_time > sub->begin_time)
					start_time = sub->begin_time;
				if (end_time < sub->complete_time)
					end_time = sub->complete_time;

				/*
				if (trace_over_flag == 1)
				{
					compare_time = ssd->current_time * 10;

				}
				else
					compare_time = ssd->current_time;
				*/

				if ((sub->current_state == SR_COMPLETE) || ((sub->next_state == SR_COMPLETE) && (sub->next_state_predict_time <= ssd->current_time)))	// if any sub-request is not completed, the request is not completed
				{
					sub = sub->next_subs;
					if (end_time - start_time == 0)
					{
						printf("the response time is 0?? \n");
						getchar();
					}
				}
				else
				{
					flag = 0;
					break;
				}

			}

			if (flag == 1)
			{
				//fprintf(ssd->outputfile,"%10I64u %10u %6u %2u %16I64u %16I64u %10I64u\n",req->time,req->lsn, req->size, req->operation, start_time, end_time, end_time-req->time);
				fprintf(ssd->outputfile, "%16lld %10d %6d %2d %16lld %16lld %10lld\n", req->time, req->lsn, req->size, req->operation, start_time, end_time, end_time - req->time);
				fflush(ssd->outputfile);

				if (end_time - start_time == 0)
				{
					printf("the response time is 0?? \n");
					getchar();
				}

				if (req->operation == READ)
				{
					ssd->read_request_count++;
					ssd->read_avg = ssd->read_avg + (end_time - req->time);
				}
				else
				{
					ssd->write_request_count++;
					ssd->write_avg = ssd->write_avg + (end_time - req->time);
				}


				if (req->lsn == 91389 && req->size == 64)
				{
					printf("ERROR\n");
				}


				//������ִ����ɣ��ͷ�����������
				while (req->subs != NULL)
				{
					tmp = req->subs;
					req->subs = tmp->next_subs;
					if (tmp->update != NULL)
					{
						free(tmp->update->location);
						tmp->update->location = NULL;
						free(tmp->update);
						tmp->update = NULL;
					}
					free(tmp->location);
					tmp->location = NULL;
					free(tmp);
					tmp = NULL;
				}

				if (pre_node == NULL)
				{
					if (req->next_node == NULL)
					{
						free(req->need_distr_flag);
						req->need_distr_flag = NULL;
						free(req);
						req = NULL;
						ssd->request_queue = NULL;
						ssd->request_tail = NULL;
						ssd->request_queue_length--;
					}
					else
					{
						ssd->request_queue = req->next_node;
						pre_node = req;
						req = req->next_node;
						free(pre_node->need_distr_flag);
						pre_node->need_distr_flag = NULL;
						free(pre_node);
						pre_node = NULL;
						ssd->request_queue_length--;
					}
				}
				else
				{
					if (req->next_node == NULL)
					{
						pre_node->next_node = NULL;
						free(req->need_distr_flag);
						req->need_distr_flag = NULL;
						free(req);
						req = NULL;
						ssd->request_tail = pre_node;
						ssd->request_queue_length--;
					}
					else
					{
						pre_node->next_node = req->next_node;
						free(req->need_distr_flag);
						req->need_distr_flag = NULL;
						free(req);
						req = pre_node->next_node;
						ssd->request_queue_length--;
					}

				}
			}
			else
			{
				pre_node = req;
				req = req->next_node;
			}
		}
		else
		{
			pre_node = req;
			req = req->next_node;
		}
	}
}


/*******************************************************************************
*statistic_output()������Ҫ�����������һ����������ش�����Ϣ��
*1�������ÿ��plane�Ĳ���������plane_erase���ܵĲ���������erase
*2����ӡmin_lsn��max_lsn��read_count��program_count��ͳ����Ϣ���ļ�outputfile�С�
*3����ӡ��ͬ����Ϣ���ļ�statisticfile��
*******************************************************************************/
void statistic_output(struct ssd_info *ssd)
{
	unsigned int lpn_count=0,i,j,k,m,p,erase=0,plane_erase=0;
	unsigned int blk_read = 0, plane_read = 0;
	unsigned int blk_write = 0, plane_write = 0;
	unsigned int pre_plane_write = 0;
	double gc_energy=0.0;
#ifdef DEBUG
	printf("enter statistic_output,  current time:%I64u\n",ssd->current_time);
#endif

	for(i=0;i<ssd->parameter->channel_number;i++)
	{
		for (p = 0; p < ssd->parameter->chip_channel[i]; p++)
		{
			for (j = 0; j < ssd->parameter->die_chip; j++)
			{
				for (k = 0; k < ssd->parameter->plane_die; k++)
				{
					for (m = 0; m < ssd->parameter->block_plane; m++)
					{
						if (ssd->channel_head[i].chip_head[p].die_head[j].plane_head[k].blk_head[m].erase_count > 0)
						{
							ssd->channel_head[i].chip_head[p].die_head[j].plane_head[k].plane_erase_count += ssd->channel_head[i].chip_head[p].die_head[j].plane_head[k].blk_head[m].erase_count;
						}

						if (ssd->channel_head[i].chip_head[p].die_head[j].plane_head[k].blk_head[m].page_read_count > 0)
						{
							ssd->channel_head[i].chip_head[p].die_head[j].plane_head[k].plane_read_count += ssd->channel_head[i].chip_head[p].die_head[j].plane_head[k].blk_head[m].page_read_count;
						}

						if (ssd->channel_head[i].chip_head[p].die_head[j].plane_head[k].blk_head[m].page_write_count > 0)
						{
							ssd->channel_head[i].chip_head[p].die_head[j].plane_head[k].plane_program_count += ssd->channel_head[i].chip_head[p].die_head[j].plane_head[k].blk_head[m].page_write_count;
						}

						if (ssd->channel_head[i].chip_head[p].die_head[j].plane_head[k].blk_head[m].pre_write_count > 0)
						{
							ssd->channel_head[i].chip_head[p].die_head[j].plane_head[k].pre_plane_write_count += ssd->channel_head[i].chip_head[p].die_head[j].plane_head[k].blk_head[m].pre_write_count;
						}
					}
					fprintf(ssd->outputfile, "the %d channel, %d chip, %d die, %d plane has : ", i, p, j, k);
					fprintf(ssd->outputfile, "%3d erase operations,", ssd->channel_head[i].chip_head[p].die_head[j].plane_head[k].plane_erase_count);
					fprintf(ssd->outputfile, "%3d read operations,", ssd->channel_head[i].chip_head[p].die_head[j].plane_head[k].plane_read_count);
					fprintf(ssd->outputfile, "%3d write operations,", ssd->channel_head[i].chip_head[p].die_head[j].plane_head[k].plane_program_count);
					fprintf(ssd->outputfile, "%3d pre_process write operations\n", ssd->channel_head[i].chip_head[p].die_head[j].plane_head[k].pre_plane_write_count);
					
					fprintf(ssd->statisticfile, "the %d channel, %d chip, %d die, %d plane has : ", i, p, j, k);
					fprintf(ssd->statisticfile, "%3d erase operations,", ssd->channel_head[i].chip_head[p].die_head[j].plane_head[k].plane_erase_count);
					fprintf(ssd->statisticfile, "%3d read operations,", ssd->channel_head[i].chip_head[p].die_head[j].plane_head[k].plane_read_count);
					fprintf(ssd->statisticfile, "%3d write operations,", ssd->channel_head[i].chip_head[p].die_head[j].plane_head[k].plane_program_count);
					fprintf(ssd->statisticfile, "%3d pre_process write operations\n", ssd->channel_head[i].chip_head[p].die_head[j].plane_head[k].pre_plane_write_count);
				}
			}
		}
	}

	fprintf(ssd->outputfile,"\n");
	fprintf(ssd->outputfile,"\n");
	fprintf(ssd->outputfile,"---------------------------statistic data---------------------------\n");	 
	fprintf(ssd->outputfile,"min lsn: %13d\n",ssd->min_lsn);	
	fprintf(ssd->outputfile,"max lsn: %13d\n",ssd->max_lsn);
	fprintf(ssd->outputfile,"read count: %13d\n",ssd->read_count);	  
	fprintf(ssd->outputfile,"the read operation leaded by un-covered update count: %13d\n",ssd->update_read_count);
	fprintf(ssd->outputfile, "the read operation leaded by gc read count: %13d\n", ssd->gc_read_count);
	fprintf(ssd->outputfile, "\n");
	fprintf(ssd->outputfile, "program count: %13d\n", ssd->program_count);
	fprintf(ssd->outputfile, "the write operation leaded by pre_process write count: %13d\n", ssd->pre_all_write);
	fprintf(ssd->outputfile, "the write operation leaded by un-covered update count: %13d\n", ssd->update_write_count);
	fprintf(ssd->outputfile, "the write operation leaded by gc read count: %13d\n", ssd->gc_write_count);
	fprintf(ssd->outputfile, "\n");
	fprintf(ssd->outputfile,"erase count: %13d\n",ssd->erase_count);
	fprintf(ssd->outputfile,"direct erase count: %13d\n",ssd->direct_erase_count);
	//fprintf(ssd->outputfile,"copy back count: %13d\n",ssd->copy_back_count);
	fprintf(ssd->outputfile,"multi-plane program count: %13d\n",ssd->m_plane_prog_count);
	fprintf(ssd->outputfile,"multi-plane read count: %13d\n",ssd->m_plane_read_count);
	//fprintf(ssd->outputfile,"interleave write count: %13d\n",ssd->interleave_count);
	//fprintf(ssd->outputfile,"interleave read count: %13d\n",ssd->interleave_read_count);
	//fprintf(ssd->outputfile,"interleave two plane and one program count: %13d\n",ssd->inter_mplane_prog_count);
	//fprintf(ssd->outputfile,"interleave two plane count: %13d\n",ssd->inter_mplane_count);
	//fprintf(ssd->outputfile,"gc copy back count: %13d\n",ssd->gc_copy_back);
	fprintf(ssd->outputfile,"write flash count: %13d\n",ssd->write_flash_count);
	fprintf(ssd->outputfile, "\n");
	//fprintf(ssd->outputfile,"interleave erase count: %13d\n",ssd->interleave_erase_count);
	//fprintf(ssd->outputfile,"multiple plane erase count: %13d\n",ssd->mplane_erase_conut);
	//fprintf(ssd->outputfile,"interleave multiple plane erase count: %13d\n",ssd->interleave_mplane_erase_count);
	fprintf(ssd->outputfile,"read request count: %13d\n",ssd->read_request_count);
	fprintf(ssd->outputfile,"write request count: %13d\n",ssd->write_request_count);
	fprintf(ssd->outputfile, "\n");
	fprintf(ssd->outputfile,"read request average size: %13f\n",ssd->ave_read_size);
	fprintf(ssd->outputfile,"write request average size: %13f\n",ssd->ave_write_size);
	fprintf(ssd->outputfile, "\n");
//	fprintf(ssd->outputfile,"read request average response time: %16I64u\n",ssd->read_avg/ssd->read_request_count);
	fprintf(ssd->outputfile,"write request average response time: %16I64u\n",ssd->write_avg/ssd->write_request_count);
	fprintf(ssd->outputfile, "\n");
	fprintf(ssd->outputfile,"buffer read hits: %13d\n",ssd->dram->buffer->read_hit);
	fprintf(ssd->outputfile,"buffer read miss: %13d\n",ssd->dram->buffer->read_miss_hit);
	fprintf(ssd->outputfile,"buffer write hits: %13d\n",ssd->dram->buffer->write_hit);
	fprintf(ssd->outputfile,"buffer write miss: %13d\n",ssd->dram->buffer->write_miss_hit);
	fprintf(ssd->outputfile, "\n");
	fflush(ssd->outputfile);

	fclose(ssd->outputfile);


	fprintf(ssd->statisticfile, "\n");
	fprintf(ssd->statisticfile, "\n");
	fprintf(ssd->statisticfile, "---------------------------statistic data---------------------------\n");
	fprintf(ssd->statisticfile, "min lsn: %13d\n", ssd->min_lsn);
	fprintf(ssd->statisticfile, "max lsn: %13d\n", ssd->max_lsn);
	fprintf(ssd->statisticfile, "read count: %13d\n", ssd->read_count);
	fprintf(ssd->statisticfile, "the read operation leaded by un-covered update count: %13d\n", ssd->update_read_count);
	fprintf(ssd->statisticfile, "the read operation leaded by gc read count: %13d\n", ssd->gc_read_count);
	fprintf(ssd->statisticfile, "\n");
	fprintf(ssd->statisticfile, "program count: %13d\n", ssd->program_count);
	fprintf(ssd->statisticfile, "the write operation leaded by pre_process write count: %13d\n", ssd->pre_all_write);
	fprintf(ssd->statisticfile, "the write operation leaded by un-covered update count: %13d\n", ssd->update_write_count);
	fprintf(ssd->statisticfile, "the write operation leaded by gc read count: %13d\n", ssd->gc_write_count);
	fprintf(ssd->statisticfile, "\n");
	fprintf(ssd->statisticfile,"erase count: %13d\n",ssd->erase_count);	  
	fprintf(ssd->statisticfile,"direct erase count: %13d\n",ssd->direct_erase_count);
	fprintf(ssd->statisticfile, "\n");
	//fprintf(ssd->statisticfile,"copy back count: %13d\n",ssd->copy_back_count);
	fprintf(ssd->statisticfile,"multi-plane program count: %13d\n",ssd->m_plane_prog_count);
	fprintf(ssd->statisticfile,"multi-plane read count: %13d\n",ssd->m_plane_read_count);
	fprintf(ssd->statisticfile, "\n");
	//fprintf(ssd->statisticfile,"interleave count: %13d\n",ssd->interleave_count);
	//fprintf(ssd->statisticfile,"interleave read count: %13d\n",ssd->interleave_read_count);
	//fprintf(ssd->statisticfile,"interleave two plane and one program count: %13d\n",ssd->inter_mplane_prog_count);
	//fprintf(ssd->statisticfile,"interleave two plane count: %13d\n",ssd->inter_mplane_count);
	//fprintf(ssd->statisticfile,"gc copy back count: %13d\n",ssd->gc_copy_back);
	fprintf(ssd->statisticfile,"write flash count: %13d\n",ssd->write_flash_count);
	fprintf(ssd->statisticfile, "\n");
	//fprintf(ssd->statisticfile,"waste page count: %13d\n",ssd->waste_page_count);
	//fprintf(ssd->statisticfile,"interleave erase count: %13d\n",ssd->interleave_erase_count);
	//fprintf(ssd->statisticfile,"multiple plane erase count: %13d\n",ssd->mplane_erase_conut);
	//fprintf(ssd->statisticfile,"interleave multiple plane erase count: %13d\n",ssd->interleave_mplane_erase_count);
	fprintf(ssd->statisticfile,"read request count: %13d\n",ssd->read_request_count);
	fprintf(ssd->statisticfile, "write request count: %13d\n", ssd->write_request_count);
	fprintf(ssd->statisticfile, "\n");
	fprintf(ssd->statisticfile,"read request average size: %13f\n",ssd->ave_read_size);
	fprintf(ssd->statisticfile,"write request average size: %13f\n",ssd->ave_write_size);
	fprintf(ssd->statisticfile, "\n");
//	fprintf(ssd->statisticfile,"read request average response time: %16I64u\n",ssd->read_avg/ssd->read_request_count);
	fprintf(ssd->statisticfile,"write request average response time: %16I64u\n",ssd->write_avg/ssd->write_request_count);
	fprintf(ssd->statisticfile, "\n");
	fprintf(ssd->statisticfile,"buffer read hits: %13d\n",ssd->dram->buffer->read_hit);
	fprintf(ssd->statisticfile,"buffer read miss: %13d\n",ssd->dram->buffer->read_miss_hit);
	fprintf(ssd->statisticfile,"buffer write hits: %13d\n",ssd->dram->buffer->write_hit);
	fprintf(ssd->statisticfile,"buffer write miss: %13d\n",ssd->dram->buffer->write_miss_hit);

//	fprintf(ssd->statisticfile, "buffer write hit request count : %13d\n", request_lz_count);

	fprintf(ssd->statisticfile, "\n");
	fflush(ssd->statisticfile);

	fclose(ssd->statisticfile);
}




/***********************************************
*free_all_node()���������þ����ͷ���������Ľڵ�
************************************************/
void free_all_node(struct ssd_info *ssd)
{
	unsigned int i,j,k,l,n;
	struct buffer_group *pt=NULL;
	struct direct_erase * erase_node=NULL;
	for (i=0;i<ssd->parameter->channel_number;i++)
	{
		for (j=0;j<ssd->parameter->chip_channel[0];j++)
		{
			for (k=0;k<ssd->parameter->die_chip;k++)
			{
				for (l=0;l<ssd->parameter->plane_die;l++)
				{
					for (n=0;n<ssd->parameter->block_plane;n++)
					{
						free(ssd->channel_head[i].chip_head[j].die_head[k].plane_head[l].blk_head[n].page_head);
						ssd->channel_head[i].chip_head[j].die_head[k].plane_head[l].blk_head[n].page_head=NULL;
					}
					free(ssd->channel_head[i].chip_head[j].die_head[k].plane_head[l].blk_head);
					ssd->channel_head[i].chip_head[j].die_head[k].plane_head[l].blk_head=NULL;
					while(ssd->channel_head[i].chip_head[j].die_head[k].plane_head[l].erase_node!=NULL)
					{
						erase_node=ssd->channel_head[i].chip_head[j].die_head[k].plane_head[l].erase_node;
						ssd->channel_head[i].chip_head[j].die_head[k].plane_head[l].erase_node=erase_node->next_node;
						free(erase_node);
						erase_node=NULL;
					}
				}
				
				free(ssd->channel_head[i].chip_head[j].die_head[k].plane_head);
				ssd->channel_head[i].chip_head[j].die_head[k].plane_head=NULL;
			}
			free(ssd->channel_head[i].chip_head[j].die_head);
			ssd->channel_head[i].chip_head[j].die_head=NULL;
		}
		free(ssd->channel_head[i].chip_head);
		ssd->channel_head[i].chip_head=NULL;
	}
	free(ssd->channel_head);
	ssd->channel_head=NULL;

	avlTreeDestroy( ssd->dram->buffer);
	ssd->dram->buffer=NULL;
	
	free(ssd->dram->map->map_entry);
	ssd->dram->map->map_entry=NULL;
	free(ssd->dram->map);
	ssd->dram->map=NULL;
	free(ssd->dram);
	ssd->dram=NULL;
	free(ssd->parameter);
	ssd->parameter=NULL;

	free(ssd);
	ssd=NULL;
}


/*****************************************************************************
*make_aged()���������þ���ģ����ʵ���ù�һ��ʱ���ssd��
*��ô���ssd����Ӧ�Ĳ�����Ҫ�ı䣬�����������ʵ���Ͼ��Ƕ�ssd�и��������ĸ�ֵ��
******************************************************************************/
struct ssd_info *make_aged(struct ssd_info *ssd)
{
	unsigned int i,j,k,l,m,n,ppn;
	int threshould,flag=0;
    
	if (ssd->parameter->aged==1)
	{
		//threshold��ʾһ��plane���ж���ҳ��Ҫ��ǰ��ΪʧЧ
		threshould=(int)(ssd->parameter->block_plane*ssd->parameter->page_block*ssd->parameter->aged_ratio);  
		for (i=0;i<ssd->parameter->channel_number;i++)
			for (j=0;j<ssd->parameter->chip_channel[i];j++)
				for (k=0;k<ssd->parameter->die_chip;k++)
					for (l=0;l<ssd->parameter->plane_die;l++)
					{  
						flag=0;
						for (m=0;m<ssd->parameter->block_plane;m++)
						{  
							if (flag>=threshould)
							{
								break;
							}
							//ע������ɻ���+1����ʾ��������˾ɻ���������ʣ��һ��ȫ�����е�block.
							for (n=0;n<(ssd->parameter->page_block*ssd->parameter->aged_ratio+1);n++)
							{  
								ssd->channel_head[i].chip_head[j].die_head[k].plane_head[l].blk_head[m].page_head[n].valid_state=0;        //��ʾĳһҳʧЧ��ͬʱ���valid��free״̬��Ϊ0
								ssd->channel_head[i].chip_head[j].die_head[k].plane_head[l].blk_head[m].page_head[n].free_state=0;         //��ʾĳһҳʧЧ��ͬʱ���valid��free״̬��Ϊ0
								ssd->channel_head[i].chip_head[j].die_head[k].plane_head[l].blk_head[m].page_head[n].lpn=0;  //��valid_state free_state lpn����Ϊ0��ʾҳʧЧ������ʱ�������⣬����lpn=0��������Чҳ
								ssd->channel_head[i].chip_head[j].die_head[k].plane_head[l].blk_head[m].free_page_num--;
								ssd->channel_head[i].chip_head[j].die_head[k].plane_head[l].blk_head[m].invalid_page_num++;
								ssd->channel_head[i].chip_head[j].die_head[k].plane_head[l].blk_head[m].last_write_page++;
								ssd->channel_head[i].chip_head[j].die_head[k].plane_head[l].free_page--;
								flag++;

								ppn=find_ppn(ssd,i,j,k,l,m,n);
							
							}
							make_age_free_page = ssd->channel_head[i].chip_head[j].die_head[k].plane_head[l].blk_head[m].free_page_num;
						} 
					}	 
	}  
	else
	{
		return ssd;
	}

	return ssd;
}



/*********************************************************************************************
*����Ч���е�free_pageȫ����Ϊ��Ч����֤���һ����Ч���а�����free page,����ʵ��ssd�Ļ���
*�������еĿ飬����Ч���а����оɻ�������free_page��ʱ�򣬽����е�free_page����Ч��������
*Ч�����gc����
*********************************************************************************************/
struct ssd_info *pre_process_write(struct ssd_info *ssd)
{
	unsigned  int i, j, k, p, m, n;
	struct direct_erase *direct_erase_node, *new_direct_erase;

	for (i = 0; i<ssd->parameter->channel_number; i++)
	{
		for (m = 0; m < ssd->parameter->chip_channel[i]; m++)
		{
			for (j = 0; j < ssd->parameter->die_chip; j++)
			{
				for (k = 0; k < ssd->parameter->plane_die; k++)
				{
					//�鿴�Ƿ��п��п�
					for (p = 0; p < ssd->parameter->block_plane; p++)
					{
						//if ((ssd->channel_head[i].chip_head[m].die_head[j].plane_head[k].blk_head[p].free_page_num == make_age_free_page) && (ssd->channel_head[i].chip_head[m].die_head[j].plane_head[k].blk_head[p].free_page_num < ssd->parameter->page_block))
						if ((ssd->channel_head[i].chip_head[m].die_head[j].plane_head[k].blk_head[p].free_page_num > 0) && (ssd->channel_head[i].chip_head[m].die_head[j].plane_head[k].blk_head[p].free_page_num < ssd->parameter->page_block))
						{
							if (ssd->channel_head[i].chip_head[m].die_head[j].plane_head[k].blk_head[p].free_page_num == make_age_free_page)
							{
								ssd->channel_head[i].chip_head[m].die_head[j].plane_head[k].free_page = ssd->channel_head[i].chip_head[m].die_head[j].plane_head[k].free_page - ssd->channel_head[i].chip_head[m].die_head[j].plane_head[k].blk_head[p].free_page_num;
								ssd->channel_head[i].chip_head[m].die_head[j].plane_head[k].blk_head[p].free_page_num = 0;
								ssd->channel_head[i].chip_head[m].die_head[j].plane_head[k].blk_head[p].invalid_page_num = ssd->parameter->page_block;
								ssd->channel_head[i].chip_head[m].die_head[j].plane_head[k].blk_head[p].last_write_page = ssd->parameter->page_block - 1;

								for (n = 0; n < ssd->parameter->page_block; n++)
								{
									ssd->channel_head[i].chip_head[m].die_head[j].plane_head[k].blk_head[p].page_head[n].valid_state = 0;        //��ʾĳһҳʧЧ��ͬʱ���valid��free״̬��Ϊ0
									ssd->channel_head[i].chip_head[m].die_head[j].plane_head[k].blk_head[p].page_head[n].free_state = 0;         //��ʾĳһҳʧЧ��ͬʱ���valid��free״̬��Ϊ0
									ssd->channel_head[i].chip_head[m].die_head[j].plane_head[k].blk_head[p].page_head[n].lpn = 0;  //��valid_state free_state lpn����Ϊ0��ʾҳʧЧ������ʱ�������⣬����lpn=0��������Чҳ
								}
								//����ҳ����Чҳ����Ҫ����block��Ч�� ����ӵ�gc����

								new_direct_erase = (struct direct_erase *)malloc(sizeof(struct direct_erase));
								alloc_assert(new_direct_erase, "new_direct_erase");
								memset(new_direct_erase, 0, sizeof(struct direct_erase));

								new_direct_erase->block = p;  //������ǰ��Ч��Ŀ��
								new_direct_erase->next_node = NULL;
								direct_erase_node = ssd->channel_head[i].chip_head[m].die_head[j].plane_head[k].erase_node;
								if (direct_erase_node == NULL)
								{
									ssd->channel_head[i].chip_head[m].die_head[j].plane_head[k].erase_node = new_direct_erase;
								}
								else
								{
									new_direct_erase->next_node = ssd->channel_head[i].chip_head[m].die_head[j].plane_head[k].erase_node;
									ssd->channel_head[i].chip_head[m].die_head[j].plane_head[k].erase_node = new_direct_erase;
								}
							}
							else
							{
								for (n = (ssd->channel_head[i].chip_head[m].die_head[j].plane_head[k].blk_head[p].last_write_page + 1); n < ssd->parameter->page_block; n++)
								{
									ssd->channel_head[i].chip_head[m].die_head[j].plane_head[k].blk_head[p].page_head[n].valid_state = 0;        //��ʾĳһҳʧЧ��ͬʱ���valid��free״̬��Ϊ0
									ssd->channel_head[i].chip_head[m].die_head[j].plane_head[k].blk_head[p].page_head[n].free_state = 0;         //��ʾĳһҳʧЧ��ͬʱ���valid��free״̬��Ϊ0
									ssd->channel_head[i].chip_head[m].die_head[j].plane_head[k].blk_head[p].page_head[n].lpn = 0;  //��valid_state free_state lpn����Ϊ0��ʾҳʧЧ������ʱ�������⣬����lpn=0��������Чҳ


									ssd->channel_head[i].chip_head[m].die_head[j].plane_head[k].free_page = ssd->channel_head[i].chip_head[m].die_head[j].plane_head[k].free_page - ssd->channel_head[i].chip_head[m].die_head[j].plane_head[k].blk_head[p].free_page_num;
									ssd->channel_head[i].chip_head[m].die_head[j].plane_head[k].blk_head[p].free_page_num = 0;
									ssd->channel_head[i].chip_head[m].die_head[j].plane_head[k].blk_head[p].invalid_page_num = ssd->parameter->page_block;
									ssd->channel_head[i].chip_head[m].die_head[j].plane_head[k].blk_head[p].last_write_page = ssd->parameter->page_block - 1;


								}
							}
						}
						//printf("%d,0,%d,%d,%d:%5d\n", i, j, k, p, ssd->channel_head[i].chip_head[m].die_head[j].plane_head[k].blk_head[p].free_page_num);
					}
				}
			}
		}
	}
return ssd;
}


/************************************************
*����,�����ļ�ʧ��ʱ�������open �ļ��� error��
*************************************************/
void file_assert(int error, char *s)
{
	if (error == 0) return;
	printf("open %s error\n", s);
	getchar();
	exit(-1);
}

/*****************************************************
*����,�������ڴ�ռ�ʧ��ʱ�������malloc ������ error��
******************************************************/
void alloc_assert(void *p, char *s)//����
{
	if (p != NULL) return;
	printf("malloc %s error\n", s);
	getchar();
	exit(-1);
}

/*********************************************************************************
*����
*A��������time_t��device��lsn��size��ope��<0ʱ�������trace error:.....��
*B��������time_t��device��lsn��size��ope��=0ʱ�������probable read a blank line��
**********************************************************************************/
void trace_assert(_int64 time_t, int device, unsigned int lsn, int size, int ope)//����
{
	if (time_t <0 || device < 0 || lsn < 0 || size < 0 || ope < 0)
	{
		printf("trace error:%I64u %d %d %d %d\n", time_t, device, lsn, size, ope);
		getchar();
		exit(-1);
	}
	if (time_t == 0 && device == 0 && lsn == 0 && size == 0 && ope == 0)
	{
		printf("probable read a blank line\n");
		getchar();
	}
}