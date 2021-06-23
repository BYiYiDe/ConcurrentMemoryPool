#pragma once

#include "Common.h"

class ThreadCache
{
private:
	Freelist _freelist[NLISTS];//��������184����
	//SizeClass::Index(size)�� �����ڴ���С ��� _freelist[index]��

public:
	//������ͷ��ڴ����
	void* Allocate(size_t size);
	void Deallocate(void* ptr, size_t size);

	//�����Ļ����ȡ����
	void* FetchFromCentralCache(size_t index, size_t size);

	//�ͷŶ���ʱ���������ʱ�������ڴ�ص����Ķ�
	void ListTooLong(Freelist* list, size_t size);
};

//��̬�ģ��������пɼ�
//ÿ���߳��и��Լ���ָ��, ��(_declspec (thread))��������ʹ��ʱ��ÿ���������Լ��ģ��Ͳ��ü�����
//ÿ���̶߳����Լ���tlslist
_declspec (thread) static ThreadCache* tlslist = nullptr;
/*
	thread ��������һ���̱߳��ر���.
	_declspec(thread)��ǰ׺��Microsoft��Ӹ�Visual C++��������һ���޸ķ���
	�����߱���������Ӧ�ı���Ӧ�÷����ִ���ļ���DLL�ļ��������Լ��Ľ��С�
	_declspec(thread)����ı��� ��������Ϊ�����У������⣩��һ��ȫ�ֱ�����̬������
*/

/*
Freelist _freelist[NLISTS]; �������thread cache �ڿ��õĿ����ڴ�����
��ô�����ڴ����� ��Ҫ֪���ڴ������size
	size_t index = SizeClass::Index(size);//��ȡ��_freelist���Ӧ��λ��
	Freelist* freelist = &_freelist[index];
	if (!freelist->Empty()){		//��ThreadCache����Ϊ�յĻ���ֱ��ȡ
		return freelist->Pop();
	}else{
		// ����Ļ��������Ļ��洦��ȡ
		return FetchFromCentralCache(index, SizeClass::Roundup(size));
	}

	1. ��δ����Ļ��������ڴ棿
	size_t index:_freelist��Ӧ����, size_t size:�����ڴ��С�����С���㣬����ȡ�����size.
	��ȻҪ���룬�Ǿ�˵�����freelist�ǿյ�
	��ȷ����Ӧ��freelist
		Freelist* freelist = &_freelist[index];
	ȷ��Ҫ����Ķ������
		size_t maxsize = freelist->MaxSize();
		size_t numtomove = min(SizeClass::NumMoveSize(size), maxsize);// 65536/size vs maxsize
	�����Ļ����������start��end ��һ�������������βָ��
		void* start = nullptr, *end = nullptr;
		size_t batchsize = CentralCache::Getinstence()->FetchRangeObj(start, end, numtomove, size);
		if (batchsize > 1){
			freelist->PushRange(NEXT_OBJ(start), end, batchsize - 1);// ( ]; _list = NEXT_OBJ(start)
		}
		if (batchsize >= freelist->MaxSize()){
			freelist->SetMaxSize(maxsize + 1);
		}
		return start;

��ô�ͷ��ڴ�? void* ptr: �ڴ������ָ�� , size_t size: ����Ĵ�С
	ֱ�ӷŻؽ���freelist
		size_t index = SizeClass::Index(size);
		Freelist* freelist = &_freelist[index];
		freelist->Push(ptr);
		if (freelist->Size() >= freelist->MaxSize()){
			ListTooLong(freelist, size);
		}
	�ͷŶ���ʱ��freelist�������ʱ��ȫ�����յ����Ļ��棬��Ҫ֪����Ӧ��freelist�Ͷ����Сsize
		void* start = freelist->PopRange(); //����freelist���׵�ַ
		CentralCache::Getinstence()->ReleaseListToSpans(start, size);// �� CentralCache ��Span �ͷ��ڴ�
	
*/