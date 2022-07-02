#pragma once
#include "Common.h"
#include "PageMap.h"

class PageCache
{
public:
	static PageCache* GetInstance()
	{
		return &_sInst;
	}
	// 向系统申请k页内存挂到自由链表
	void* SystemAllocPage(size_t k);

	Span* NewSpan(size_t k);

	//获取从对象到span的映射
	Span* MapObjectToSpan(void* obj);

	void ReleaseSpanToPageCache(Span* span);
private:
	SpanList _spanList[NPAGES];	// 按页数映射

	//std::map<PageID, Span*> _idSpanMap; //这里是有可能多个页都映射同一个Span的
	//tcmalloc 基数树 效率更高

	TCMalloc_PageMap2<32-PAGE_SHIFT> _idSpanMap;



	std::recursive_mutex _mtx;
private:
	PageCache() = default;
	PageCache(const PageCache&) = delete;
	PageCache& operator=(const PageCache&) = delete;

	static PageCache _sInst;
};




