//实现一个定长的内存池（针对某一个具体的对象，所以起名字叫ObjictPool）
#pragma once 

#include"Common.h"

template<class T>
class ObjectPool
{
public:
	~ObjectPool()
	{
		//
	}
	//此时代码还存一个很大的问题：我们默认这里取的是前四个字节，但是在64位的平台下，需要取的应该是这块小内存的前8个字节来保存地址
	void*& Nextobj(void* obj)
	{
		return *((void**)obj); //对于返回的void*可以自动的适配平台
	}
	//申请内存的函数接口
	T* New()
	{
		T* obj = nullptr;
		//一上来首先应该判断freeList
		if (_freeList)
		{
			//那就直接从自由链表中取一块出来
			obj = (T*)_freeList;
			//_freeList = (void*)(*(int*)_freeList);
			_freeList = Nextobj(_freeList);
		}
		else
		{
			//表示自由链表是空的
			//那么这里又要进行判断，memory有没有
			if (_leftSize < sizeof(T)) //说明此时空间不够了
			{
				//那么就进行切割
				_leftSize = 1024 * 100;
				_memory = (char*)malloc(_leftSize);
				//对于C++来说，如果向系统申请失败了，则会抛异常
				if (_memory == nullptr)
				{
					throw std::bad_alloc();
				}
			}
			//进行memory的切割
			obj = (T*)_memory;
			_memory += sizeof(T); //这里如果想不通可以画一下图，很简单
			_leftSize -= sizeof(T); //表示剩余空间的大小
		}
		new(obj)T;  //定位new，因为刚申请的空间内如果是自定义类型是没有初始化的
		//所以需要可以显示的调用这个类型的构造函数，这个是专门配合内存池使用的
		return obj;
	}

	void Delete(T* obj)
	{
		obj->~T();//先把自定义类型进行析构
		//然后在进行释放，但是此时还回来的都是一块一块的小内存，无法做到一次性进行free，所以需要一个自由链表将这些小内存都挂接住
		//这里其实才是核心的关键点
		//对于指针来说，在32位的平台下面是4字节，在64位平台下面是8字节

		//头插到freeList
		//*((int*)obj)= (int)_freeList;
		Nextobj(obj) = _freeList;
		_freeList = obj;
	}
private:
	char* _memory = nullptr;//这里给char*是为了好走大小，并不是一定要给T*或者void*
	int _leftSize = 0; //为什么会加入这个成员变量呢？因为你的menory += sizeof(T),有可能就会造成越界的问题
	void* _freeList = nullptr; //给一些缺省值，让他的构造函数自己生成就可以了
};

struct TreeNode
{
	int _val;
	TreeNode* _left;
	TreeNode* _right;

	TreeNode()
		:_val(0)
		, _left(nullptr)
		, _right(nullptr)
	{}
};
void TestObjectPool()
{

	//验证还回来的内存是否重复利用的问题
	//	ObjectPool<TreeNode> tnPool;
	//TreeNode* node1 = tnPool.New();
	//TreeNode* node2 = tnPool.New();
	//cout << node1 << endl;
	//cout << node2 << endl;

	//tnPool.Delete(node1);
	//TreeNode* node3 = tnPool.New();
	//cout << node3 << endl;

	//cout << endl;

	//验证内存池到底快不快，有没有做到性能的优化
	//new底层本身调用的malloc,会一直和操作系统的底部打交道
	size_t begin1 = clock();
	std::vector<TreeNode*> v1;
	for (int i = 0; i < 1000000; ++i)
	{
		v1.push_back(new TreeNode);
	}
	for (int i = 0; i < 1000000; ++i)
	{
		delete v1[i];
	}
	size_t end1 = clock();


	//这里我们调用自己所写的内存池
	ObjectPool<TreeNode> tnPool;
	size_t begin2 = clock();
	std::vector<TreeNode*> v2;
	for (int i = 0; i < 1000000; ++i)
	{
		v2.push_back(tnPool.New());
	}
	for (int i = 0; i < 1000000; ++i)
	{
		tnPool.Delete(v2[i]);
	}
	size_t end2 = clock();

	cout << end1 - begin1 << endl;
	cout << end2 - begin2 << endl;
}
