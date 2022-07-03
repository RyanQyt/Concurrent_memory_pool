# Concurrent_memory_pool
参考tcmalloc，使用三级缓存实现的高并发内存池。

## 项目整体框架
使用三级缓存：线程缓存(ThreadCache), 中心缓存(CentralCache), 页缓存(PageCache)实现高并发内存池。
+ ThreadCache：使用线程局部存储(thread local storage, tls)，保证每个线程都有自己的缓存。  
  线程申请内存时( < 64 KB)，会优先在这里申请，因为这里申请不需要加锁，每个线程独享一个Cache，这也是并发内存池高效的原因之一。  
  本质上，ThreadCache管理的是不同的内存桶，每个内存桶内都是给定长度的、可用的内存链表(freelist)。内存桶的定长保证了申请64B以上的内存时，内部碎片**小于12.5%**.  
  
+ CentralCache：所有ThreadCache共享的Cache。  
  申请内存时，如果线程申请的内存 < 64KB, 但是本地线程中对应的freelist为空的时候，线程会向CentralCache申请。  
  CentralCache管理的同样是定长内存桶，但是每个内存桶里保存的是可用的span(数个page长度的内存)。  
  
+ PageCache：管理不同Page的span。  
  当线程申请 >= 64KB 的内存时，或者CentralCache中的span不够时，会向这里申请内存。   
  PageCache中内存桶保存的是不同长度的span，span中每页和span的对应关系由基数树维护。  

## 申请内存的流程
1. 线程申请小内存时（< 64KB）,会先在ThreadCache里申请，无锁地拿ThreadCache对应桶里面的内存，如果ThreadCache中没有，再到CentralCache中对应的桶里的span拿。  
2. 如果CentralCache的spanlist仍然为空，就去PageCache中去找size对应大小的span  
3. 如果PageCache中依然没有对应空闲span，就向列表中更大的span找，从更大的span中切一块下来，切出来的一块给CentralCache，把这个span内部切成符合要求的小块，并在基数树中建立list对应的映射，多出来的一块挂到PageCache对应spanlist上  
4. 线程如果申请稍大的内存( <= 128page, 512KB), 会直接在Page中申请相应页数的page。  
5. 线程如果申请更大的内存( > 128page),会直接使用系统调用(VirtualAlloc, mmap, brk等)申请

## 释放内存的流程
1. 和申请内存时基本相同，如果释放小内存，先放到ThreadCache，如果ThreadCache里面太多了，就会批量归还到CentralCache中对应的span中
2. CentralCache中一个span中的所有内存块都被还回来了，CentralCache就会把这个span还回到PageCache中，并将这个span内存地址前后相邻的span合并
3. 如果申请大内存，直接使用VirtualFree，munmap，brk等进行释放。


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
