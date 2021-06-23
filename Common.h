#pragma once

#include <iostream>
#include <thread>
#include <mutex>
#include <map>
#include <unordered_map>
#include <vector>
#include <stdlib.h>
#include <algorithm>
#include <assert.h>

using std::cout;
using std::endl;

#include <Windows.h>

const size_t MAX_BYTES = 64 * 1024; //64K。ThreadCache 申请的最大内存，超过这个就要从系统申请。
const size_t NLISTS = 184; //数组元素总的有多少个，由对齐规则计算得来
const size_t PAGE_SHIFT = 12;
const size_t NPAGES = 129;


inline static void*& NEXT_OBJ(void* obj)//抢取对象头四个或者头八个字节，void*的别名，本省是内存，只能我们自己取
{
	return *((void**)obj);   // 先强转为void**,然后解引用就是一个void*
}

//设置一个公共的FreeList对象链表，每个对象中含有各个接口，到时候直接使用接口进行操作
//让一个类来管理自由链表
class Freelist
{
private:
	void* _list = nullptr;	// 给上缺省值
	size_t _size = 0;		// 记录有多少个对象，记录有几个内存块
	size_t _maxsize = 1;	// 

public:

	void Push(void* obj){ // Freelist 从首位置push
		NEXT_OBJ(obj) = _list;//将obj的头四个或者头八个字节 赋值为 下一个元素的地址
		_list = obj;
		++_size;
	}

	void PushRange(void* start, void* end, size_t n){ // Freelist 从首位置push
		NEXT_OBJ(end) = _list;
		_list = start;
		_size += n;
	}

	void* Pop(){ //把对象弹出去, Freelist 从首位置pop
		void* obj = _list;
		_list = NEXT_OBJ(obj);
		--_size;
		return obj;
	}

	void* PopRange(){ //把所有对象弹出去
		_size = 0;
		void* list = _list;
		_list = nullptr;

		return list;
	}
	
	bool Empty(){
		return _list == nullptr;
	}

	size_t Size(){
		return _size;
	}

	size_t MaxSize(){
		return _maxsize;
	}

	void SetMaxSize(size_t maxsize){
		_maxsize = maxsize;
	}
};

//专门用来计算大小位置的类
class SizeClass
{
public:
	//获取Freelist的位置
	inline static size_t _Index(size_t size, size_t align){//align : 3 4 7 10
		size_t alignnum = 1 << align;  //库里实现的方法。 8 16 128 1024； alignnum 表示单位量大小
		return ((size + alignnum - 1) >> align) - 1;// (size -1 + 2^n)/2^n - 1
	}

	//功能是整数提升，即 将size提升为 2^align 定义的整数的倍数。
	//将size 提升为 2^align 的倍数
	//例如：align = 8, size = 7，则这条宏运行的结果是8；align = 8, size = 13，则这条宏运行的结果是16。
	inline static size_t _Roundup(size_t size, size_t align) //align : 3 4 7 10
	{
		size_t alignnum = 1 << align;// 8 16 128 1024
		return (size + alignnum - 1)&~(alignnum - 1);
	}

public:
	// 控制在12%左右的内碎片浪费
	// [1,128]				8byte对齐 freelist[0,16)，每8byte用一个freelist，16个freelist
	// [129,1024]			16byte对齐 freelist[16,72)，每16byte用一个freelist，56个freelist
	// [1025,8*1024]		128byte对齐 freelist[72,128)，每128byte用一个freelist，56个freelist
	// [8*1024+1,64*1024]	1024byte对齐 freelist[128,184)，每1024byte用一个freelist，56个freelist

	inline static size_t Index(size_t size)//根据内存块对象的大小 计算在 freelist 中的索引
	{
		assert(size <= MAX_BYTES);

		// 每个区间有多少个链 分为4个区间
		static int group_array[4] = { 16, 56, 56, 56 };
		if (size <= 128)
		{
			return _Index(size, 3); //8 
		}
		else if (size <= 1024)
		{
			return _Index(size - 128, 4) + group_array[0];
		}
		else if (size <= 8192)
		{
			return _Index(size - 1024, 7) + group_array[0] + group_array[1];
		}
		else//if (size <= 65536)
		{
			return _Index(size - 8 * 1024, 10) + group_array[0] + group_array[1] + group_array[2];
		}
	}

	// 对齐大小计算，向上取整
	//  用于申请内存块对象的时候使用，必须向上取整
	static inline size_t Roundup(size_t bytes)
	{
		assert(bytes <= MAX_BYTES);

		if (bytes <= 128){
			return _Roundup(bytes, 3);	// 把 bytes 升到 8 的倍数
		}
		else if (bytes <= 1024){
			return _Roundup(bytes, 4);	// 把 bytes 升到 16 的倍数 
		}
		else if (bytes <= 8192){
			return _Roundup(bytes, 7);	//把 bytes 升到 128 的倍数
		}
		else {//if (bytes <= 65536){
			return _Roundup(bytes, 10);	//把 bytes 升到 1024 的倍数
		}
	}

	//动态计算从中心缓存分配多少个内存对象到ThreadCache中
	static size_t NumMoveSize(size_t size)
	{
		if (size == 0)
			return 0;

		int num = (int)(MAX_BYTES / size); //64K/size
		if (num < 2)
			num = 2;

		if (num > 512)
			num = 512;

		return num;
	}

	// 根据size计算中心缓存要从页缓存获取多大的span对象
	static size_t NumMovePage(size_t size) {	//返回页的个数
		size_t num = NumMoveSize(size);
		size_t npage = num*size;
		npage >>= PAGE_SHIFT;//2^12 = 4K
		if (npage == 0)
			npage = 1;
		return npage;
	}
};

#ifdef _WIN32
	typedef size_t PageID;
#else
	typedef long long PageID;
#endif //_WIN32

//Span是一个跨度，既可以分配内存出去，也是负责将内存回收回来到PageCache合并
//是一链式结构，定义为结构体就行，避免需要很多的友元
// span 是一个抽象的概念，表示一段有着 相同对象 的连续内存页，
//     包含这段连续内存页的页号、页数（段范围）;链接span; 可行位置
// 一个内存块对象有且只有在一个span中，可以根据对象地址求得页号，根据页号求得span指针。
//pageCache 中的span根据相同页数创建链表；CentralCache根据对象大小创建span链表。
//两者的span是相互排斥的关系，一个span不是在pageCache就是在CentralCache。
//threadCache的freelist是从CentralCache的span的_list移动确定范围的，CentralCache的span根据_usecount确定在threadCache的对象个数。
//CentralCache的span的_usecount为0，就必须把这个span还给PageCache了。
//CentralCache中_objsize表示内存对象的大小；PageCache中_objsize一直是0；
//PageCache中_objsize一直是0, 
//PageCache::AllocBigPageObj和FreeBigPageObj两个是PageCache对外程序，不是对CentralCache的接口，这时_objsize表示这个span的总大小
struct Span
{
	PageID _pageid = 0;//页号
	size_t _npage = 0;//页数

	Span* _prev = nullptr; 
	Span* _next = nullptr;

	void* _list = nullptr;//链接对象的自由链表，后面有对象就不为空，没有对象就是空
	size_t _objsize = 0;//对象的大小，

	size_t _usecount = 0;//对象使用计数,
};

//和上面的Freelist一样，各个接口自己实现，双向带头循环的Span链表
class SpanList //双向链表，循环链表
{
public:
	Span* _head; //head 不存东西，其实这是一个永久的链表尾。_head->next指向链表头，_head->pre指向可用尾节点
	std::mutex _mutex;

public:
	SpanList()
	{
		_head = new Span;
		_head->_next = _head;
		_head->_prev = _head;
	}

	~SpanList()//释放链表的每个节点
	{
		Span * cur = _head->_next;
		while (cur != _head)
		{
			Span* next = cur->_next;
			delete cur;
			cur = next;
		}
		delete _head;
		_head = nullptr;
	}

	//防止拷贝构造和赋值构造，将其封死，没有拷贝的必要，不然就自己会实现浅拷贝
	SpanList(const SpanList&) = delete;
	SpanList& operator=(const SpanList&) = delete;

	//左闭右开
	Span* Begin()//返回的一个数据的指针
	{
		return _head->_next;
	}

	Span* End()//最后一个的下一个指针
	{
		return _head;
	}

	bool Empty()
	{
		return _head->_next == _head;
	}

	//在pos位置的前面插入一个newspan
	void Insert(Span* cur, Span* newspan)
	{
		Span* prev = cur->_prev;

		//prev newspan cur
		prev->_next = newspan;
		newspan->_next = cur;

		newspan->_prev = prev;
		cur->_prev = newspan;
	}

	//删除pos位置的节点
	void Erase(Span* cur)//此处只是单纯的把pos拿出来，并没有释放掉，后面还有用处
	{
		Span* prev = cur->_prev;
		Span* next = cur->_next;

		prev->_next = next;
		next->_prev = prev;
	}

	//尾插
	void PushBack(Span* newspan)
	{
		Insert(End(), newspan);
	}

	//头插
	void PushFront(Span* newspan)
	{
		Insert(Begin(), newspan);
	}

	//尾删
	Span* PopBack()//实际是将尾部位置的节点拿出来
	{
		Span* span = _head->_prev;
		Erase(span);

		return span;
	}

	//头删
	Span* PopFront()//实际是将头部位置节点拿出来
	{
		Span* span = _head->_next;
		Erase(span);

		return span;
	}

	void Lock()
	{
		_mutex.lock();
	}

	void Unlock()
	{
		_mutex.unlock();
	}
};