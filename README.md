# Priority_Threads_Pool

支持优先级调度SPF和先来先服务FCFS的线程池，基于开源项目ctpl (https://github.com/vit-vit/CTPL)拓展

#文件说明
PriorityThreadsPool文件夹：vs2019工程项目
Priority_Threads_Pool.h 线程库文件
test.cpp 测试用例

#实现新功能

1.自定义优先级的任务队列，在push时指定

```
#include "priorityThreadPool.h"

PriorityThreadPool pl(2);
pl.push(1,test1,params...);//第一个函数是优先级，后面是执行函数和参数表
```

2.删除了传线程id的参数，无需二次包装
```
//需要执行的任务
void test() {
    Sleep(2000);
    cout << "helloworld" << endl;
}
//ctpl库中函数必须接收线程id参数，因此需要包装一层函数
void package(int id) {
    test();
}

int main(){
	//ctpl中
	ctpl::thread_pool p(2);//2个线程的线程池
    p.push(package);

	//ptpl中
	FCFS_ThreadPool pl(2);
	pl.push(test);
	
}
```
3.任务队列独立封装，提高了可拓展性
```
//改写TaskQueue类实现自定义任务调度，Priority_Threads_Pool在此基础上实现
class TaskQueue{
...
}
```

4.脱离了对boost库的依赖，基于STL queue库实现了线程安全的任务队列

5.支持完美转发(ctpl已实现，Priority_Threads_Pool保留了这一特点)

6.根据自己理解，修改了变量命名，并且在基类上提供了详细的中文注释

#更新计划
· 将实现自适应大小的线程池，线程长时间阻塞时自动释放，自动均衡负载。

#快速开始
```
#include <iostream>
#include <functional>
#include <Windows.h>
#include "priorityThreadPool.h"
using namespace std;
using namespace ptpl;
void test1() {
	Sleep(2000);
	cout << "helloworld" << endl;
}
void test2(int a) {
	Sleep(2000);
	cout << "prioritytask" <<a<< endl;
}
int main() {
	FCFS_ThreadPool pl(2);
	pl.push(test1);
	pl.push(test1);
	pl.push(test1);
	pl.push(test1);
	pl.push(test1);
	pl.push(test1);
	
	PriorityThreadPool ptpl(2);
	ptpl.push(1,test1);
	ptpl.push(1,test1);
	ptpl.push(1,test1);
	ptpl.push(1,test1);
	ptpl.push(2,test2,3);
	ptpl.push(2,test2,4);
	return 0;
}
```

#原理描述
1.任务队列：将传入的函数使用`std::package_task`转为可执行对象保存在任务队列中，使用优先队列来实现优先级调度。

2.线程执行：每一个线程完成**从任务队列中获取任务-执行任务**的循环，当任务队列为空时通过条件变量阻塞，直到提交新的任务后将线程唤醒。
