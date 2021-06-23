#pragma once

#include "Common.h"

class ThreadCache
{
private:
	Freelist _freelist[NLISTS];//自由链表，184个。
	//SizeClass::Index(size)； 单个内存块大小 求得 _freelist[index]。

public:
	//申请和释放内存对象
	void* Allocate(size_t size);
	void Deallocate(void* ptr, size_t size);

	//从中心缓存获取对象
	void* FetchFromCentralCache(size_t index, size_t size);

	//释放对象时，链表过长时，回收内存回到中心堆
	void ListTooLong(Freelist* list, size_t size);
};

//静态的，不是所有可见
//每个线程有个自己的指针, 用(_declspec (thread))，我们在使用时，每次来都是自己的，就不用加锁了
//每个线程都有自己的tlslist
_declspec (thread) static ThreadCache* tlslist = nullptr;
/*
	thread 用于声明一个线程本地变量.
	_declspec(thread)的前缀是Microsoft添加给Visual C++编译器的一个修改符。
	它告诉编译器，对应的变量应该放入可执行文件或DLL文件中它的自己的节中。
	_declspec(thread)后面的变量 必须声明为函数中（或函数外）的一个全局变量或静态变量。
*/

/*
Freelist _freelist[NLISTS]; 保存的是thread cache 内可用的空闲内存块对象
怎么申请内存块对象？ 需要知道内存块对象的size
	size_t index = SizeClass::Index(size);//获取到_freelist相对应的位置
	Freelist* freelist = &_freelist[index];
	if (!freelist->Empty()){		//在ThreadCache处不为空的话，直接取
		return freelist->Pop();
	}else{
		// 否则的话，从中心缓存处获取
		return FetchFromCentralCache(index, SizeClass::Roundup(size));
	}

	1. 如何从中心缓存申请内存？
	size_t index:_freelist对应索引, size_t size:申请内存大小对齐大小计算，向上取整后的size.
	既然要申请，那就说明这个freelist是空的
	先确定对应的freelist
		Freelist* freelist = &_freelist[index];
	确定要申请的对象个数
		size_t maxsize = freelist->MaxSize();
		size_t numtomove = min(SizeClass::NumMoveSize(size), maxsize);// 65536/size vs maxsize
	向中心缓存申请对象。start、end 是一段连续对象的首尾指针
		void* start = nullptr, *end = nullptr;
		size_t batchsize = CentralCache::Getinstence()->FetchRangeObj(start, end, numtomove, size);
		if (batchsize > 1){
			freelist->PushRange(NEXT_OBJ(start), end, batchsize - 1);// ( ]; _list = NEXT_OBJ(start)
		}
		if (batchsize >= freelist->MaxSize()){
			freelist->SetMaxSize(maxsize + 1);
		}
		return start;

怎么释放内存? void* ptr: 内存块对象的指针 , size_t size: 对象的大小
	直接放回进入freelist
		size_t index = SizeClass::Index(size);
		Freelist* freelist = &_freelist[index];
		freelist->Push(ptr);
		if (freelist->Size() >= freelist->MaxSize()){
			ListTooLong(freelist, size);
		}
	释放对象时，freelist链表过长时，全部回收到中心缓存，需要知道对应的freelist和对象大小size
		void* start = freelist->PopRange(); //返回freelist的首地址
		CentralCache::Getinstence()->ReleaseListToSpans(start, size);// 向 CentralCache 的Span 释放内存
	
*/