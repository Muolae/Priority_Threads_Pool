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
	//�̰߳�ȫ���������
	class TaskQueue {
	private:
		mutex x;//������л�����
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
		//�Ѻ�����װ�ɿɵ��ö��󣬼��뵽������
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

	//�����������
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

	//�̳߳�
	
	class FCFS_ThreadPool {
	protected:
		//���ÿ������캯��
		FCFS_ThreadPool& operator=(const FCFS_ThreadPool&);
		FCFS_ThreadPool& operator=(const FCFS_ThreadPool&&);

		//���ø�ֵ���캯��
		FCFS_ThreadPool(const FCFS_ThreadPool&);
		FCFS_ThreadPool(const FCFS_ThreadPool&&);
		
		//����ͬ����������з��ʵ����������뻥����
		mutex mx;
		condition_variable cv;
		//�̳߳�
		vector<unique_ptr<thread>>threadpool;
		//�߳�ֹͣ�ź�
		vector<shared_ptr<atomic<bool>>> stopsign;
		//�������
		TaskQueue tasks;
		//�����߳�����
		atomic<int> idleThreadNum;
		//�̳߳�״̬
		atomic<bool>isDone;
		atomic<bool>isStop;
		//����i���߳�
		void startThread(int i) {
			shared_ptr<atomic<bool>>stop_sign_copy(this->stopsign[i]);
			//�����߳�����
			auto threadtask = [this, i, stop_sign_copy]() {
				atomic<bool>& _sign = *stop_sign_copy;
				function<void(void)>* _func;
				bool tasknoempty = this->tasks.pop(_func);
				while (true) {
					//ȡ����ɹ���ִ������
					while (tasknoempty) {
						(*_func)();
						//���ֹͣ�ź�
						if (_sign)return;
						else tasknoempty = this->tasks.pop(_func);
					}
					//�����߳�+1
					unique_lock<mutex>lck(this->mx);
					++this->idleThreadNum;
					this->cv.wait(lck, [this,&_func,&_sign,&tasknoempty]() {
						tasknoempty = this->tasks.pop(_func);
						return tasknoempty || this->isDone || _sign;
						});
					--this->idleThreadNum;
					//��������ִ�У������߳�����
					if (!tasknoempty) {
						return;
					}
				}	
			};
			//�߳����񽻸��߳�ִ��
			this->threadpool[i].reset( new thread(threadtask));
		}
		//�̳߳ر�־����ʼ��
		void init() {
			isStop = false;
			isDone = false;
			idleThreadNum = 0;
		}
	public:
		
		//���캯��
		FCFS_ThreadPool() {
			init();
		}
		FCFS_ThreadPool(int n) {
			resize(n);
			init();
		}
		//��������
		~FCFS_ThreadPool() {
			
			
			stop(true);
			
		}
		//�����̳߳ش�С���������߳�
		void resize(int n){
			//����״̬�²�����Ч
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
						//���������̣߳�ʹ�����ͷŵ��߳̽�����
						unique_lock<mutex>lck(mx);
						cv.notify_all();
					}
					//��ȫ��ɾ��
					threadpool.resize(n);
					stopsign.resize(n);
				}
			}
		}
		//��ȡ�����߳�����
		int GetidleThreadNumber() {
			return idleThreadNum;
		}
		//��ȡ�̳߳�����
		int pool_size() {
			return threadpool.size();
		}
		//��ȡʣ����������
		int GetTaskQueueSize() {
			return tasks.size();
		}
		//����������
		void clear_allTask() {
			tasks.clear();
		}
		//ֹͣ�̳߳�
		void stop(bool iswait=false) {
			if (!iswait) {//ǿ��ֹͣ������������������е�ʣ������
				//�Ѿ�ֹͣ�����
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
			//����һ���ȴ��е��߳�
			unique_lock<mutex>lck(mx);
			cv.notify_one();
			return pck;
		}
	};

//�������ȶ����̳߳�
	
	class PriorityThreadPool {
	private:
		//���ÿ������캯��
		PriorityThreadPool& operator=(const PriorityThreadPool&);
		PriorityThreadPool& operator=(const PriorityThreadPool&&);

		//���ø�ֵ���캯��
		PriorityThreadPool(const PriorityThreadPool&);
		PriorityThreadPool(const PriorityThreadPool&&);

		//����ͬ����������з��ʵ����������뻥����
		mutex mx;
		condition_variable cv;
		//�̳߳�
		vector<unique_ptr<thread>>threadpool;
		//�߳�ֹͣ�ź�
		vector<shared_ptr<atomic<bool>>> stopsign;
		//�������
		PriorityTaskQueue tasks;
		//�����߳�����
		atomic<int> idleThreadNum;
		//�̳߳�״̬
		atomic<bool>isDone;
		atomic<bool>isStop;
		//����i���߳�
		void startThread(int i) {
			shared_ptr<atomic<bool>>stop_sign_copy(this->stopsign[i]);
			//�����߳�����
			auto threadtask = [this, i, stop_sign_copy]() {
				atomic<bool>& _sign = *stop_sign_copy;
				function<void(void)>* _func;
				bool tasknoempty = this->tasks.pop(_func);
				while (true) {
					//ȡ����ɹ���ִ������
					while (tasknoempty) {
						(*_func)();
						//���ֹͣ�ź�
						if (_sign)return;
						else tasknoempty = this->tasks.pop(_func);
					}
					//�����߳�+1
					unique_lock<mutex>lck(this->mx);
					++this->idleThreadNum;
					this->cv.wait(lck, [this, &_func, &_sign, &tasknoempty]() {
						tasknoempty = this->tasks.pop(_func);
						return tasknoempty || this->isDone || _sign;
						});
					--this->idleThreadNum;
					//��������ִ�У������߳�����
					if (!tasknoempty) {
						return;
					}
				}
			};
			//�߳����񽻸��߳�ִ��
			this->threadpool[i].reset(new thread(threadtask));
		}
		//�̳߳ر�־����ʼ��
		void init() {
			isStop = false;
			isDone = false;
			idleThreadNum = 0;
		}
	public:

		//���캯��
		PriorityThreadPool() {
			init();
		}
		PriorityThreadPool(int n) {
			resize(n);
			init();
		}
		//��������
		~PriorityThreadPool() {


			stop(true);

		}
		//�����̳߳ش�С���������߳�
		void resize(int n) {
			//����״̬�²�����Ч
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
						//���������̣߳�ʹ�����ͷŵ��߳̽�����
						unique_lock<mutex>lck(mx);
						cv.notify_all();
					}
					//��ȫ��ɾ��
					threadpool.resize(n);
					stopsign.resize(n);
				}
			}
		}
		//��ȡ�����߳�����
		int GetidleThreadNumber() {
			return idleThreadNum;
		}
		//��ȡ�̳߳�����
		int pool_size() {
			return threadpool.size();
		}
		//��ȡʣ����������
		int GetTaskQueueSize() {
			return tasks.size();
		}
		//����������
		void clear_allTask() {
			tasks.clear();
		}
		//ֹͣ�̳߳�
		void stop(bool iswait = false) {
			if (!iswait) {//ǿ��ֹͣ������������������е�ʣ������
				//�Ѿ�ֹͣ�����
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
			//����һ���ȴ��е��߳�
			unique_lock<mutex>lck(mx);
			cv.notify_one();
			return pck;
		}
	};
	//֧������Ӧ�̴߳�С���̳߳�
	class AutoSuitPool :public FCFS_ThreadPool {
	private:
		queue<int>outThread;//�Ѿ��˳����߳�id
		mutex outlock;
		tot::TimeOutTable totable;//�ȴ��е��̱߳�

		thread* Threadmaster;
		//����߳�����
		int maxthreadnum = 100;
		//��ʱʱ��
		int maxwaittime = 5000;

		//�߳��ͷŻ���
		
		//�����߳�
		void startThread(int i) {
			shared_ptr<atomic<bool>>stop_sign_copy(this->stopsign[i]);
			//�����߳�����
			auto threadtask = [this, i, stop_sign_copy]() {
				atomic<bool>& _sign = *stop_sign_copy;
				function<void(void)>* _func;
				bool tasknoempty = this->tasks.pop(_func);
				while (true) {
					//ȡ����ɹ���ִ������
					while (tasknoempty) {
						(*_func)();
						//���ֹͣ�ź�
						if (_sign)return;
						else tasknoempty = this->tasks.pop(_func);
					}
					//���̼߳��뵽�ȴ���
					totable.push(i);
					//�����߳�+1
					unique_lock<mutex>lck(this->mx);
					++this->idleThreadNum;
					this->cv.wait(lck, [this, &_func, &_sign, &tasknoempty]() {
						tasknoempty = this->tasks.pop(_func);
						return tasknoempty || this->isDone || _sign;
						});
					totable.remove(i);
					--this->idleThreadNum;
					//��������ִ�У������߳�����
					if (!tasknoempty) {
						return;
					}
				}
			};
			//�߳����񽻸��߳�ִ��
			//�ͷ�֮ǰ���߳�
			this->threadpool[i]->detach();
			this->threadpool[i].reset(new thread(threadtask));
		}
		//��ʱ����߳�
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
							//��ֹ�߳�
							//(*(this->threadpool[i])).~thread();
							*(this->stopsign[i]) = true;
							{
								unique_lock<mutex>lck(this->mx);
								this->cv.notify_all();
							}
							//���뵽�˳��̶߳���
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
		//��������߳�
		AutoSuitPool():FCFS_ThreadPool(5) {
			startThreadMaster();
		}
		AutoSuitPool(int maxThreadNum,int FirstThreadNum,int overTime) :FCFS_ThreadPool(FirstThreadNum) {
			this->maxthreadnum = maxThreadNum;
			this->maxwaittime = overTime;
			startThreadMaster();
		}
		//�߳���������
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
					//����һ���ȴ��е��߳�
					unique_lock<mutex>lck(mx);
					cv.notify_one();
					
				}
			}
			return pck;
		}
	};
}





