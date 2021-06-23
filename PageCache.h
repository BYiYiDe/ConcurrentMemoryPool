#pragma once

#include "Common.h"

//����Page CacheҲҪ����Ϊ����������Central Cache��ȡspan��ʱ��
//ÿ�ζ��Ǵ�ͬһ��page�����л�ȡspan
//����ģʽ ����ģʽ�������̰߳�ȫ
class PageCache
{
public:
	static PageCache* GetInstence(){
		return &_inst;
	}

	Span* AllocBigPageObj(size_t size);
	void FreeBigPageObj(void* ptr, Span* span);

	Span* _NewSpan(size_t n);
	Span* NewSpan(size_t n);//��ȡ������ҳΪ��λ

	//��ȡ�Ӷ���span��ӳ��
	Span* MapObjectToSpan(void* obj);

	//�ͷſռ�span�ص�PageCache�����ϲ����ڵ�span
	void ReleaseSpanToPageCache(Span* span);

private:
	SpanList _spanlist[NPAGES];//129����������ҳ����������Ҫ��ҳ�����Ҷ�Ӧ��SpanList
	//std::map<PageID, Span*> _idspanmap;
	std::unordered_map<PageID, Span*> _idspanmap;//size_t Span*��ͨ��pageid ��� Spanָ��

	std::mutex _mutex;
private:
	PageCache(){}

	PageCache(const PageCache&) = delete;
	static PageCache _inst;
};
// SpanList _spanlist[NPAGES]�У�_spanlist[n] ���spanlist�е�����span������ͬ��ҳ�����Ҷ��ǿ���span��ÿ��Span�ڲ�û�зֶ�
/*page cache:
//	�������ߵ��ڴ棬��ҳ�����У�_spanlist[span->_npage] Ҫ�����spanɾ������Ϊ��������ҳ�����еĿ���span
//	��_idspanmap�У�ֻҪ����ҳ�Ͷ�Ӧ��spanû�б仯�Ͳ���ɾ��
��ô�Ӳ���ϵͳҪ�ڴ� �� ��ô�����ڴ�(������)�ģ�
	size_t size; ֻ��Ҫ�ṩ��Ҫ���ڴ�ҳ��
	1.���濪ʼ�յģ���һֱ����128ҳ���ڴ��
		void* ptr = VirtualAlloc(0, (NPAGES - 1)*(1 << PAGE_SHIFT), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
		span->_pageid = (PageID)ptr >> PAGE_SHIFT;
		span->_npage = NPAGES - 1;
	ͨ�������䣬�����ṹ�� Span �� ��ҳҳ��(�ڴ��׵�ַptr) ��ӳ�䡣
	��ΪSpan�ڲ�û�зֶΣ�û���ڴ����(void*)������_list��_objsize������ȥ��ֵ
		for (size_t i = 0; i < span->_npage; ++i)
			_idspanmap[span->_pageid + i] = span;
	��������� ҳ��(����ҳ��ַ) �� Span ��ӳ��
		_spanlist[span->_npage].PushFront(span);
	�½�����Span �����Ӧ��spanlist _spanlist[span->_npage]

	2.Ҫnҳ���ڴ�(n<129)
		return _spanlist[n].PopFront();
	û����ô��ҳ���ʹӸ����Spanlist������span
		Span* span = _spanlist[i].PopFront();		//PopFront()  PushFront(span)
		Span* splist = new Span;

		splist->_pageid = span->_pageid;
		splist->_npage = n;
		span->_pageid = span->_pageid + n;
		span->_npage = span->_npage - n;//ҳ������n
	����[span->_pageid,span->_pageid+n-1]ҳ���ڴ������Splist�ˣ���span���ڳ�����
		for (size_t i = 0; i < n; ++i)
			_idspanmap[splist->_pageid + i] = splist;
		_spanlist[span->_npage].PushFront(span);//span��һ��spanlist��
	splistҪ�͸�Central Cache�����Բ��ᱻ�Ž�_spanlist[n]��
		return splist;

	3.Ҫ����128ҳ���ڴ棬Ҫ�����ϵͳ���� npage ҳ
		void* ptr = VirtualAlloc(0, npage << PAGE_SHIFT, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
		Span* span = new Span;
		span->_npage = npage;//����ҳ��
		span->_pageid = (PageID)ptr >> PAGE_SHIFT;//��ַ/4k == id
		span->_objsize = npage << PAGE_SHIFT;//�� size
		_idspanmap[span->_pageid] = span;
		return span;
	��Ϊ���������Ҫ���ߣ����Բ������_spanlist��ֻ�ǽ���һ�� ��ҳ�� �� span ��ӳ�䣬��¼ҳ�����ܴ�С�͹���

	4.ע����ǣ�spanֻ���ڷ�����ҳ����(returnǰ)���ͷŻ�ҳ���棬_objsize(������ڴ��С) �Żḳֵ��
	�������ߣ�_objsize ��ֵΪ ���ڴ��С�����ͷŵ�������ֵΪ 0��

��ô�û��ڴ�ģ� 
	void* ptr, Span* span����Ҫ֪�� ���ͷ��ڴ����(void*) �Ͷ�Ӧ��span
	size_t npage = span->_objsize >> PAGE_SHIFT; ������֪�����span�ж���ҳ
	1. ���ͷ��ڴ�ܴ󣬴���128ҳ
	��ȡ��ҳ��
		_idspanmap.erase(span->_pageid);
		delete span;
		VirtualFree(ptr, 0, MEM_RELEASE);
	2.�ͷ�С��129ҳ���ڴ� 
		span->_objsize = 0;
	�ȿ��Ƿ������ǰ�ϲ����ϲ�ֻ����_npage _pageid����Ϊ��û�зֶ���
		PageID curid = cur->_pageid;
		PageID previd = curid - 1;
		auto it = _idspanmap.find(previd);	//ע��_idspanmap�и���pageid��span�����span��һ������PageCache�У�ֻ��_usecount==0����PageCache��
		_spanlist[prev->_npage].Erase(prev);		//��ǰ�ϲ���������ɾ��prev
		prev->_npage += cur->_npage;				// �ϲ�
		for (PageID i = 0; i < cur->_npage; ++i){	//����id->span��ӳ���ϵ
			_idspanmap[cur->_pageid + i] = prev;	//cur��ҳ���Ž�prev��
		}
		delete cur;	
		cur = prev;// ������ǰ�ϲ�
	���ϲ�:
		PageID curid = cur->_pageid;
		PageID nextid = curid + cur->_npage;
		auto it = _idspanmap.find(nextid);
		_spanlist[next->_npage].Erase(next);
		cur->_npage += next->_npage;
		for (PageID i = 0; i < next->_npage; ++i){
			_idspanmap[next->_pageid + i] = cur;
		}
		delete next; //���ϲ���curû�б仯
*/
