#pragma once
#include "Common.h"

//CentralCache是要加锁的，但是锁的粒度不需要太大，只需要加一个桶锁，因为只有多个线程同时取一个Span
//要保证CentralCache和PageCache对象都是全局唯一的，所以直接使用单例模式
//且这里使用的是饿汉模式---一开始就进行创建（main函数之前就进行了创建）
class CentralCache
{
public:
	//返回指针挥着引用都是可以的
	static CentralCache* GetInstance()
	{
		return &_inst;
	}


	// 从中心缓存获取一定数量的对象给thread cache
	size_t FetchRangeObj(void*& start, void*& end, size_t n, size_t byte_size);

	// 从SpanList或者page cache获取一个span
	Span* GetOneSpan(SpanList& list, size_t byte_size);


	// 将一定数量的对象释放到span跨度
	void ReleaseListToSpans(void* start, size_t byte_size);
private:
	SpanList _spanLists[NFREELISTS]; // 按对齐方式映射    这里的span被切过了，并且有一部分小对象已经切分出去了

private:

	CentralCache() = default;
	CentralCache(const CentralCache&) = delete;
	CentralCache& operator=(const CentralCache&) = delete;

	static CentralCache _inst;
};
