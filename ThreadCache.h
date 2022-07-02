#pragma once

#include "Common.h"


class ThreadCache
{
public:
	// 申请和释放内存对象
	void* Allocate(size_t size);
	void Deallocate(void* ptr, size_t size);

	// 从中心缓存获取对象
	void* FetchFromCentralCache(size_t index, size_t size);

	// 释放对象时，链表过长时，回收内存回到中心缓存
	void ListTooLong(FreeList& list, size_t size);
	~ThreadCache();
private:
	FreeList _freeLists[NFREELISTS];
};

// 使用Thread_local关键字代替实现_declspec的tls(thread local storage)，并使用unique_ptr封装，确保线程销毁的时候ThreadCache被析构，_freeList中的内存块被回收
thread_local static std::unique_ptr<ThreadCache> tls_threadcache;

// map<int, ThreadCache> idCache;
// TLS  thread local storage---xian