# Concurrent_memory_pool
参考tcmalloc，使用三级缓存实现的高并发内存池。

## 项目整体框架
使用三级缓存：线程缓存(ThreadCache), 中心缓存(CentralCache), 页缓存(PageCache)实现高并发内存池。
+ ThreadCache：使用线程局部存储(thread local storage, tls)，保证每个线程都有自己的缓存。  
  线程申请内存时( < 64 KB)，会优先在这里申请，因为这里申请不需要加锁，每个线程独享一个Cache，这是并发内存池高效的原因之一。  
  本质上，ThreadCache管理的是不同的内存桶，每个内存桶内都是给定长度的、可用的内存链表(freelist)。内存桶的定长保证了申请64B以上的内存时，内部碎片**小于12.5%**.  
  
+ CentralCache：所有ThreadCache共享的Cache。  
  申请内存时，如果线程申请的内存 < 64KB, 但是本地线程中对应的freelist为空的时候，线程会向CentralCache申请。  
  CentralCache管理的同样是定长内存桶，但是每个内存桶里保存的是可用的span(数个page长度的内存)。  
  
+ PageCache：管理不同Page的span。  
  当线程申请 >= 64KB 的内存时，或者CentralCache中的span不够时，会向这里申请内存。   
  PageCache中内存桶保存的是不同长度的span，span中每个page和span的对应关系由基数树维护，读取基数树是无锁的，这也是并发内存池高效的原因之一。  

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
+ 效率问题：在线程安全的前提下，项目整体申请和释放内存**效率优于malloc**。  
+ 内存碎片问题：项目产生**内存碎片**需要**尽可能少**。  

### 项目效率为何较高？
  ThreadCache使用线程局部存储(thread local storage, tls)，保证每个线程都有自己的缓存，这一级缓存对于线程是无锁访问的。  
  CentralCache管理各个长度的span，分配时使用桶锁降低锁的粒度。  
  PageCache使用基数树管理页与sapn的映射关系，读基数树是无锁的。  
  使用查表（基数树，对齐大小）、位运算（计算classsize）等方式加快了计算速度。  

### 如何保证线程安全？
  + ThreadCache这一层，因为线程都是访问自己的ThreadCache，所以没有线程安全的问题。
  + CentralCache在分配给ThreadCache或回收ThreadCache的内存时，使用桶锁保证只有一个线程能访问到对应定长内存桶
  + PageCache使用的基数树虽然能够被多个线程访问到，但是不会出现一个线程读的同时，另一个线程对这个内容写；或者多个线程同时写同一个部分的内容。原因如下：
    线程写基数树的场景是span的分配（写page和span对应关系）和span合并时候，这两个地方都使用了PageCache内部的大锁，**不会出现多个线程同时写的问题**。  
    线程在归还某一个page的上的内存块的时候，需要读基数树，但不会有线程对这部分进行写操作。因为所有写的场景都不满足：待归还内存对应的span已经被分配在CentralCache中，不会在PageCache中又被分配；这个span在被使用中，也不会被进行合并操作，所以也**不会出现一个线程读的同时，另一个线程写**的问题。
    
### 如何减少内存碎片？
  + 什么是内存碎片？
  分配但没有使用上的内存部分。比如申请100B的内存，分配了112B的内存，那么就有12B的内存没有使用。  
  首先分配的内存必须要被4B或8B整除，其次，如果都从大的内存块上切出内存碎片最小的块来分配，那么大块内存会被切的非常碎，同样造成了碎片。同时，由效率上的考量，我们提前准备了一些大小的内存块(size_class)，当申请内存的时候，找到申请内存大小对应的内存块大小，直接从Cache拿即可。  
  
  + 内部内存碎片
  size_class的设置保证在申请 > 64B的内存碎片时内部碎片率 < 12.5 %.
  对于申请的内存，每个内存都以一定的对齐长度(align)进行对齐, 比如[129B, 1024B]范围内的就是用16B的对齐。申请的内存先以align对齐，就得到sizeclass，比如129B对齐到144B。可以算出，这种方式得到的最大的内存碎片就是align-1的大小，内存碎片率为(align - 1) / (sizeclass)。sizeclass取最小值的时候碎片率取最大，放缩一下不等式 =>  align / min(sizeclass) < 0.125,即sizeclass > 8 * align就满足要求。比如：1KB的对齐长度，对应的内存真实长度至少为8KB  
  在程序的计算中，对于align是查表计算的，对于sizeclass使用位运算加快速度。
  
  + 外部碎片
  所有的小块内存都是至少一个page的span切出来的，但是如果当span的大小不能整除classsize的时候，就会发生外部碎片。比如需要3KB的内存块，结果使用了1个page长度的span去切，那么能得到1块满足要求的内存，产生1KB大小的碎片。  
  所以需要使用多个page的span去切分，比如对3KB的内存，使用4个page去切，内存碎片率1/16。同时需要注意：span不能太大，切出来的内存数量不能太多。  
  参考TCMalloc外部碎片策略，将外部碎片率降低到最多12.5%：
  - 32KB以上的内存块，因为一个page是4KB，由align / span 粗算碎片率，碎片率一定< 0.125  
  - 128B以下的内存块，可以在64KB以下的span中最少找到一个能整除的，外部碎片率 = 0.  
  - 128B ~ 32KB的内存块，都从最大64KB的span中切，如果64KB能整除则已，不能整除就把得到的内存块数量向下取整，由此得到的span大小向上取整（比如64KB切17KB大小内存，只能切三块，所以得到的span大小为51KB，含page数量为12.75，因为span为4KB整数倍所以向上取整为13），外部碎片率最大为 pagesize / min(classsize) = 4 / 32 = 0.125。  
  综上，外部碎片的碎片率也是最大12.5%。  
   


  

## 性能测试
相关内容见benchmark.cpp
测试环境: Vistual studio 2019, i5-4790K

使用malloc和本项目内存池分别申请400000次内存，内存池效率明显优于malloc
![benchmark](https://user-images.githubusercontent.com/99704932/177002648-3e8857cc-56bb-4f63-95bf-1ea782e48e18.png)
