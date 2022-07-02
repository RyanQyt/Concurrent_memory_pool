#pragma once

#include <iostream>
#include <exception>
#include <vector>
#include <time.h>
#include <assert.h>
#include<map>

#include <thread>
#include<mutex>
#include <algorithm>

#ifdef _WIN32
	#include <windows.h>
#else
	// ..
#endif


using std::cout;
using std::endl;

static const size_t MAX_BYTES = 64 * 1024;
static const size_t NFREELISTS = 184;
static const size_t NPAGES = 129;
static const size_t PAGE_SHIFT = 12; //这个可能也是会变的，因为在32位平台下面认为一页是4K，但是在64位平台下面一页更大一些


//条件编译最大的好处就是可以支持我们进行跨平台
#ifdef _WIN32
	typedef size_t ADDRES_INT;
#else
	typedef unsigned long long ADDRES_INT;
#endif // _WIN32


// 2 ^ 32 / 2 ^ 12
// 2 ^ 64 / 2 ^ 12
#ifdef _WIN32
	typedef size_t PageID;
#else
	typedef unsigned long long ADDRES_INT;
#endif // _WIN32



inline void*& NextObj(void* obj)
{
	return *((void**)obj);
}

//size_t Index(size_t size)
//{
//	if (size % 8 == 0)
//	{
//		return size / 8 - 1;
//	}
//	else
//	{
//		return size / 8;
//	}
//}

//如果频繁调用的话在极致上是认为提高了效率
// 8 + 7 = 15
// 7 + 7
// ...
// 1 + 7 = 8
static size_t Index(size_t size)
{
	return ((size + (2 ^ 3 - 1)) >> 3) - 1;
}

// 管理对齐和映射等关系
class SizeClass
{
public:
	// 控制在12%左右的内碎片浪费  也就在8字节对齐的时候浪费比较大，在其他时候基本上浪费率就控制在10% - 12%之间
	// [1,128] 8byte对齐    				 freelist[0,16)  申请内存的字节数在这个范围内我们还是按照8字节来进行映射
	// [129,1024] 16byte对齐				 freelist[16,72) 这里计算映射桶的个数：（1024-128）/ 16 = 56个桶
	// [1025,8*1024] 128byte对齐			 freelist[72,128) 这里也是对应映射56个桶
	// [8*1024+1,64*1024] 1024byte对齐  	 freelist[128,184)  56个桶
	// 对齐数最好选用2的整数倍

	//这里就是如果一开始申请的是5字节，如果不写这个它讲按照5字节对申请上来的span进行切割然后挂接在list中
	//但是我们想要的是[1,8] 字节内都要申请8字节的内存
	// ~7 0000 0111 取反 1111 1000
	//假设申请2字节 2+7=9   0000 1001
	//                      1111 1000
	//                      0000 1000  这样就能将对齐数都控制在8字节
	static inline size_t _RoundUp(size_t bytes, size_t align)
	{
		return (((bytes)+align - 1) & ~(align - 1));
	}

	// 对齐大小计算，浪费大概在1%-12%左右
	static inline size_t RoundUp(size_t bytes)
	{
		//assert(bytes <= MAX_BYTES);

		if (bytes <= 128){
			return _RoundUp(bytes, 8);
		}
		else if (bytes <= 1024){
			return  _RoundUp(bytes, 16);
		}
		else if (bytes <= 8192){
			return  _RoundUp(bytes, 128);
		}
		else if (bytes <= 65536){
			return  _RoundUp(bytes, 1024);
		}
		else
		{
			return _RoundUp(bytes, 1 << PAGE_SHIFT);
		}

		return -1;
	}



	static inline size_t _Index(size_t bytes, size_t align_shift)
	{
		return ((bytes + (1 << align_shift) - 1) >> align_shift) - 1;
	}

	// 计算映射的哪一个自由链表桶
	static inline size_t Index(size_t bytes)
	{
		assert(bytes <= MAX_BYTES);

		// 每个区间有多少个链
		static int group_array[4] = { 16, 56, 56, 56 };
		if (bytes <= 128){
			return _Index(bytes, 3);
		}
		else if (bytes <= 1024){
			return _Index(bytes - 128, 4) + group_array[0];
		}
		else if (bytes <= 8192){
			return _Index(bytes - 1024, 7) + group_array[1] + group_array[0];
		}
		else if (bytes <= 65536){
			return _Index(bytes - 8192, 10) + group_array[2] + group_array[1] + group_array[0];
		}

		assert(false);

		return -1;
	}

	// 一次从中心缓存获取多少个
	static size_t NumMoveSize(size_t size)
	{
		if (size == 0)
			return 0;

		// [2, 512] 一次批量移动多少个对象的上限值
		//小对象一次批量上线高
		//大对象一次批量上线低
		int num = MAX_BYTES / size;
		if (num < 2)
			num = 2;

		if (num > 512)
			num = 512;

		return num;
	}

	// 计算一次向系统获取几个页
	// 单个对象 8byte
	// ...
	// 单个对象 64KB
	static size_t NumMovePage(size_t size)
	{
		size_t num = NumMoveSize(size);
		size_t npage = num*size;

		npage >>= 12;
		if (npage == 0)
			npage = 1;

		return npage;
	}
};


class FreeList
{
public:
	void PushRange(void* start, void* end, int n)
	{
		NextObj(end) = _head;
		_head = start;
		_size += n;
	}

	void PopRange(void*& start, void*& end, int n)
	{
		start = _head;
		for (int i = 0; i < n; ++i)
		{
			end = _head;
			_head = NextObj(_head);
		}

		NextObj(end) = nullptr;
		_size -= n;
	}

	// 头插
	void Push(void* obj)
	{
		NextObj(obj) = _head;
		_head = obj;
		_size += 1;
	}

	// 头删
	void* Pop()
	{
		void* obj = _head;
		_head = NextObj(_head);
		_size -= 1;

		return obj;
	}

	bool Empty()
	{
		return _head == nullptr;
	}

	size_t MaxSize()
	{
		return _max_size;
	}

	void SetMaxSize(size_t n)
	{
		_max_size = n;
	}

	size_t Size()
	{
		return _size;
	}

private:
	void* _head = nullptr;
	size_t _max_size = 1;
	size_t _size = 0;
};

////////////////////////////////////////////////////////////
// Span
// 管理一个跨度的大块内存



//管理以页为单位的大块内存
struct Span
{
	PageID _pageId = 0;   // 页号
	size_t _n = 0;        // 页的数量

	Span* _next = nullptr;
	Span* _prev = nullptr;

	void* _list = nullptr;  //大块内存切小链接起来，这样回收回来的内存也方便链接
	size_t _usecount = 0;  // 使用计数，==0 说明所有对象都回来了

	//这个最重要的作用是什么？ 解决ConcurrentFree(void* ptr, size_t size)中需要传递两个参数的问题
	size_t _objsize = 0;   // 切出来的单个对象的大小，一块Span是专门为一种大小的内存服务
};

class SpanList
{
public:
	SpanList()
	{
		_head = new Span;
		_head->_next = _head;
		_head->_prev = _head;
	}

	Span* Begin()
	{
		return _head->_next;
	}

	Span* End()
	{
		return _head;
	}

	void PushFront(Span* span)
	{
		Insert(Begin(), span);
	}

	//并不是真正的删除，而是把关系解除掉
	Span* PopFront()
	{
		Span* span = Begin();
		Erase(span);

		return span;
	}

	void Insert(Span* cur, Span* newspan)
	{
		Span* prev = cur->_prev;
		// prev newspan cur
		prev->_next = newspan;
		newspan->_prev = prev;

		newspan->_next = cur;
		cur->_prev = newspan;
	}

	void Erase(Span* cur)
	{
		assert(cur != _head);

		Span* prev = cur->_prev;
		Span* next = cur->_next;

		prev->_next = next;
		next->_prev = prev;
	}

	bool Empty()
	{
		return _head->_next == _head;
	}

private:
	Span* _head;
public:
	std::mutex _mtx;
};



inline static void* SystemAlloc(size_t kpage)
{
#ifdef _WIN32
	void* ptr = VirtualAlloc(0, kpage*(1 << PAGE_SHIFT),
		MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
#else
	// brk mmap等
#endif

	if (ptr == nullptr)
		throw std::bad_alloc();

	return ptr;
}

inline static void SystemFree(void* ptr)
{
#ifdef _WIN32
	VirtualFree(ptr, 0, MEM_RELEASE);
#else
	// sbrk unmmap等
#endif
}