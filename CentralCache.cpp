#include "CentralCache.h"
#include "PageCache.h"

CentralCache CentralCache::_inst;

Span* CentralCache::GetOneSpan(SpanList& spanlist, size_t byte_size)
{
	Span* span = spanlist.Begin();
	while (span != spanlist.End())//当前找到一个span
	{
		if (span->_list != nullptr)
			return span;
		else
			span = span->_next;
	}

	////测试打桩
	//Span* newspan = new Span;
	//newspan->_objsize = 16;
	//void* ptr = malloc(16 * 8);
	//void* cur = ptr;
	//for (size_t i = 0; i < 7; ++i)
	//{
	//	void* next = (char*)cur + 16;
	//	NEXT_OBJ(cur) = next;
	//	cur = next;
	//}
	//NEXT_OBJ(cur) = nullptr;
	//newspan->_list = ptr;

	// 走到这儿，说明前面没有获取到span,都是空的，到下一层pagecache获取span
	Span* newspan = PageCache::GetInstence()->NewSpan(SizeClass::NumMovePage(byte_size)); //获取新的span
	// 将span页切分成需要的对象并链接起来
	char* cur = (char*)(newspan->_pageid << PAGE_SHIFT);
	char* end = cur + (newspan->_npage << PAGE_SHIFT);
	newspan->_list = cur;
	newspan->_objsize = byte_size; //单个对象的大小

	while (cur + byte_size < end){ //已经知道起点和终点，建立链表
		char* next = cur + byte_size;
		NEXT_OBJ(cur) = next;
		cur = next;
	}
	NEXT_OBJ(cur) = nullptr;//span链表末端没有下一个obj

	spanlist.PushFront(newspan);

	return newspan;
}


//获取一个批量的内存对象
//start end :传出形参;  byte_size: 一块内存块的大小 n: 内存块的个数
size_t CentralCache::FetchRangeObj(void*& start, void*& end, size_t n, size_t byte_size)
{
	size_t index = SizeClass::Index(byte_size);
	SpanList& spanlist = _spanlist[index];//赋值->拷贝构造。该种内存块的spanlist

	//  到时候记得加锁
	//  spanlist.Lock();
	std::unique_lock<std::mutex> lock(spanlist._mutex);

	Span* span = GetOneSpan(spanlist, byte_size);
	//到这儿已经获取到一个newspan

	//从span中获取range对象
	size_t batchsize = 0;						
	void* prev = nullptr;//提前保存前一个
	void* cur = span->_list;//用cur来遍历，往后走
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

	//将空的span移到最后，保持非空的span在前面
	if (span->_list == nullptr)
	{
		spanlist.Erase(span);
		spanlist.PushBack(span);
	}

	//spanlist.Unlock();

	return batchsize;
}

// 把一个freelist都释放还给span
//start:freelist的第一块的指针； size: freelist的单个内存块的大小
void CentralCache::ReleaseListToSpans(void* start, size_t size)
{
	size_t index = SizeClass::Index(size);
	SpanList& spanlist = _spanlist[index];

	//将锁放在循环外面
	// CentralCache:对当前桶进行加锁(桶锁)，减小锁的粒度
	// PageCache:必须对整个SpanList全局加锁
	// 因为可能存在多个线程同时去系统申请内存的情况
	//spanlist.Lock();
	std::unique_lock<std::mutex> lock(spanlist._mutex);

	while (start)
	{
		void* next = NEXT_OBJ(start);

		////到时候记得加锁
		//spanlist.Lock(); // 构成了很多的锁竞争

		Span* span = PageCache::GetInstence()->MapObjectToSpan(start);//start指向的内存块对应的span
		NEXT_OBJ(start) = span->_list;//span->_list 表示这个span中为分配的对象的首地址
		span->_list = start;//把start指向内存块还给了span，放在span的开头

		//当一个span的对象全部释放回来的时候，将span还给pagecache,并且做页合并
		if (--span->_usecount == 0)//一个span内的内存块，没有被线程占用的，就还给pageCache
		{
			spanlist.Erase(span);
			PageCache::GetInstence()->ReleaseSpanToPageCache(span);//释放span给pagecache
		}

		//spanlist.Unlock();

		start = next;
	}

	//spanlist.Unlock();
}