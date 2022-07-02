# Concurrent_memory_pool
参考tcmalloc，使用三级缓存实现的高并发内存池。

## 项目整体框架
使用三级缓存：线程缓存(ThreadCache), 中心缓存(CentralCache), 页缓存(PageCache)实现高并发内存池。
+ ThreadCache：使用线程局部存储(thread local storage, tls)，保证每个线程都有自己的缓存。  
  本质上，ThreadCache管理的是不同的内存桶，每个内存桶内都是给定长度的、可用的内存链表(freelist)。内存桶的定长保证了申请64B以上的内存时，内部碎片**小于12.5%**.  
  线程申请内存时，会优先在这里申请，因为这里申请不需要加锁，每个线程独享一个Cache，这也是并发内存池高效的原因之一。  
+ CentralCache：所有ThreadCache共享的Cache。  
  ThreadCache管理的同样是定长内存桶，但是每个内存桶里保存的是可用的span(数个page长度的内存)。  
  申请内存时，如果线程申请的内存 < 64KB, 但是本地线程中对应的freelist为空的时候，线程会向CentralCache申请。  
+ PageCache：管理Page的Cache。  
  PageCache中内存桶保存的是不同长度的span，



## 项目难点
+ 效率问题：项目整体申请和释放内存**效率优于malloc**。  
  本项目使用了  
  ThreadCache使用线程局部存储(thread local storage, tls)，保证每个线程都有自己的缓存，这一级缓存对于线程是无锁访问的。  
  CentralCache管理各个align的span，分配时使用桶锁降低锁的粒度。  
  PageCache使用基数树管理页与sapn的映射关系，这里同样是无锁的。  
  
  
+ 内存碎片问题：项目产生**内存碎片**需要**尽可能少**。  
+ 线程安全。内存池在多线程环境下运行，需要考虑线程安全问题。  


## 性能测试
相关内容见benchmark.cpp
测试环境: Vistual studio 2019, i5-4790K

使用malloc和本项目内存池分别申请400000次内存，内存池效率明显优于malloc
![benchmark](https://user-images.githubusercontent.com/99704932/177002648-3e8857cc-56bb-4f63-95bf-1ea782e48e18.png)
