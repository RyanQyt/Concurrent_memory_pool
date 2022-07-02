# Concurrent_memory_pool
参考tcmalloc，使用三级缓存实现的高并发内存池。

# 项目难点
+ 效率问题：项目整体申请和释放内存**效率**需要**优于malloc**。
  本项目使用了线程缓存(ThreadCache), 中心缓存(CentralCache), 页缓存(PageCache)三级缓存
  
+ 内存碎片问题：项目产生**内存碎片**需要**尽可能少**。
+ 线程安全。内存池在多线程环境下运行，需要考虑线程安全问题。


# 性能测试
相关内容在benchmark.cpp
测试环境: Vistual studio 2019, i5-4790K

使用malloc和本项目内存池分别申请400000次内存，本项目内存池明显优于malloc
![benchmark](https://user-images.githubusercontent.com/99704932/177002648-3e8857cc-56bb-4f63-95bf-1ea782e48e18.png)
