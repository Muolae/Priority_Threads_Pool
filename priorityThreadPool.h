#pragma once
#include <queue>
#include <functional>
#include <future>
#include <mutex>
#include <memory>
#include <atomic>
#include <iostream>
#include <ctime>
#include "timeouttable.h"
namespace ptpl {
	using namespace std;
	//线程安全的任务队列
	class TaskQueue {
	private:
		mutex x;//任务队列互斥锁
		queue<function<void(void)>*>taskque;
	public:
		int size() {
			unique_lock<mutex>lck(this->x);
			return (int)taskque.size();
		}
		bool empty(){
			unique_lock<mutex>lck(this->x);
			return taskque.empty();
		}
		bool pop(function<void(void)>*& t) {
			unique_lock<mutex>lck(this->x);
			if (taskque.empty())return false;
			else {
				t = taskque.front();
				taskque.pop();
				return true;
			}
			return false;
		}
		void clear() {
			//unique_lock<mutex>lck(this->x);
			function<void(void)>* f;
			while (pop(f)) {
				delete f;
			}
		}
		//把函数包装成可调用对象，加入到队列中
		template <typename F,typename... PARAM>
		auto push(F&& f,PARAM&&... params) {
			auto pck = make_shared<packaged_task<decltype(f(params...))(void)> >(
				bind(forward<F>(f), forward<PARAM>(params)...)
			);
			function<void(void)>* _f = new function<void(void)>([pck]() {(*pck)(); });
			{
				unique_lock<mutex>lck(x);
				taskque.push(_f);
			}
			return pck->get_future();
		}
	};

	//优先任务队列
	struct PriorityTask {
		int priority = 1;
		function<void(void)>* task;
		PriorityTask(int n, function<void(void)>* f) :priority(n), task(f) {};
	};
	class cmp {
	public:
		bool operator()(PriorityTask a,PriorityTask b){
			return a.priority < b.priority;
		}
	};
	class PriorityTaskQueue {
	private:
		mutex x;
		priority_queue<PriorityTask, vector<PriorityTask>, cmp> taskque;
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
				t = taskque.top().task;
				taskque.pop();
				return true;
			}
			return false;
		}
		void clear() {
			//unique_lock<mutex>lck(this->x);
			function<void(void)>* f;
			while (pop(f)) {
				delete f;
			}
		}
		template <typename F, typename... PARAM>
		auto push(int priority,F&& f, PARAM&&... params) {
			auto pck = make_shared<packaged_task<decltype(f(params...))(void)> >(
				bind(forward<F>(f), forward<PARAM>(params)...)
				);
			function<void(void)>* _f = new function<void(void)>([pck]() {(*pck)(); });
			{
				unique_lock<mutex>lck(x);
				taskque.push(PriorityTask(priority,_f));
			}
			return pck->get_future();
		}
	};

	//线程池
	
	class FCFS_ThreadPool {
	protected:
		//禁用拷贝构造函数
		FCFS_ThreadPool& operator=(const FCFS_ThreadPool&);
		FCFS_ThreadPool& operator=(const FCFS_ThreadPool&&);

		//禁用赋值构造函数
		FCFS_ThreadPool(const FCFS_ThreadPool&);
		FCFS_ThreadPool(const FCFS_ThreadPool&&);
		
		//用来同步对任务队列访问的条件变量与互斥锁
		mutex mx;
		condition_variable cv;
		//线程池
		vector<unique_ptr<thread>>threadpool;
		//线程停止信号
		vector<shared_ptr<atomic<bool>>> stopsign;
		//任务队列
		TaskQueue tasks;
		//空闲线程数量
		atomic<int> idleThreadNum;
		//线程池状态
		atomic<bool>isDone;
		atomic<bool>isStop;
		//启动i号线程
		void startThread(int i) {
			shared_ptr<atomic<bool>>stop_sign_copy(this->stopsign[i]);
			//创建线程任务
			auto threadtask = [this, i, stop_sign_copy]() {
				atomic<bool>& _sign = *stop_sign_copy;
				function<void(void)>* _func;
				bool tasknoempty = this->tasks.pop(_func);
				while (true) {
					//取任务成功，执行任务
					while (tasknoempty) {
						(*_func)();
						//检查停止信号
						if (_sign)return;
						else tasknoempty = this->tasks.pop(_func);
					}
					//空闲线程+1
					unique_lock<mutex>lck(this->mx);
					++this->idleThreadNum;
					this->cv.wait(lck, [this,&_func,&_sign,&tasknoempty]() {
						tasknoempty = this->tasks.pop(_func);
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
			this->threadpool[i].reset( new thread(threadtask));
		}
		//线程池标志量初始化
		void init() {
			isStop = false;
			isDone = false;
			idleThreadNum = 0;
		}
	public:
		
		//构造函数
		FCFS_ThreadPool() {
			init();
		}
		FCFS_ThreadPool(int n) {
			resize(n);
			init();
		}
		//析构函数
		~FCFS_ThreadPool() {
			
			
			stop(true);
			
		}
		//设置线程池大小并且启动线程
		void resize(int n){
			//运行状态下才能生效
			if (!isStop&&!isDone) {
				int oldsize = threadpool.size();
				if (n >= oldsize) {
					threadpool.resize(n);
					stopsign.resize(n);
					for (int i = oldsize; i < n; i++) {
						
						stopsign[i] = make_shared<atomic<bool>>(false);
						startThread(i);
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
		int GetTaskQueueSize() {
			return tasks.size();
		}
		//清空任务队列
		void clear_allTask() {
			tasks.clear();
		}
		//停止线程池
		void stop(bool iswait=false) {
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
				if(isStop||isDone)return;
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
		template <typename F,typename... PARAM>
		auto push(F&& f,PARAM&&... params) {
			auto pck=tasks.push(forward<F>(f), forward<PARAM>(params)...);
			//激活一个等待中的线程
			unique_lock<mutex>lck(mx);
			cv.notify_one();
			return pck;
		}
	};

//基于优先队列线程池
	
	class PriorityThreadPool {
	private:
		//禁用拷贝构造函数
		PriorityThreadPool& operator=(const PriorityThreadPool&);
		PriorityThreadPool& operator=(const PriorityThreadPool&&);

		//禁用赋值构造函数
		PriorityThreadPool(const PriorityThreadPool&);
		PriorityThreadPool(const PriorityThreadPool&&);

		//用来同步对任务队列访问的条件变量与互斥锁
		mutex mx;
		condition_variable cv;
		//线程池
		vector<unique_ptr<thread>>threadpool;
		//线程停止信号
		vector<shared_ptr<atomic<bool>>> stopsign;
		//任务队列
		PriorityTaskQueue tasks;
		//空闲线程数量
		atomic<int> idleThreadNum;
		//线程池状态
		atomic<bool>isDone;
		atomic<bool>isStop;
		//启动i号线程
		void startThread(int i) {
			shared_ptr<atomic<bool>>stop_sign_copy(this->stopsign[i]);
			//创建线程任务
			auto threadtask = [this, i, stop_sign_copy]() {
				atomic<bool>& _sign = *stop_sign_copy;
				function<void(void)>* _func;
				bool tasknoempty = this->tasks.pop(_func);
				while (true) {
					//取任务成功，执行任务
					while (tasknoempty) {
						(*_func)();
						//检查停止信号
						if (_sign)return;
						else tasknoempty = this->tasks.pop(_func);
					}
					//空闲线程+1
					unique_lock<mutex>lck(this->mx);
					++this->idleThreadNum;
					this->cv.wait(lck, [this, &_func, &_sign, &tasknoempty]() {
						tasknoempty = this->tasks.pop(_func);
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
	public:

		//构造函数
		PriorityThreadPool() {
			init();
		}
		PriorityThreadPool(int n) {
			resize(n);
			init();
		}
		//析构函数
		~PriorityThreadPool() {


			stop(true);

		}
		//设置线程池大小并且启动线程
		void resize(int n) {
			//运行状态下才能生效
			if (!isStop && !isDone) {
				int oldsize = threadpool.size();
				if (n >= oldsize) {
					threadpool.resize(n);
					stopsign.resize(n);
					for (int i = oldsize; i < n; i++) {

						stopsign[i] = make_shared<atomic<bool>>(false);
						startThread(i);
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
		int GetTaskQueueSize() {
			return tasks.size();
		}
		//清空任务队列
		void clear_allTask() {
			tasks.clear();
		}
		//停止线程池
		void stop(bool iswait = false) {
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
		template <typename F, typename... PARAM>
		auto push(int priority,F&& f, PARAM&&... params) {
			auto pck = tasks.push(priority,forward<F>(f), forward<PARAM>(params)...);
			//激活一个等待中的线程
			unique_lock<mutex>lck(mx);
			cv.notify_one();
			return pck;
		}
	};
	//支持自适应线程大小的线程池
	class AutoSuitPool :public FCFS_ThreadPool {
	private:
		queue<int>outThread;//已经退出的线程id
		mutex outlock;
		tot::TimeOutTable totable;//等待中的线程表

		thread* Threadmaster;
		//最大线程数量
		int maxthreadnum = 100;
		//超时时间
		int maxwaittime = 5000;

		//线程释放机制
		
		//启动线程
		void startThread(int i) {
			shared_ptr<atomic<bool>>stop_sign_copy(this->stopsign[i]);
			//创建线程任务
			auto threadtask = [this, i, stop_sign_copy]() {
				atomic<bool>& _sign = *stop_sign_copy;
				function<void(void)>* _func;
				bool tasknoempty = this->tasks.pop(_func);
				while (true) {
					//取任务成功，执行任务
					while (tasknoempty) {
						(*_func)();
						//检查停止信号
						if (_sign)return;
						else tasknoempty = this->tasks.pop(_func);
					}
					//将线程加入到等待表
					totable.push(i);
					//空闲线程+1
					unique_lock<mutex>lck(this->mx);
					++this->idleThreadNum;
					this->cv.wait(lck, [this, &_func, &_sign, &tasknoempty]() {
						tasknoempty = this->tasks.pop(_func);
						return tasknoempty || this->isDone || _sign;
						});
					totable.remove(i);
					--this->idleThreadNum;
					//已无任务执行，结束线程任务
					if (!tasknoempty) {
						return;
					}
				}
			};
			//线程任务交付线程执行
			//释放之前的线程
			this->threadpool[i]->detach();
			this->threadpool[i].reset(new thread(threadtask));
		}
		//超时监控线程
		void startThreadMaster() {
			this->Threadmaster = new thread([this]() {
				long long t;
				while (true) {
					Sleep(this->maxwaittime);
					time(&t);
					int i;
					this->totable.lockque();
					if (!this->totable.empty()) {
						
						while (t - this->totable.GetTopTime() > this->maxwaittime) {
							i = this->totable.pop();
							if (i < 0)break;
							//终止线程
							//(*(this->threadpool[i])).~thread();
							*(this->stopsign[i]) = true;
							{
								unique_lock<mutex>lck(this->mx);
								this->cv.notify_all();
							}
							//加入到退出线程队列
							{
								unique_lock<mutex> lck(this->outlock);
								this->outThread.push(i);
							}						
							time(&t);
						}
						
					}
					this->totable.unlockque();
				}
			});
		}
	public:
		//启动监控线程
		AutoSuitPool():FCFS_ThreadPool(5) {
			startThreadMaster();
		}
		AutoSuitPool(int maxThreadNum,int FirstThreadNum,int overTime) :FCFS_ThreadPool(FirstThreadNum) {
			this->maxthreadnum = maxThreadNum;
			this->maxwaittime = overTime;
			startThreadMaster();
		}
		//线程重启机制
		template <typename F, typename... PARAM>
		auto push(F&& f, PARAM&&... params) {
			auto pck = tasks.push(forward<F>(f), forward<PARAM>(params)...);
			{
				if (GetidleThreadNumber() == 0) {
					unique_lock<mutex> lck(outlock);
					if (outThread.empty()) {
						int ms = threadpool.size();
						ms = max(ms + 1, (int)(1.2 * ms));
						ms = min(ms, this->maxthreadnum);
						resize(ms);
					}
					else {
						startThread(outThread.front());
						outThread.pop();
					}
				}
				else {
					//激活一个等待中的线程
					unique_lock<mutex>lck(mx);
					cv.notify_one();
					
				}
			}
			return pck;
		}
	};
}





