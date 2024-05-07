#pragma once
#include <queue>
#include <functional>
#include <future>

#include <mutex>
#include <memory>
#include <atomic>
using namespace std;
//任务队列抽象类
template<class C>
class Task {
protected:
	C taskque;

	mutex x;//任务队列互斥锁

	template <typename F, typename... PARAM>
	auto funcPackage(function<void(void)>*& _f,F&& f, PARAM&&... params) {
		auto pck = make_shared<packaged_task<decltype(f(params...))(void)> >(
			bind(forward<F>(f), forward<PARAM>(params)...)
			);
		 _f = new function<void(void)>([pck]() {(*pck)(); });
		return pck->get_future();
	}
public:
	int size() {
		unique_lock<mutex>lck(this->x);
		return (int)taskque.size();
	}
	bool empty() {
		unique_lock<mutex>lck(this->x);
		return taskque.empty();
	}
	bool pop(function<void(void)>*& t) {
		if (taskque.empty())return false;
		else {
			unique_lock<mutex>lck(this->x);
			t = taskque.front();
			taskque.pop();
			return true;
		}
		return false;
	}
	virtual void clear() {
		//unique_lock<mutex>lck(this->x);
		function<void(void)>* f;
		while (pop(f)) {
			delete f;
		}
	}
	//把函数包装成可调用对象，加入到队列中
	template <typename F, typename... PARAM>
	auto push(F&& f, PARAM&&... params) {
		function<void(void)>* _f;
		auto pf=funcPackage(_f,f,params...);
		{
			unique_lock<mutex>lck(x);
			taskque.push(_f);
		}
		return pf;
	}


};
class FCFSTaskque :public Task<queue<function<void(void)>*>> {

};

//优先任务队列
struct PriorityTask {
	int priority = 1;
	function<void(void)>* task;
	PriorityTask(int n, function<void(void)>* f) :priority(n), task(f) {};
};
class cmp {
public:
	bool operator()(PriorityTask a, PriorityTask b) {
		return a.priority < b.priority;
	}
};
class PriorityTaskque:public Task<priority_queue<PriorityTask,vector<PriorityTask>,cmp> > {
public:
	bool pop(function<void(void)>*& t){
		if (taskque.empty())return false;
		else {
			unique_lock<mutex>lck(this->x);
			t = taskque.top().task;
			taskque.pop();
			return true;
		}
		return false;
	}
	void clear() {
		//unique_lock<mutex>lck(this->x);
		function<void(void)>* f;
		while (this->pop(f)) {
			delete f;
		}
	}
	template <typename F, typename... PARAM>
	auto push(int pri,F&& f, PARAM&&... params) {
		function<void(void)>* _f;
		auto pf = funcPackage(_f, f, params...);
		{
			unique_lock<mutex>lck(x);
			taskque.push(PriorityTask(pri,_f));
		}
		return pf;
	}
};
//定义抽象模板类
class Threadpool {
protected:
	//线程互斥锁
	mutex mx;
	condition_variable cv;
	//线程池
	vector<unique_ptr<thread>>threadpool;
	//线程停止信号
	vector<shared_ptr<atomic<bool>>> stopsign;
	//空闲线程数量
	atomic<int> idleThreadNum;
	//线程池状态
	atomic<bool>isDone;
	atomic<bool>isStop;
	//启动i号线程
	template <class C>
	void startThread(int i,C& tasks) {
		shared_ptr<atomic<bool>>stop_sign_copy(this->stopsign[i]);
		//创建线程任务
		auto threadtask = [this, i, stop_sign_copy]() {
			atomic<bool>& _sign = *stop_sign_copy;
			function<void(void)>* _func;
			bool tasknoempty = tasks.pop(_func);
			while (true) {
				//取任务成功，执行任务
				while (tasknoempty) {
					(*_func)();
					//检查停止信号
					if (_sign)return;
					else tasknoempty = tasks.pop(_func);
				}
				//空闲线程+1
				unique_lock<mutex>lck(this->mx);
				++this->idleThreadNum;
				this->cv.wait(lck, [this, &_func, &_sign, &tasknoempty]() {
					tasknoempty = tasks.pop(_func);
					return tasknoempty || this->isDone || _sign;
					});
				--this->idleThreadNum;
				//已无任务执行，结束线程任务
				if (!tasknoempty) {
					return;
				}
			}
		};
		//线程任务交付线程执行
		this->threadpool[i].reset(new thread(threadtask));
	}
	//线程池标志量初始化
	void init() {
		isStop = false;
		isDone = false;
		idleThreadNum = 0;
	}
	//设置线程池大小并且启动线程
	template<class C>
	void resize(int n,C& tasks) {
		//运行状态下才能生效
		if (!isStop && !isDone) {
			int oldsize = threadpool.size();
			if (n >= oldsize) {
				threadpool.resize(n);
				stopsign.resize(n);
				for (int i = oldsize; i < n; i++) {

					stopsign[i] = make_shared<atomic<bool>>(false);
					startThread<C>(i,&tasks);
				}
			}
			else {
				for (int i = n; i < oldsize; i++) {
					(*stopsign[i]) = true;
					threadpool[i]->detach();
				}
				{
					//激活所有线程，使所有释放的线程结束。
					unique_lock<mutex>lck(mx);
					cv.notify_all();
				}
				//安全的删除
				threadpool.resize(n);
				stopsign.resize(n);
			}
		}
	}
	//获取空闲线程数量
	int GetidleThreadNumber() {
		return idleThreadNum;
	}
	//获取线程池总数
	int pool_size() {
		return threadpool.size();
	}
	//获取剩余任务数量
	template<class C>
	int GetTaskQueueSize(C& tasks) {
		return tasks.size();
	}
	//清空任务队列
	template<class C>
	void clear_allTask(C&tasks) {
		tasks.clear();
	}
	//停止线程池
	template<class C>
	void stop(C&tasks,bool iswait = false) {
		if (!iswait) {//强制停止，不继续处理任务队列的剩余任务
			//已经停止的情况
			if (isStop)return;
			isStop = true;
			for (auto& i : stopsign) {
				(*i) = true;
			}
			tasks.clear();

		}
		else {
			if (isStop || isDone)return;
			isDone = true;
		}
		{
			unique_lock<mutex>lck(mx);
			cv.notify_all();
		}

		for (auto& i : threadpool) {
			if (i->joinable()) {

				i->join();

			}
		}

		tasks.clear();
		threadpool.clear();
		stopsign.clear();

	}
	template <class C,typename F, typename... PARAM>
	auto push(C&tasks,F&& f, PARAM&&... params) {
		auto pck = tasks.push(forward<F>(f), forward<PARAM>(params)...);
		//激活一个等待中的线程
		unique_lock<mutex>lck(mx);
		cv.notify_one();
		return pck;
	}
};
