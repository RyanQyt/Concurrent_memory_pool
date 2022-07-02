#include "CentralCache.h"
#include "PageCache.h"
//单例模式的定义我们一般放在.cpp中,并且以后想要获取该对象就可以调用GetInstance()这个接口

CentralCache CentralCache::_inst;

Span* CentralCache::GetOneSpan(SpanList& list, size_t size)
{
	//但是我怎么知道这个Span是否合适①如果它也没有是不是应该去PageCache中要，如果有的话是否合适（是否还有足够的空间）
	//先在spanlist中取找还有内存的span
	Span* it = list.Begin();
	while (it != list.End())
	{
		//当Span中的memory为空的时候，就表示用完了
		if (it->_list)
		{
			return it;
		}
		it = it->_next;
	}
	//走到这里的逻辑其实是该位置就没有Span，要么就是找到的这个Span里面的内存都是用完了
	//如果走到这里，代表span都没有内存了，那么此时就只能找PageCache
	//那么要多少页呢？如果要的内存大的话，那么是不是应该多要几个页，如果要的内存小的话，那么可能1个或者2个页就可以满足了

	//这里返回的页应该是已经被切好的
	Span* span = PageCache::GetInstance()->NewSpan(SizeClass::NumMovePage(size));

	//切分好挂接在list中
	//这里也就可以清楚CentralCache和PageCache中所挂接的Span有什么不同（CentralCache中span是切好的且一部分小内存已经被分出去，
	//PageCache中的span一个完整的大块内存，并不需要切）
	//切的时候，应该第一步算一下这个span能切多少块小内存
	char* start = (char*)(span->_pageId << PAGE_SHIFT); //先算出这块span的起始地址
	char* end = start + (span->_n << PAGE_SHIFT);
	//这里使用头插,这一块代码就可以完全的把该大块Span切割好
	while (start < end)
	{
		char* next = start + size;                            //start                       end
		NextObj(start) = span->_list;                         // ---------------------------
		// ---------------------------
		span->_list = start;

		start = next;
	}
	span->_objsize = size;
	list.PushFront(span); //还应该把这个span链接在链表中
	//如果最后切割出来的小内存不够size的大小，就应该丢弃掉      ☆☆☆
	//但是因为对齐规则，4096 / 8   4096 / 16   4096 / 128    4096 / 1024 应该都是整数所以这个问题就不需要在考虑
	return span;

}

//不管咋样都要依据size的大小来获取页的数量
//这里返回的是实际的数量
size_t CentralCache::FetchRangeObj(void*& start, void*& end, size_t n, size_t size)
{
	//直接在这个位置加锁是不好的，因为会导致取不同的spanList[i]都是串行化的
	size_t i = SizeClass::Index(size);
	std::lock_guard<std::mutex> lock(_spanLists[i]._mtx);//RAII

	Span* span = GetOneSpan(_spanLists[i], size);

	// 找到一个有对象的span，有多少给多少
	size_t j = 1;
	start = span->_list;
	void* cur = start;
	void* prev = start;
	while (j <= n && cur != nullptr)
	{
		prev = cur;
		cur = NextObj(cur);
		++j;
		span->_usecount++; //这一个是为归还所做准备的
	}

	span->_list = cur;
	end = prev;
	NextObj(prev) = nullptr; //顺便把链表的链接关系也改变，这样就不会再链接着原来的那一段了

	return j - 1;
}


void CentralCache::ReleaseListToSpans(void* start, size_t byte_size)
{
	//这里有一个问题就是如何能够知道当你的小块内存想要还回CentralCache的时候，每一块小内存属于那一块Span？
	//原因就在申请的时候可能内存来自于不同的Span，并且归还的顺序是不确定的
	//所以这个时候就是使用usecount的时候了，当一个Span全部都还回来的时候，就需要归还给PageCache，一遍可以让PageCache合成更大的页

	//所以这里需要引入map，为的就是能够让每一个小内存找到属于它的大块Span

	//因为对于start>>12，得到的结果就是页号（那一段页号里面的地址>>12都是这个页号）
	//这样就可以把对应的小内存还到属于它的Span
	size_t i = SizeClass::Index(byte_size);
	std::lock_guard<std::mutex> lock(_spanLists[i]._mtx);
	//一个一个还到对应的Span
	while (start)
	{
		//如果不提前保存就找不到下一个了
		void* next = NextObj(start);

		// 找start内存块属于哪个span
		Span* span = PageCache::GetInstance()->MapObjectToSpan(start);

		// 把对象插入到span管理的list中
		NextObj(start) = span->_list;
		span->_list = start;
		span->_usecount--;
		// _usecount == 0说明这个span中切出去的大块内存
		// 都还回来了
		//需要把这个Span删除掉，有可能这个Span在任意位置，所以这里也就看出了SpanList设计为双向带头循环链表
		if (span->_usecount == 0)
		{
			_spanLists[i].Erase(span);
			span->_list = nullptr; //把Span里面挂接的小内存关系解除掉，因为在PageCache中需要的是一块连续的内存，并不需要被切割好的
			PageCache::GetInstance()->ReleaseSpanToPageCache(span);
		}

		start = next;
	}

}
