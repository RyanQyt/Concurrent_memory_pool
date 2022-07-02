//进行基准的测试，测试项目在高并发的情况下和malloc和free进行对比
#include "ConcurrentAlloc.h"

void BenchmarkMalloc(size_t ntimes, size_t nworks, size_t rounds)
{
	std::vector<std::thread> vthread(nworks);
	size_t malloc_costtime = 0;
	size_t free_costtime = 0;

	for (size_t k = 0; k < nworks; ++k)
	{
		vthread[k] = std::thread([&, k]() {
			std::vector<void*> v;
			v.reserve(ntimes);

			for (size_t j = 0; j < rounds; ++j)
			{
				size_t begin1 = clock();
				for (size_t i = 0; i < ntimes; i++)
				{
					v.push_back(malloc(16));
				}
				size_t end1 = clock();

				size_t begin2 = clock();
				for (size_t i = 0; i < ntimes; i++)
				{
					free(v[i]);
				}
				size_t end2 = clock();
				v.clear();

				malloc_costtime += end1 - begin1;
				free_costtime += end2 - begin2;
			}
		});
	}

	for (auto& t : vthread)
	{
		t.join();
	}

	printf("%u threads, %u rounds, malloc %u times per round: cost: %u ms\n",
		nworks, rounds, ntimes, malloc_costtime);

	printf("%u threads, %u rounds, free %u times per round: cost: %u ms\n",
		nworks, rounds, ntimes, free_costtime);

	printf("%u threads, malloc&free %u times, totally cost: %u ms\n",
		nworks, nworks*rounds*ntimes, malloc_costtime + free_costtime);
}


// 单轮次申请释放次数 线程数 轮次
void BenchmarkConcurrentMalloc(size_t ntimes, size_t nworks, size_t rounds)
{
	std::vector<std::thread> vthread(nworks);
	size_t malloc_costtime = 0;
	size_t free_costtime = 0;

	for (size_t k = 0; k < nworks; ++k)
	{
		vthread[k] = std::thread([&]() {
			std::vector<void*> v;
			v.reserve(ntimes);

			for (size_t j = 0; j < rounds; ++j)
			{
				size_t begin1 = clock();
				for (size_t i = 0; i < ntimes; i++)
				{
					v.push_back(ConcurrentAlloc(16));
				}
				size_t end1 = clock();

				size_t begin2 = clock();
				for (size_t i = 0; i < ntimes; i++)
				{
					ConcurrentFree(v[i]);
				}
				size_t end2 = clock();
				v.clear();

				malloc_costtime += end1 - begin1;
				free_costtime += end2 - begin2;
			}
		});
	}

	for (auto& t : vthread)
	{
		t.join();
	}

	printf("%u threads, %u rounds, malloc %u times per round: cost: %u ms\n",
		nworks, rounds, ntimes, malloc_costtime);

	printf("%u threads, %u rounds, free %u times per round: cost : %u ms\n",
		nworks, rounds, ntimes, free_costtime);

	printf("%u threads, malloc&free %u times, totally cost: %u ms\n",
		nworks, nworks*rounds*ntimes, malloc_costtime + free_costtime);
}

int main()
{
	cout << "==========================================================" << endl;
	BenchmarkMalloc(10000, 4, 10);
	cout << endl << endl;

	BenchmarkConcurrentMalloc(10000, 4, 10);
	cout << "==========================================================" << endl;

	return 0;
}