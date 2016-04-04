# CSAPP_Lab
CSAPP(一书配套的实验中，其中3个经典实验的源码 +  基于epoll模型的http服务器<br>
##http：基于epoll模型的http服务器<br>
采用epoll模型，实现了统一事件源，并通过时间堆管理定时器回收非活动连接。<br>
通过一个线程池实现对任务的处理，然后使用状态机解析HTTP报文，请求了静态文件。<br>
<br>
##malloclab-handout：基于分离适配算法的内存分配器<br>
采用双向链表结构维护分配器，每次分配一个内存块时，通过链表头指针查找到一个大小合适的块，并进行可选的分割。性能较隐式空闲链表分配器提升了大约20%。
<br>
##proxylab-handout：实现了一个简单的代理程序
<br>
##shlab-handout：Tiny Shell<br>
实现了一个简易shell程序，主要涉及进程管理和信号处理。定义了一个数据结构管理job，实现了job的add，delete，fg，bg等功能。并正确的处理了SIGINT，SIGCHLD，SIGTSTP信号。
