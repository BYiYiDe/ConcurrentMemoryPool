#pragma once

#include "Common.h"

//上面的ThreadCache里面没有的话，要从中心获取

/*
进行资源的均衡，对于ThreadCache的某个资源过剩的时候，可以回收ThreadCache内部的的内存
从而可以分配给其他的ThreadCache
只有一个中心缓存，对于所有的线程来获取内存的时候都应该是一个中心缓存
所以对于中心缓存可以使用单例模式来进行创建中心缓存的类
对于中心缓存来说要加锁
*/

//设计成单例模式
class CentralCache
{
public:
	static CentralCache* Getinstence()
	{
		return &_inst;
	}

	//从page cache获取一个span
	Span* GetOneSpan(SpanList& spanlist, size_t byte_size);

	//从中心缓存获取一定数量的对象给threa cache
	size_t FetchRangeObj(void*& start, void*& end, size_t n, size_t byte_size);

	//将一定数量的对象释放给span跨度
	void ReleaseListToSpans(void* start, size_t size);

private:
	SpanList _spanlist[NLISTS];//184个SpanList；分了 内存块对象 的SpanList

private:
	CentralCache(){}//private中，防止默认构造，自己创建

	CentralCache(CentralCache&) = delete;//不允许拷贝
	static CentralCache _inst;
};


/*

	VirtualAlloc(0, npage << PAGE_SHIFT, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	解析： 
*/

/*
Central Cache本质是由一个哈希映射的Span对象自由双向链表构成。
_spanlist[NLISTS] 不同于 页缓存，是按照内存块对象大小设置的哈希表。
单个的Span是双向带头循环的链表点，大小4K，_第一个内存对象里有第二个对象的地址。
span的 _list 表示当前span中空闲的内存块对象的首地址，不一定和 _pageid<<12 相等。

内存是怎么被申请的(给thread cache)？
	需要 byte_size: 一块内存块对象的大小 n: 内存块对象的个数；传出被申请的内存收尾地址 void*& start, void*& end。
	确定对应内存块对象的 SpanList，并上行锁
		size_t index = SizeClass::Index(byte_size);
		SpanList& spanlist = _spanlist[index]; 
		std::unique_lock<std::mutex> lock(spanlist._mutex);
	1.怎么向对应的spanlist获取一个span
		Span* span = spanlist.Begin();
		while (span != spanlist.End()){		//当前找到一个span
			if (span->_list != nullptr)
				return span;
			else
				span = span->_next;
		}
	2.如果对应的spanlist中没有span，那就向 页缓存 申请。 byte_size 对象块大小
	因为中心缓存中的对象已经分好了，所以不能像页缓存一样看其他的spanlist
	先计算要申请多少页，因为页缓存的单位大小是页数。
		int page_num = SizeClass::NumMovePage(byte_size);
		Span* newspan = PageCache::GetInstence()->NewSpan(page_num);//newspan只初始化了_pageid _npage
	将span页切分成需要的对象并链接起来
		char* cur = (char*)(newspan->_pageid << PAGE_SHIFT);	//因为要和byte_size计算，且要限制指针数值位数，转换成(char*)
		char* end = cur + (newspan->_npage << PAGE_SHIFT);
		newspan->_list = cur;
		newspan->_objsize = byte_size;	
		while (cur + byte_size < end){ //已经知道起点和终点，切割span,建立链表
			char* next = cur + byte_size;
			NEXT_OBJ(cur) = next;
			cur = next;
		}
		NEXT_OBJ(cur) = nullptr;//span链表末端没有下一个obj
		_spanlist[byte_size].PushFront(newspan);	//新来的一定放在前面优先取用
		return newspan;
	3.获得对应的span后，按照threadcache需要的对象个数n，确定 首尾指针
	注意要记录这个span中的_usecount，代表这个span中被使用的对象个数
		size_t batchsize = 0;	//实际发送的对象个数不一定是n，batchsize这个数是记录实际发送的对象个数
		void* prev = nullptr;	//提前保存前一个
		void* cur = span->_list;//用cur来遍历，往后走；pan->_list 表示这段span可用内存的首地址
		for (size_t i = 0; i < n; ++i){
			prev = cur;
			cur = NEXT_OBJ(cur);
			++batchsize;
			if (cur == nullptr)//随时判断cur是否为空，为空的话，提前停止
				break;
		} //prev 才是需要被带走的 end， cur 是spanlist新的头
		start = span->_list;
		end = prev;
		span->_list = cur;
		span->_usecount += batchsize;	
		if (span->_list == nullptr){ //用完的span放在最后面
			spanlist.Erase(span);
			spanlist.PushBack(span);
		}
		return batchsize;

内存块对象是怎么被释放回来的？(一旦释放，肯定是一整条freelist都释放了)
	需要知道void* start, size_t size，释放对象的首地址 和 单个对象大小
	首先确定对应的spanlist
		size_t index = SizeClass::Index(size);
		SpanList& spanlist = _spanlist[index];
		std::unique_lock<std::mutex> lock(spanlist._mutex);
	之后一个对象一个对象地放到对应的span之中
		void* next = NEXT_OBJ(start);
		Span* span = PageCache::GetInstence()->MapObjectToSpan(start);
		NEXT_OBJ(start) = span->_list;		//对象的插入方式
		span->_list = start;
		if (--span->_usecount == 0) {		//一个span内的内存块，没有被线程占用的，就还给pageCache
			spanlist.Erase(span);
			PageCache::GetInstence()->ReleaseSpanToPageCache(span);//释放span给pagecache
		}
		start = next;

*/