#include "ObjectPool.h"
#include "ConcurrentAlloc.h"
#include <vector>

void func1()
{
	std::vector<void*> temp;

	for (size_t i = 0; i < 10; ++i)
	{
		temp.push_back(ConcurrentAlloc(17));
	}
	for (size_t i = 0; i < 10; ++i) {
		ConcurrentFree(temp[i]);
	}

}

void func2()
{
	std::vector<void*> temp;
	for (size_t i = 0; i < 20; ++i)
	{
		temp.push_back(ConcurrentAlloc(5));
	}
	for (size_t i = 0; i < 20; ++i) {
		ConcurrentFree(temp[i]);
	}

}

void TestThreads()
{
	std::thread t1(func1);
	std::thread t2(func2);


	t1.join();
	t2.join();
}

void TestSizeClass()
{
	cout << SizeClass::Index(1035) << endl;
	cout << SizeClass::Index(1025) << endl;
	cout << SizeClass::Index(1024) << endl;
}

void TestConcurrentAlloc()
{
	/*void* ptr0 = ConcurrentAlloc(5); *///这段代码处是有一个隐蔽的错误的，就目前而言，如果一开始就申请8字节，那么从PageCache中申请上来的页
	//就会被按照8字节都切分好然后挂接在CentralCache中
	//问题就在于如果一开始就只是申请5字节呢，那么你难道要把PageCache中申请上来的页切成一个个5字节的小内存然后挂接在CentralCache中吗？
	//所以还需要一个对齐数，当申请5字节的时候，也依旧按照8字节将PageCache申请上来的页进行切分GetOneSpan()
	void* ptr1 = ConcurrentAlloc(8);
	void* ptr2 = ConcurrentAlloc(8);

	//ConcurrentFree(ptr0);
	ConcurrentFree(ptr1);
	ConcurrentFree(ptr2);
}

void TestBigMemory()
{
	void* ptr1 = ConcurrentAlloc(65 * 1024);
	ConcurrentFree(ptr1);

	//也有可能申请的是一块大于128页的内存
	void* ptr2 = ConcurrentAlloc(129 * 4 * 1024);
	ConcurrentFree(ptr2);
}

//int main()
//{
	// TestObjectPool();
	// TestThreads();
	// TestSizeClass();
	// TestConcurrentAlloc();
	//TestBigMemory();
	// return 0;
//}