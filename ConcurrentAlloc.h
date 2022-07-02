#pragma once

//主要是拿它在高并发的情况下和malloc和free进行对比性能
#include "Common.h"
#include "ThreadCache.h"
#include "PageCache.h"

//void* tcmalloc(size_t size)
static void* ConcurrentAlloc(size_t size)
{
	try
	{
		if (size > MAX_BYTES)
		{
			// 向PageCache要，这里也是要加锁的，但是总是申请这么大的空间的场景毕竟是少数的，所以并不影响效率
			//如果你要65KB，那么就给你68KB，因为在PageCache中，都是以页为单位的
			size_t npage = SizeClass::RoundUp(size) >> PAGE_SHIFT;
			//即使是大于128页的也使用的是同样的逻辑，所以对于NewSpan代码需要调整，那么这里NewSpan()既然对大于128页做出了调整,在释放的时候
			//对于ReleaseSpanToPageCache()，也应该作出调整
			Span* span = PageCache::GetInstance()->NewSpan(npage);
			span->_objsize = size; //向17页和129页这两个例子

			void* ptr = (void*)(span->_pageId << PAGE_SHIFT);
			return ptr;
		}
		else
		{
			if (tls_threadcache.get() == nullptr)
			{
				tls_threadcache = std::unique_ptr<ThreadCache>(new ThreadCache());
			}

			return tls_threadcache->Allocate(size);
		}
	}
	catch (const std::exception& e)
	{
		cout << e.what() << endl;
	}
	return nullptr;
}

//释放的时候不应该有后面的大小，因为正常的free就是没有的
static void ConcurrentFree(void* ptr)
{
	try
	{
		Span* span = PageCache::GetInstance()->MapObjectToSpan(ptr);
		size_t size = span->_objsize;

		if (size > MAX_BYTES)
		{
			PageCache::GetInstance()->ReleaseSpanToPageCache(span);
		}
		else
		{
			assert(tls_threadcache.get());
			tls_threadcache->Deallocate(ptr, size);
		}
	}
	catch (const std::exception& e)
	{
		cout << e.what() << endl;
	}
}