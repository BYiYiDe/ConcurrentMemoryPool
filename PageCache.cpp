#include "PageCache.h"

PageCache PageCache::_inst;//定义了。饿汉模式


//大对象申请，直接从系统
// size：总需求的内存大小，不是对象的大小。 返回符合要求的span。一个span就是一个连续的内存块。
Span* PageCache::AllocBigPageObj(size_t size){
	assert(size > MAX_BYTES);

	size = SizeClass::_Roundup(size, PAGE_SHIFT); //对齐 12
	size_t npage = size >> PAGE_SHIFT;//需要的页数
	if (npage < NPAGES)//129页
	{
		Span* span = NewSpan(npage);	//创建新的span
		span->_objsize = size;			//记录span的内存大小
		return span;					
	}
	else
	{
		void* ptr = VirtualAlloc(0, npage << PAGE_SHIFT,
			MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);

		if (ptr == nullptr)
			throw std::bad_alloc();

		Span* span = new Span;
		span->_npage = npage;//设置页数
		span->_pageid = (PageID)ptr >> PAGE_SHIFT;//地址/4k == id
		span->_objsize = npage << PAGE_SHIFT;//总 size

		_idspanmap[span->_pageid] = span;

		return span;
	}
}

//小对象申请，直接从_spanlist n:页数
Span* PageCache::NewSpan(size_t n)
{
	// 加锁，防止多个线程同时到PageCache中申请span
	// 这里必须是给全局加锁，不能单独的给每个桶加锁
	// 如果对应桶没有span,是需要向系统申请的
	// 可能存在多个线程同时向系统申请内存的可能
	std::unique_lock<std::mutex> lock(_mutex);

	return _NewSpan(n);
}


// n: 申请的页数
Span* PageCache::_NewSpan(size_t n)
{
	assert(n < NPAGES);
	//当Central Cache向page cache申请内存时，Page Cache先检查对应位置有没有span，
	//如果没有则向更大页寻找一个span，如果找到则分裂成两个。
	//比如：申请的是4page，4page后面没有挂span，则向后面寻找更大的span，
	//假设在10page位置找到一个span，则将10page span分裂为一个4page span和一个6page span。
	if (!_spanlist[n].Empty())
		return _spanlist[n].PopFront();

	for (size_t i = n + 1; i < NPAGES; ++i)
	{
		if (!_spanlist[i].Empty())
		{
			Span* span = _spanlist[i].PopFront();//
			Span* splist = new Span;

			splist->_pageid = span->_pageid;
			splist->_npage = n;
			span->_pageid = span->_pageid + n;//后面剩余的内存的首id
			span->_npage = span->_npage - n;//页数减了n

			//splist->_pageid = span->_pageid + n;	
			//span->_npage = splist->_npage - n;	
			//span->_npage = n;						

			for (size_t i = 0; i < n; ++i)
				_idspanmap[splist->_pageid + i] = splist;//建立这n页内存 与 span的映射

			//_spanlist[splist->_npage].PushFront(splist);
			//return span;

			_spanlist[span->_npage].PushFront(span);
			return splist;
		}
	}

	Span* span = new Span;

	// 到这里说明SpanList中没有合适的span,只能向系统申请128页的内存
#ifdef _WIN32
	void* ptr = VirtualAlloc(0, (NPAGES - 1)*(1 << PAGE_SHIFT), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
#else
	//  brk
#endif

	span->_pageid = (PageID)ptr >> PAGE_SHIFT;//页号。因为这是一次申请的连续内存，所以页号每自增1，就多4K的内存
	span->_npage = NPAGES - 1;

	for (size_t i = 0; i < span->_npage; ++i)
		_idspanmap[span->_pageid + i] = span;

	_spanlist[span->_npage].PushFront(span);  //方括号。 
	return _NewSpan(n);//重来一遍
}

// 获取从对象到span的映射
Span* PageCache::MapObjectToSpan(void* obj)
{
	//计算页号
	PageID id = (PageID)obj >> PAGE_SHIFT;//id 就是 地址/4K
	auto it = _idspanmap.find(id);
	if (it != _idspanmap.end())
	{
		return it->second;
	}
	else
	{
		assert(false);
		return nullptr;
	}
}

//将一个空闲的span释放掉。如果大于128页还给操作系统，小于就还给pagecache
void PageCache::FreeBigPageObj(void* ptr, Span* span)
{
	size_t npage = span->_objsize >> PAGE_SHIFT;//页数
	if (npage < NPAGES)							//相当于还是小于128页
	{
		span->_objsize = 0;
		ReleaseSpanToPageCache(span);	//
	}
	else
	{
		_idspanmap.erase(npage);
		delete span;
		VirtualFree(ptr, 0, MEM_RELEASE);
	}
}

//释放小于128页的内存
void PageCache::ReleaseSpanToPageCache(Span* cur)
{
	// 必须上全局锁,可能多个线程一起从ThreadCache中归还数据
	std::unique_lock<std::mutex> lock(_mutex);

	// 当释放的内存是大于128页,直接将内存归还给操作系统,不能合并
	if (cur->_npage >= NPAGES){										
		void* ptr = (void*)(cur->_pageid << PAGE_SHIFT);
		// 归还之前删除掉页到span的映射
		_idspanmap.erase(cur->_pageid);
		VirtualFree(ptr, 0, MEM_RELEASE);
		delete cur;
		return;
	}

	// 向前合并
	while (1)
	{
		////超过128页则不合并
		//if (cur->_npage > NPAGES - 1)
		//	break;

		PageID curid = cur->_pageid;
		PageID previd = curid - 1;
		auto it = _idspanmap.find(previd);//看前一个id所以哪个span

		// 没有找到
		if (it == _idspanmap.end())
			break;

		// 前一个span不空闲
		if (it->second->_usecount != 0)
			break;

		Span* prev = it->second;

		//超过128页则不合并
		if (cur->_npage + prev->_npage > NPAGES - 1)
			break;


		// 先把prev从链表中移除
		_spanlist[prev->_npage].Erase(prev);

		// 合并
		prev->_npage += cur->_npage;
		//修正id->span的映射关系
		for (PageID i = 0; i < cur->_npage; ++i)
		{
			_idspanmap[cur->_pageid + i] = prev;
		}
		delete cur;

		// 继续向前合并
		cur = prev;
	}


	//向后合并
	while (1)
	{
		////超过128页则不合并
		//if (cur->_npage > NPAGES - 1)
		//	break;

		PageID curid = cur->_pageid;
		PageID nextid = curid + cur->_npage;
		//std::map<PageID, Span*>::iterator it = _idspanmap.find(nextid);
		auto it = _idspanmap.find(nextid);

		if (it == _idspanmap.end())
			break;

		if (it->second->_usecount != 0)
			break;

		Span* next = it->second;

		//超过128页则不合并
		if (cur->_npage + next->_npage >= NPAGES - 1)
			break;

		_spanlist[next->_npage].Erase(next);

		cur->_npage += next->_npage;
		//修正id->Span的映射关系
		for (PageID i = 0; i < next->_npage; ++i)
		{
			_idspanmap[next->_pageid + i] = cur;
		}

		delete next;
	}

	// 最后将合并好的span插入到span链中
	_spanlist[cur->_npage].PushFront(cur);
}
