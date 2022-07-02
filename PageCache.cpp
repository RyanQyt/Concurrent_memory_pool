#include "PageCache.h"

//对于PageCache也是要加锁的，这把锁的力度还要大，要把整个PageCache都锁住，因为当CentralCache申请Span的时候有可能对应的位置没有那么大的页
//所以他就想要去向后找，看是否有更大的页，然后把它进行切割，所以有可能会操作整个PageCache

PageCache PageCache::_sInst;
// 向系统申请k页内存

void* PageCache::SystemAllocPage(size_t k)
{
	return ::SystemAlloc(k); 
}



//要4页为什么不直接向系统申请一块4页的内存，而是申请一块128页的内存，然后切割呢？这样不是更加麻烦吗？
//好处就是---因为他们可以在合成一块大内存，这样就避免了你需要大内存的时候，由于内存碎片的问题导致无法给你
Span* PageCache::NewSpan(size_t k)
{
	//recursive_mutex专门给递归使用的锁
	std::lock_guard<std::recursive_mutex> lock(_mtx); //因为这里有一个递归调用，但是原先的锁资源的生命周期还没有到，就会报busy的错，所以需要换锁
	//针对直接申请大于NPAGES的大块内存，直接找系统要
	if (k >= NPAGES)
	{
		void* ptr = SystemAllocPage(k);
		//虽然是向系统要的这个超过128页的大块内存，但是使用的同样的逻辑，所以还是要返回一个Span的
		Span* span = new Span;
		span->_pageId = (ADDRES_INT)ptr >> PAGE_SHIFT;
		span->_n = k;

		_idSpanMap[span->_pageId] = span;
		return span;
	}
	if (!_spanList[k].Empty())
	{
		/*Span* it = _spanList[k].Begin();
		_spanList[k].Erase(it);
		return it;*/

		return _spanList[k].PopFront();
	}
	//splitspan是剩余的
	for (size_t i = k + 1; i < NPAGES; ++i)
	{
		// 大页给切小,切成k页的span返回
		// 切出i-k页挂回自由链表
		if (!_spanList[i].Empty())
		{
			//这里是头切，并不好，因为毕竟申请内存大概率都是申请的小页，那么后面的很大一部分页号都要重新映射
			//如果改为尾切，那么只需要改动很小一部分的映射关系就可以了
			//Span* span = _spanList[i].Begin();
			//_spanList[i].Erase(span);       //这个位置感觉不对      ☆☆☆
   //                                                   //100   |
			//Span* splitSpan = new Span;               //------------------------------------
			//splitSpan->_pageId = span->_pageId + k;   //------------------------------------
			//splitSpan->_n = span->_n - k;

			//span->_n = k;

			//_spanList[splitSpan->_n].Insert(_spanList[splitSpan->_n].Begin(), splitSpan);

			//return span;

			// 尾切出一个k页span
			Span* span = _spanList[i].PopFront();

			Span* split = new Span;
			split->_pageId = span->_pageId + span->_n - k;
			split->_n = k;

			// 改变切出来span的页号和span的映射关系
			for (PageID i = 0; i < k; ++i)
			{
				_idSpanMap[split->_pageId + i] = split;
			}

			//对于span来说只是修改一下页数，并不需要动对应的关系
			span->_n -= k;

			_spanList[span->_n].PushFront(span);

			return split;
		}
	}

	Span* bigSpan = new Span;
	void* memory = SystemAllocPage(NPAGES - 1);
	bigSpan->_pageId = (size_t)memory >> 12; //指针就是对字节的编号
	bigSpan->_n = NPAGES - 1;
	// 按页号和span映射关系建立
	for (PageID i = 0; i < bigSpan->_n; ++i)
	{
		PageID id = bigSpan->_pageId + i;
		_idSpanMap[id] = bigSpan;
	}
	_spanList[NPAGES - 1].Insert(_spanList[NPAGES - 1].Begin(), bigSpan);

	return NewSpan(k);
}



Span* PageCache::MapObjectToSpan(void* obj)
{
	PageID id = (ADDRES_INT)obj >> PAGE_SHIFT;
//	auto ret = _idSpanMap.find(id);
//	if (ret != _idSpanMap.end())
//	{
//		return ret->second;
//	}
//	else
//	{
//		//应该一定是能够找到的，因为每一块小内存都来自于Span
//		//如果找不到就出现大问题了，所以这里选择这种粗暴的方式直接断死
//		assert(false);  //☆这里有问题
//		return  nullptr; //这里应该一定不会返回空
//	}

	Span* span = _idSpanMap.get(id);
	if (span != nullptr)
	{
		return span;
	}
	else
	{
		assert(false);
		return nullptr;
	}
}

void PageCache::ReleaseSpanToPageCache(Span* span)
{
	//解决超过128页的大块内存的释放问题
	if (span->_n >= NPAGES)
	{
		//_idSpanMap.erase(span->_pageId);
		_idSpanMap.erase(span->_pageId);
		void* ptr = (void*)(span->_pageId << PAGE_SHIFT);
		SystemFree(ptr);
		delete span;
		return;
	}

	std::lock_guard<std::recursive_mutex> lock(_mtx);

	// 检查前后空闲span页，进行合并,解决内存碎片问题

	//合并的过程应该是一只进行下去的，直到前一个Span不是空闲的
	// 向前合并
	while (1)
	{
		PageID preId = span->_pageId - 1;
		//auto ret = _idSpanMap.find(preId);
		//// 如果前一个页的span不存在，系统未分配，结束向前合并
		//if (ret == _idSpanMap.end())
		//{
		//	break;
		//}
		Span* preSpan = _idSpanMap.get(preId);
		if (preSpan == nullptr)
		{
			break;
		}

		// 如果前一个页的span还在使用中，结束向前合并
		if (preSpan->_usecount != 0)
		{
			break;
		}

		// 开始合并... 此时都已经超过128页了，所以不要在进行合并了，没有意义了，没地方挂了
		if (preSpan->_n + span->_n >= NPAGES)
		{
			break;
		}

		//Span本身就是从CentralCache中返回来的，并没有插入到PageCache中
		// 从对应的span链表中解下来，再合并
		_spanList[preSpan->_n].Erase(preSpan);

		span->_pageId = preSpan->_pageId;
		span->_n += preSpan->_n;

		// 更新页之间映射关系，不需要全部都进行更改，只需要将preSpan中的进行改变就可以了
		for (PageID i = 0; i < preSpan->_n; ++i)
		{
			_idSpanMap[preSpan->_pageId + i] = span;
		}

		delete preSpan;
	}

	// 向后合并
	while (1)
	{
		PageID nextId = span->_pageId + span->_n; //如果想不通就找例子带入进行分析 PageID 100  一共2页 那么下一页就是102
		//auto ret = _idSpanMap.find(nextId);
		//if (ret == _idSpanMap.end())
		//{
		//	break;
		//}
		Span* nextSpan = _idSpanMap.get(nextId);
		if (nextSpan == nullptr)
		{
			break;
		}

		if (nextSpan->_usecount != 0)
		{
			break;
		}

		//和上面的逻辑一样，如果都已经是128页了，就不要再继续往更大的去合并了
		if (nextSpan->_n + span->_n >= NPAGES)
		{
			break;
		}

		_spanList[nextSpan->_n].Erase(nextSpan);

		span->_n += nextSpan->_n;
		for (PageID i = 0; i < nextSpan->_n; ++i)
		{
			_idSpanMap[nextSpan->_pageId + i] = span;
		}

		delete nextSpan;
	}

	// 合并出的大span，插入到对应的链表中
	_spanList[span->_n].PushFront(span);
}