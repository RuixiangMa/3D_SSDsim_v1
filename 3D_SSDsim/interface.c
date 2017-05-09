/*****************************************************************************************************************************
This is a project on 3D_SSDsim, based on ssdsim under the framework of the completion of structures, the main function:
1.Support for 3D commands, for example:mutli plane\interleave\copyback\program suspend/Resume..etc
2.Multi - level parallel simulation
3.Clear hierarchical interface
4.4-layer structure

FileName�� ssd.c
Author: Zuo Lu 		Version: 1.0	Date:2017/04/06
Description: 
Interface layer: to complete the IO request to obtain, and converted into the corresponding page-level SSD request

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

extern int buffer_full_flag ;
extern int trace_over_flag ;


/********    get_request    ******************************************************
*	1.get requests that arrived already
*	2.add those request node to ssd->reuqest_queue
*	return	0: reach the end of the trace
*			-1: no request has been added
*			1: add one request to list
*SSDģ����������������ʽ:ʱ������(��ȷ��̫��) �¼�����(���������) trace����()��
*���ַ�ʽ�ƽ��¼���channel/chip״̬�ı䡢trace�ļ�����ﵽ��
*channel/chip״̬�ı��trace�ļ����󵽴���ɢ����ʱ�����ϵĵ㣬ÿ�δӵ�ǰ״̬����
*��һ��״̬��Ҫ���������һ��״̬��ÿ����һ����ִ��һ��process
********************************************************************************/
int get_requests(struct ssd_info *ssd)
{
	char buffer[200];
	unsigned int lsn = 0;
	int device, size, ope, large_lsn, i = 0, j = 0;
	struct request *request1;
	int flag = 1;
	long filepoint;
	__int64 time_t;
	__int64 nearest_event_time;

	extern __int64 request_lz_count;

#ifdef DEBUG
	printf("enter get_requests,  current time:%I64u\n", ssd->current_time);
#endif
	
	if (trace_over_flag == 1)
		return 0;

	/*
	filepoint = ftell(ssd->tracefile);
	fgets(buffer, 200, ssd->tracefile);
	sscanf(buffer, "%I64u %d %d %d %d", &time_t, &device, &lsn, &size, &ope);
	*/

	while (TRUE)
	{
		filepoint = ftell(ssd->tracefile);
		fgets(buffer, 200, ssd->tracefile);
		sscanf(buffer, "%I64u %d %d %d %d", &time_t, &device, &lsn, &size, &ope);

		if (size < (ssd->parameter->dram_capacity / 512))
			break;

		if (feof(ssd->tracefile))      //�ж��Ƿ��������trace,��������������ѭ��
			break;
	}

	if ((device<0) && (lsn<0) && (size<0) && (ope<0))
	{
		return 100;
	}
	if (lsn<ssd->min_lsn)
		ssd->min_lsn = lsn;
	if (lsn>ssd->max_lsn)
		ssd->max_lsn = lsn;

	/******************************************************************************************************
	*�ϲ��ļ�ϵͳ���͸�SSD���κζ�д��������������֣�LSN��size�� LSN���߼������ţ������ļ�ϵͳ���ԣ����������Ĵ�
	*���ռ���һ�����Ե������ռ䡣���磬������260��6����ʾ������Ҫ��ȡ��������Ϊ260���߼�������ʼ���ܹ�6��������
	*large_lsn: channel�����ж��ٸ�subpage�������ٸ�sector��overprovideϵ����SSD�в��������еĿռ䶼���Ը��û�ʹ�ã�
	*����32G��SSD������10%�Ŀռ䱣�������������ã����Գ���1-provide
	***********************************************************************************************************/
	large_lsn = (int)((ssd->parameter->subpage_page*ssd->parameter->page_block*ssd->parameter->block_plane*ssd->parameter->plane_die*ssd->parameter->die_chip*ssd->parameter->chip_num)*(1 - ssd->parameter->overprovide));
	lsn = lsn%large_lsn;

	nearest_event_time = find_nearest_event(ssd);

	if (nearest_event_time == 0x7fffffffffffffff)
	{
		ssd->current_time = time_t;
		if (buffer_full_flag == 1)
		{
			fseek(ssd->tracefile, filepoint, 0);
			return -1;
		}
		else if (ssd->request_queue_length >= ssd->parameter->queue_length)
		{
			fseek(ssd->tracefile, filepoint, 0);
			return 0;
		}
	}
	else
	{
		if ( (nearest_event_time<time_t) || (buffer_full_flag == 1))
		{
			/*******************************************************************************
			*�ع��������û�а�time_t����ssd->current_time����trace�ļ��Ѷ���һ����¼�ع�
			*filepoint��¼��ִ��fgets֮ǰ���ļ�ָ��λ�ã��ع����ļ�ͷ+filepoint��
			*int fseek(FILE *stream, long offset, int fromwhere);���������ļ�ָ��stream��λ�á�
			*���ִ�гɹ���stream��ָ����fromwhere��ƫ����ʼλ�ã��ļ�ͷ0����ǰλ��1���ļ�β2��Ϊ��׼��
			*ƫ��offset��ָ��ƫ���������ֽڵ�λ�á����ִ��ʧ��(����offset�����ļ������С)���򲻸ı�streamָ���λ�á�
			*�ı��ļ�ֻ�ܲ����ļ�ͷ0�Ķ�λ��ʽ���������д��ļ���ʽ��"r":��ֻ����ʽ���ı��ļ�
			**********************************************************************************/
			fseek(ssd->tracefile, filepoint, 0);
			if (ssd->current_time <= nearest_event_time)
				ssd->current_time = nearest_event_time;
			return -1;
		}
		else
		{
			//�������������󳬹����л���buff������
			if ( (ssd->request_queue_length >= ssd->parameter->queue_length)  ||  (buffer_full_flag == 1) )
			{
				fseek(ssd->tracefile, filepoint, 0);
				ssd->current_time = nearest_event_time;
				return -1;
			}
			else
			{
				ssd->current_time = time_t;
			}
		}
	}

	

	if (time_t < 0)
	{
		printf("error!\n");
		while (1){}
	}


	if (feof(ssd->tracefile))      //�ж��Ƿ��������trace
	{
		request1 = NULL;
		trace_over_flag = 1;
		return 0;
	}

	request1 = (struct request*)malloc(sizeof(struct request));
	alloc_assert(request1, "request");
	memset(request1, 0, sizeof(struct request));

	request1->time = time_t;
	request1->lsn = lsn;
	request1->size = size;
	request1->operation = ope;
	request1->begin_time = time_t;
	request1->response_time = 0;
	request1->energy_consumption = 0;
	request1->next_node = NULL;
	request1->distri_flag = 0;              // indicate whether this request has been distributed already
	request1->subs = NULL;
	request1->need_distr_flag = NULL;
	request1->complete_lsn_count = 0;         //record the count of lsn served by buffer
	filepoint = ftell(ssd->tracefile);		// set the file point

	if (ssd->request_queue == NULL)          //The queue is empty
	{
		ssd->request_queue = request1;
		ssd->request_tail = request1;
		ssd->request_work = request1;
		ssd->request_queue_length++;
	}
	else
	{
		(ssd->request_tail)->next_node = request1;
		ssd->request_tail = request1;
		if (ssd->request_work == NULL)
			ssd->request_work = request1;
		ssd->request_queue_length++;
	}

	request_lz_count++;
	printf("request:%I64u\n", request_lz_count);
	//printf("%d\n", ssd->request_queue_length);

	/*
	if (request_lz_count == 3698863)
		printf("lz\n");
	*/

	if (request1->operation == 1)             //����ƽ�������С 1Ϊ�� 0Ϊд
	{
		ssd->ave_read_size = (ssd->ave_read_size*ssd->read_request_count + request1->size) / (ssd->read_request_count + 1);
	}
	else
	{
		ssd->ave_write_size = (ssd->ave_write_size*ssd->write_request_count + request1->size) / (ssd->write_request_count + 1);
	}


	filepoint = ftell(ssd->tracefile);
	fgets(buffer, 200, ssd->tracefile);    //Ѱ����һ������ĵ���ʱ��
	sscanf(buffer, "%I64u %d %d %d %d", &time_t, &device, &lsn, &size, &ope);
	ssd->next_request_time = time_t;
	fseek(ssd->tracefile, filepoint, 0);

	return 1;
}


/**********************************************************************************************************
*__int64 find_nearest_event(struct ssd_info *ssd)
*Ѱ����������������絽����¸�״̬ʱ��,���ȿ��������һ��״̬ʱ�䣬���������¸�״̬ʱ��С�ڵ��ڵ�ǰʱ�䣬
*˵��������������Ҫ�鿴channel���߶�Ӧdie����һ״̬ʱ�䡣Int64���з��� 64 λ�����������ͣ�ֵ���ͱ�ʾֵ����
*-2^63 ( -9,223,372,036,854,775,808)��2^63-1(+9,223,372,036,854,775,807 )֮����������洢�ռ�ռ 8 �ֽڡ�
*channel,die���¼���ǰ�ƽ��Ĺؼ����أ������������ʹ�¼�������ǰ�ƽ���channel��die�ֱ�ص�idle״̬��die�е�
*������׼������
***********************************************************************************************************/
__int64 find_nearest_event(struct ssd_info *ssd)
{
	unsigned int i, j;
	__int64 time = 0x7fffffffffffffff;
	__int64 time1 = 0x7fffffffffffffff;
	__int64 time2 = 0x7fffffffffffffff;

	for (i = 0; i<ssd->parameter->channel_number; i++)
	{
		if (ssd->channel_head[i].next_state == CHANNEL_IDLE)
			if (time1>ssd->channel_head[i].next_state_predict_time)
				if (ssd->channel_head[i].next_state_predict_time>ssd->current_time)
					time1 = ssd->channel_head[i].next_state_predict_time;
		for (j = 0; j<ssd->parameter->chip_channel[i]; j++)
		{
			if ((ssd->channel_head[i].chip_head[j].next_state == CHIP_IDLE) || (ssd->channel_head[i].chip_head[j].next_state == CHIP_DATA_TRANSFER))
				if (time2>ssd->channel_head[i].chip_head[j].next_state_predict_time)
					if (ssd->channel_head[i].chip_head[j].next_state_predict_time>ssd->current_time)
						time2 = ssd->channel_head[i].chip_head[j].next_state_predict_time;
		}
	}

	/*****************************************************************************************************
	*timeΪ���� A.��һ״̬ΪCHANNEL_IDLE����һ״̬Ԥ��ʱ�����ssd��ǰʱ���CHANNEL����һ״̬Ԥ��ʱ��
	*           B.��һ״̬ΪCHIP_IDLE����һ״̬Ԥ��ʱ�����ssd��ǰʱ���DIE����һ״̬Ԥ��ʱ��
	*		     C.��һ״̬ΪCHIP_DATA_TRANSFER����һ״̬Ԥ��ʱ�����ssd��ǰʱ���DIE����һ״̬Ԥ��ʱ��
	*CHIP_DATA_TRANSFER��׼����״̬�������Ѵӽ��ʴ�����register����һ״̬�Ǵ�register����buffer�е���Сֵ
	*ע����ܶ�û������Ҫ���time����ʱtime����0x7fffffffffffffff ��
	*****************************************************************************************************/
	time = (time1>time2) ? time2 : time1;
	return time;
}