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

const size_t MAX_BYTES = 64 * 1024; //64K��ThreadCache ���������ڴ棬���������Ҫ��ϵͳ���롣
const size_t NLISTS = 184; //����Ԫ���ܵ��ж��ٸ����ɶ������������
const size_t PAGE_SHIFT = 12;
const size_t NPAGES = 129;


inline static void*& NEXT_OBJ(void* obj)//��ȡ����ͷ�ĸ�����ͷ�˸��ֽڣ�void*�ı�������ʡ���ڴ棬ֻ�������Լ�ȡ
{
	return *((void**)obj);   // ��ǿתΪvoid**,Ȼ������þ���һ��void*
}

//����һ��������FreeList����������ÿ�������к��и����ӿڣ���ʱ��ֱ��ʹ�ýӿڽ��в���
//��һ������������������
class Freelist
{
private:
	void* _list = nullptr;	// ����ȱʡֵ
	size_t _size = 0;		// ��¼�ж��ٸ����󣬼�¼�м����ڴ��
	size_t _maxsize = 1;	// 

public:

	void Push(void* obj){ // Freelist ����λ��push
		NEXT_OBJ(obj) = _list;//��obj��ͷ�ĸ�����ͷ�˸��ֽ� ��ֵΪ ��һ��Ԫ�صĵ�ַ
		_list = obj;
		++_size;
	}

	void PushRange(void* start, void* end, size_t n){ // Freelist ����λ��push
		NEXT_OBJ(end) = _list;
		_list = start;
		_size += n;
	}

	void* Pop(){ //�Ѷ��󵯳�ȥ, Freelist ����λ��pop
		void* obj = _list;
		_list = NEXT_OBJ(obj);
		--_size;
		return obj;
	}

	void* PopRange(){ //�����ж��󵯳�ȥ
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

//ר�����������Сλ�õ���
class SizeClass
{
public:
	//��ȡFreelist��λ��
	inline static size_t _Index(size_t size, size_t align){//align : 3 4 7 10
		size_t alignnum = 1 << align;  //����ʵ�ֵķ����� 8 16 128 1024�� alignnum ��ʾ��λ����С
		return ((size + alignnum - 1) >> align) - 1;// (size -1 + 2^n)/2^n - 1
	}

	//������������������ ��size����Ϊ 2^align ����������ı�����
	//��size ����Ϊ 2^align �ı���
	//���磺align = 8, size = 7�������������еĽ����8��align = 8, size = 13�������������еĽ����16��
	inline static size_t _Roundup(size_t size, size_t align) //align : 3 4 7 10
	{
		size_t alignnum = 1 << align;// 8 16 128 1024
		return (size + alignnum - 1)&~(alignnum - 1);
	}

public:
	// ������12%���ҵ�����Ƭ�˷�
	// [1,128]				8byte���� freelist[0,16)��ÿ8byte��һ��freelist��16��freelist
	// [129,1024]			16byte���� freelist[16,72)��ÿ16byte��һ��freelist��56��freelist
	// [1025,8*1024]		128byte���� freelist[72,128)��ÿ128byte��һ��freelist��56��freelist
	// [8*1024+1,64*1024]	1024byte���� freelist[128,184)��ÿ1024byte��һ��freelist��56��freelist

	inline static size_t Index(size_t size)//�����ڴ�����Ĵ�С ������ freelist �е�����
	{
		assert(size <= MAX_BYTES);

		// ÿ�������ж��ٸ��� ��Ϊ4������
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

	// �����С���㣬����ȡ��
	//  ���������ڴ������ʱ��ʹ�ã���������ȡ��
	static inline size_t Roundup(size_t bytes)
	{
		assert(bytes <= MAX_BYTES);

		if (bytes <= 128){
			return _Roundup(bytes, 3);	// �� bytes ���� 8 �ı���
		}
		else if (bytes <= 1024){
			return _Roundup(bytes, 4);	// �� bytes ���� 16 �ı��� 
		}
		else if (bytes <= 8192){
			return _Roundup(bytes, 7);	//�� bytes ���� 128 �ı���
		}
		else {//if (bytes <= 65536){
			return _Roundup(bytes, 10);	//�� bytes ���� 1024 �ı���
		}
	}

	//��̬��������Ļ��������ٸ��ڴ����ThreadCache��
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

	// ����size�������Ļ���Ҫ��ҳ�����ȡ����span����
	static size_t NumMovePage(size_t size) {	//����ҳ�ĸ���
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

//Span��һ����ȣ��ȿ��Է����ڴ��ȥ��Ҳ�Ǹ����ڴ���ջ�����PageCache�ϲ�
//��һ��ʽ�ṹ������Ϊ�ṹ����У�������Ҫ�ܶ����Ԫ
// span ��һ������ĸ����ʾһ������ ��ͬ���� �������ڴ�ҳ��
//     ������������ڴ�ҳ��ҳ�š�ҳ�����η�Χ��;����span; ����λ��
// һ���ڴ���������ֻ����һ��span�У����Ը��ݶ����ַ���ҳ�ţ�����ҳ�����spanָ�롣
//pageCache �е�span������ͬҳ������������CentralCache���ݶ����С����span������
//���ߵ�span���໥�ų�Ĺ�ϵ��һ��span������pageCache������CentralCache��
//threadCache��freelist�Ǵ�CentralCache��span��_list�ƶ�ȷ����Χ�ģ�CentralCache��span����_usecountȷ����threadCache�Ķ��������
//CentralCache��span��_usecountΪ0���ͱ�������span����PageCache�ˡ�
//CentralCache��_objsize��ʾ�ڴ����Ĵ�С��PageCache��_objsizeһֱ��0��
//PageCache��_objsizeһֱ��0, 
//PageCache::AllocBigPageObj��FreeBigPageObj������PageCache������򣬲��Ƕ�CentralCache�Ľӿڣ���ʱ_objsize��ʾ���span���ܴ�С
struct Span
{
	PageID _pageid = 0;//ҳ��
	size_t _npage = 0;//ҳ��

	Span* _prev = nullptr; 
	Span* _next = nullptr;

	void* _list = nullptr;//���Ӷ�������������������ж���Ͳ�Ϊ�գ�û�ж�����ǿ�
	size_t _objsize = 0;//����Ĵ�С��

	size_t _usecount = 0;//����ʹ�ü���,
};

//�������Freelistһ���������ӿ��Լ�ʵ�֣�˫���ͷѭ����Span����
class SpanList //˫��������ѭ������
{
public:
	Span* _head; //head ���涫������ʵ����һ�����õ�����β��_head->nextָ������ͷ��_head->preָ�����β�ڵ�
	std::mutex _mutex;

public:
	SpanList()
	{
		_head = new Span;
		_head->_next = _head;
		_head->_prev = _head;
	}

	~SpanList()//�ͷ�������ÿ���ڵ�
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

	//��ֹ��������͸�ֵ���죬���������û�п����ı�Ҫ����Ȼ���Լ���ʵ��ǳ����
	SpanList(const SpanList&) = delete;
	SpanList& operator=(const SpanList&) = delete;

	//����ҿ�
	Span* Begin()//���ص�һ�����ݵ�ָ��
	{
		return _head->_next;
	}

	Span* End()//���һ������һ��ָ��
	{
		return _head;
	}

	bool Empty()
	{
		return _head->_next == _head;
	}

	//��posλ�õ�ǰ�����һ��newspan
	void Insert(Span* cur, Span* newspan)
	{
		Span* prev = cur->_prev;

		//prev newspan cur
		prev->_next = newspan;
		newspan->_next = cur;

		newspan->_prev = prev;
		cur->_prev = newspan;
	}

	//ɾ��posλ�õĽڵ�
	void Erase(Span* cur)//�˴�ֻ�ǵ����İ�pos�ó�������û���ͷŵ������滹���ô�
	{
		Span* prev = cur->_prev;
		Span* next = cur->_next;

		prev->_next = next;
		next->_prev = prev;
	}

	//β��
	void PushBack(Span* newspan)
	{
		Insert(End(), newspan);
	}

	//ͷ��
	void PushFront(Span* newspan)
	{
		Insert(Begin(), newspan);
	}

	//βɾ
	Span* PopBack()//ʵ���ǽ�β��λ�õĽڵ��ó���
	{
		Span* span = _head->_prev;
		Erase(span);

		return span;
	}

	//ͷɾ
	Span* PopFront()//ʵ���ǽ�ͷ��λ�ýڵ��ó���
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