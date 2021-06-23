#pragma once

#include "Common.h"

//对于Page Cache也要设置为单例，对于Central Cache获取span的时候
//每次都是从同一个page数组中获取span
//单例模式 饿汉模式，不怕线程安全
class PageCache
{
public:
	static PageCache* GetInstence(){
		return &_inst;
	}

	Span* AllocBigPageObj(size_t size);
	void FreeBigPageObj(void* ptr, Span* span);

	Span* _NewSpan(size_t n);
	Span* NewSpan(size_t n);//获取的是以页为单位

	//获取从对象到span的映射
	Span* MapObjectToSpan(void* obj);

	//释放空间span回到PageCache，并合并相邻的span
	void ReleaseSpanToPageCache(Span* span);

private:
	SpanList _spanlist[NPAGES];//129个，索引是页数。根据需要的页数，找对应的SpanList
	//std::map<PageID, Span*> _idspanmap;
	std::unordered_map<PageID, Span*> _idspanmap;//size_t Span*，通过pageid 获得 Span指针

	std::mutex _mutex;
private:
	PageCache(){}

	PageCache(const PageCache&) = delete;
	static PageCache _inst;
};
// SpanList _spanlist[NPAGES]中，_spanlist[n] 这个spanlist中的所有span有着相同的页数，且都是空闲span。每个Span内部没有分段
/*page cache:
//	被申请走的内存，在页缓存中，_spanlist[span->_npage] 要把这个span删除，因为里面存的是页缓存有的空闲span
//	在_idspanmap中，只要单个页和对应的span没有变化就不会删除
怎么从操作系统要内存 和 怎么送走内存(被申请)的？
	size_t size; 只需要提供需要的内存页数
	1.缓存开始空的，会一直申请128页的内存块
		void* ptr = VirtualAlloc(0, (NPAGES - 1)*(1 << PAGE_SHIFT), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
		span->_pageid = (PageID)ptr >> PAGE_SHIFT;
		span->_npage = NPAGES - 1;
	通过这两句，建立结构体 Span 向 首页页号(内存首地址ptr) 的映射。
	因为Span内部没有分段，没有内存对象(void*)，所以_list和_objsize都不会去赋值
		for (size_t i = 0; i < span->_npage; ++i)
			_idspanmap[span->_pageid + i] = span;
	在这里，建立 页号(所有页地址) 向 Span 的映射
		_spanlist[span->_npage].PushFront(span);
	新建立的Span 存入对应的spanlist _spanlist[span->_npage]

	2.要n页的内存(n<129)
		return _spanlist[n].PopFront();
	没有这么多页，就从更大的Spanlist里面找span
		Span* span = _spanlist[i].PopFront();		//PopFront()  PushFront(span)
		Span* splist = new Span;

		splist->_pageid = span->_pageid;
		splist->_npage = n;
		span->_pageid = span->_pageid + n;
		span->_npage = span->_npage - n;//页数减了n
	这样[span->_pageid,span->_pageid+n-1]页的内存就属于Splist了，从span里挖出来的
		for (size_t i = 0; i < n; ++i)
			_idspanmap[splist->_pageid + i] = splist;
		_spanlist[span->_npage].PushFront(span);//span换一个spanlist放
	splist要送给Central Cache，所以不会被放进_spanlist[n]中
		return splist;

	3.要大于128页的内存，要想操作系统申请 npage 页
		void* ptr = VirtualAlloc(0, npage << PAGE_SHIFT, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
		Span* span = new Span;
		span->_npage = npage;//设置页数
		span->_pageid = (PageID)ptr >> PAGE_SHIFT;//地址/4k == id
		span->_objsize = npage << PAGE_SHIFT;//总 size
		_idspanmap[span->_pageid] = span;
		return span;
	因为过大而且又要送走，所以不会存入_spanlist。只是建立一个 首页码 向 span 的映射，记录页数和总大小就够了

	4.注意的是，span只有在发送走页缓存(return前)和释放回页缓存，_objsize(对象的内存大小) 才会赋值。
	被申请走，_objsize 赋值为 总内存大小；被释放掉，被赋值为 0；

怎么拿回内存的？ 
	void* ptr, Span* span；需要知道 被释放内存对象(void*) 和对应的span
	size_t npage = span->_objsize >> PAGE_SHIFT; 必须先知道这个span有多少页
	1. 被释放内存很大，大于128页
	获取首页码
		_idspanmap.erase(span->_pageid);
		delete span;
		VirtualFree(ptr, 0, MEM_RELEASE);
	2.释放小于129页的内存 
		span->_objsize = 0;
	先看是否可以向前合并。合并只考虑_npage _pageid，因为还没有分对象
		PageID curid = cur->_pageid;
		PageID previd = curid - 1;
		auto it = _idspanmap.find(previd);	//注意_idspanmap中根据pageid找span，这个span不一定是在PageCache中，只有_usecount==0才在PageCache中
		_spanlist[prev->_npage].Erase(prev);		//向前合并，所以先删了prev
		prev->_npage += cur->_npage;				// 合并
		for (PageID i = 0; i < cur->_npage; ++i){	//修正id->span的映射关系
			_idspanmap[cur->_pageid + i] = prev;	//cur的页都放进prev中
		}
		delete cur;	
		cur = prev;// 继续向前合并
	向后合并:
		PageID curid = cur->_pageid;
		PageID nextid = curid + cur->_npage;
		auto it = _idspanmap.find(nextid);
		_spanlist[next->_npage].Erase(next);
		cur->_npage += next->_npage;
		for (PageID i = 0; i < next->_npage; ++i){
			_idspanmap[next->_pageid + i] = cur;
		}
		delete next; //向后合并，cur没有变化
*/
