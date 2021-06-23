#pragma once

#include "Common.h"

//�����ThreadCache����û�еĻ���Ҫ�����Ļ�ȡ

/*
������Դ�ľ��⣬����ThreadCache��ĳ����Դ��ʣ��ʱ�򣬿��Ի���ThreadCache�ڲ��ĵ��ڴ�
�Ӷ����Է����������ThreadCache
ֻ��һ�����Ļ��棬�������е��߳�����ȡ�ڴ��ʱ��Ӧ����һ�����Ļ���
���Զ������Ļ������ʹ�õ���ģʽ�����д������Ļ������
�������Ļ�����˵Ҫ����
*/

//��Ƴɵ���ģʽ
class CentralCache
{
public:
	static CentralCache* Getinstence()
	{
		return &_inst;
	}

	//��page cache��ȡһ��span
	Span* GetOneSpan(SpanList& spanlist, size_t byte_size);

	//�����Ļ����ȡһ�������Ķ����threa cache
	size_t FetchRangeObj(void*& start, void*& end, size_t n, size_t byte_size);

	//��һ�������Ķ����ͷŸ�span���
	void ReleaseListToSpans(void* start, size_t size);

private:
	SpanList _spanlist[NLISTS];//184��SpanList������ �ڴ����� ��SpanList

private:
	CentralCache(){}//private�У���ֹĬ�Ϲ��죬�Լ�����

	CentralCache(CentralCache&) = delete;//��������
	static CentralCache _inst;
};


/*

	VirtualAlloc(0, npage << PAGE_SHIFT, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	������ 
*/

/*
Central Cache��������һ����ϣӳ���Span��������˫�������ɡ�
_spanlist[NLISTS] ��ͬ�� ҳ���棬�ǰ����ڴ������С���õĹ�ϣ��
������Span��˫���ͷѭ��������㣬��С4K��_��һ���ڴ�������еڶ�������ĵ�ַ��
span�� _list ��ʾ��ǰspan�п��е��ڴ�������׵�ַ����һ���� _pageid<<12 ��ȡ�

�ڴ�����ô�������(��thread cache)��
	��Ҫ byte_size: һ���ڴ�����Ĵ�С n: �ڴ�����ĸ�����������������ڴ���β��ַ void*& start, void*& end��
	ȷ����Ӧ�ڴ������ SpanList����������
		size_t index = SizeClass::Index(byte_size);
		SpanList& spanlist = _spanlist[index]; 
		std::unique_lock<std::mutex> lock(spanlist._mutex);
	1.��ô���Ӧ��spanlist��ȡһ��span
		Span* span = spanlist.Begin();
		while (span != spanlist.End()){		//��ǰ�ҵ�һ��span
			if (span->_list != nullptr)
				return span;
			else
				span = span->_next;
		}
	2.�����Ӧ��spanlist��û��span���Ǿ��� ҳ���� ���롣 byte_size ������С
	��Ϊ���Ļ����еĶ����Ѿ��ֺ��ˣ����Բ�����ҳ����һ����������spanlist
	�ȼ���Ҫ�������ҳ����Ϊҳ����ĵ�λ��С��ҳ����
		int page_num = SizeClass::NumMovePage(byte_size);
		Span* newspan = PageCache::GetInstence()->NewSpan(page_num);//newspanֻ��ʼ����_pageid _npage
	��spanҳ�зֳ���Ҫ�Ķ�����������
		char* cur = (char*)(newspan->_pageid << PAGE_SHIFT);	//��ΪҪ��byte_size���㣬��Ҫ����ָ����ֵλ����ת����(char*)
		char* end = cur + (newspan->_npage << PAGE_SHIFT);
		newspan->_list = cur;
		newspan->_objsize = byte_size;	
		while (cur + byte_size < end){ //�Ѿ�֪�������յ㣬�и�span,��������
			char* next = cur + byte_size;
			NEXT_OBJ(cur) = next;
			cur = next;
		}
		NEXT_OBJ(cur) = nullptr;//span����ĩ��û����һ��obj
		_spanlist[byte_size].PushFront(newspan);	//������һ������ǰ������ȡ��
		return newspan;
	3.��ö�Ӧ��span�󣬰���threadcache��Ҫ�Ķ������n��ȷ�� ��βָ��
	ע��Ҫ��¼���span�е�_usecount���������span�б�ʹ�õĶ������
		size_t batchsize = 0;	//ʵ�ʷ��͵Ķ��������һ����n��batchsize������Ǽ�¼ʵ�ʷ��͵Ķ������
		void* prev = nullptr;	//��ǰ����ǰһ��
		void* cur = span->_list;//��cur�������������ߣ�pan->_list ��ʾ���span�����ڴ���׵�ַ
		for (size_t i = 0; i < n; ++i){
			prev = cur;
			cur = NEXT_OBJ(cur);
			++batchsize;
			if (cur == nullptr)//��ʱ�ж�cur�Ƿ�Ϊ�գ�Ϊ�յĻ�����ǰֹͣ
				break;
		} //prev ������Ҫ�����ߵ� end�� cur ��spanlist�µ�ͷ
		start = span->_list;
		end = prev;
		span->_list = cur;
		span->_usecount += batchsize;	
		if (span->_list == nullptr){ //�����span���������
			spanlist.Erase(span);
			spanlist.PushBack(span);
		}
		return batchsize;

�ڴ���������ô���ͷŻ����ģ�(һ���ͷţ��϶���һ����freelist���ͷ���)
	��Ҫ֪��void* start, size_t size���ͷŶ�����׵�ַ �� ���������С
	����ȷ����Ӧ��spanlist
		size_t index = SizeClass::Index(size);
		SpanList& spanlist = _spanlist[index];
		std::unique_lock<std::mutex> lock(spanlist._mutex);
	֮��һ������һ������طŵ���Ӧ��span֮��
		void* next = NEXT_OBJ(start);
		Span* span = PageCache::GetInstence()->MapObjectToSpan(start);
		NEXT_OBJ(start) = span->_list;		//����Ĳ��뷽ʽ
		span->_list = start;
		if (--span->_usecount == 0) {		//һ��span�ڵ��ڴ�飬û�б��߳�ռ�õģ��ͻ���pageCache
			spanlist.Erase(span);
			PageCache::GetInstence()->ReleaseSpanToPageCache(span);//�ͷ�span��pagecache
		}
		start = next;

*/