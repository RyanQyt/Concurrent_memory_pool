#include "ThreadCache.h"
#include "CentralCache.h"

using namespace std;
void* ThreadCache::FetchFromCentralCache(size_t i, size_t size)
{
	// 获取一批对象，数量使用慢启动方式
	//因为如果你一次性给了太多，但是以后在不使用了，就会造成浪费，所以最好的办法就是采用慢启动，开始少，后面随着你要的次数越来越多，
	//那么就每次多给你一些，挂接在你的freelist下面
	size_t batchNum = min(SizeClass::NumMoveSize(size), _freeLists[i].MaxSize());

	// 去中心缓存获取batch_num个对象
	void* start = nullptr;
	void* end = nullptr;
	//要了一批返回第一个对象的地址，剩下的都挂接起来，但是也有可能我这个Span里面不够你的batch_num，那么少给你返回几个其实也是可以
	//因为已经超额完成了你需要的要求
	size_t actualNum = CentralCache::GetInstance()->FetchRangeObj(start, end, batchNum, SizeClass::RoundUp(size));
	assert(actualNum > 0);

	// >1，返回一个，剩下挂到自由链表
	//如果一次申请多个，剩下的挂起来，下次申请就不需要找中心缓存
	//减少锁的竞争
	if (actualNum > 1)
	{
		_freeLists[i].PushRange(NextObj(start), end, actualNum - 1);
	}

	if (_freeLists[i].MaxSize() == batchNum)
	{
		_freeLists[i].SetMaxSize(_freeLists[i].MaxSize() + 1);
	}

	return start;
}

void* ThreadCache::Allocate(size_t size)
{
	size_t i = SizeClass::Index(size);
	if (!_freeLists[i].Empty())
	{
		return _freeLists[i].Pop();
	}
	else
	{
		//这个函数里面其实是有两种可能的一种是向CentralCache，还有一种是CentralCache也没有，那就只能向PageCache要
		return FetchFromCentralCache(i, size);
	}
}

void ThreadCache::Deallocate(void* ptr, size_t size)
{
	size_t i = SizeClass::Index(size);
	_freeLists[i].Push(ptr);

	// List Too Long central cache 去释放
	//这里使用的是长度来确定是否需要进行归还作为条件
	if (_freeLists[i].Size() > _freeLists[i].MaxSize())
	{
		ListTooLong(_freeLists[i], size);
	}
}

// 释放对象时，链表过长时，回收内存回到中心缓存
void ThreadCache::ListTooLong(FreeList& list, size_t size)
{
	size_t batchNum = list.MaxSize(); //要还多少个
	void* start = nullptr;
	void* end = nullptr;
	list.PopRange(start, end, batchNum);

	CentralCache::GetInstance()->ReleaseListToSpans(start, size);
}

ThreadCache::~ThreadCache(){
	size_t size = 0;
	// [0, 16) 为8字节对齐线性增长，最大为128；freelist[16,72)56个桶，16字节对齐，size最大为1024
	// freelist[72,128)为128字节对齐，56个桶，最大为8Kb；freelist[128,184)为1024字节对齐，最大为64KB，也是56个桶
	// 这里把所有在freelist上面悬挂的内存全部还回CentralCache里面
	for(int i = 0 ; i < 184; ++i){
		if(i < 16) size = i * 8 + 8;
		else if(i < 72) size = (i - 15) * 16 + 128;
		else if(i < 128) size = (i - 71) * 128 + 1024;
		else size = (i - 127) * 1024 + 8192;

		void* start = nullptr;
		void* end = nullptr;
		if (_freeLists[i].Size() > 0) {
			_freeLists[i].PopRange(start, end, _freeLists[i].Size());
			CentralCache::GetInstance()->ReleaseListToSpans(start, size);
		}

	}
}