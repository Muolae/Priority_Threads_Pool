#pragma once
#include <queue>
#include <functional>
#include <future>

#include <mutex>
#include <memory>
#include <atomic>
using namespace std;
//������г�����
template<class C>
class Task {
protected:
	C taskque;

	mutex x;//������л�����

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
	//�Ѻ�����װ�ɿɵ��ö��󣬼��뵽������
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

//�����������
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
//�������ģ����
class Threadpool {
protected:
	//�̻߳�����
	mutex mx;
	condition_variable cv;
	//�̳߳�
	vector<unique_ptr<thread>>threadpool;
	//�߳�ֹͣ�ź�
	vector<shared_ptr<atomic<bool>>> stopsign;
	//�����߳�����
	atomic<int> idleThreadNum;
	//�̳߳�״̬
	atomic<bool>isDone;
	atomic<bool>isStop;
	//����i���߳�
	template <class C>
	void startThread(int i,C& tasks) {
		shared_ptr<atomic<bool>>stop_sign_copy(this->stopsign[i]);
		//�����߳�����
		auto threadtask = [this, i, stop_sign_copy]() {
			atomic<bool>& _sign = *stop_sign_copy;
			function<void(void)>* _func;
			bool tasknoempty = tasks.pop(_func);
			while (true) {
				//ȡ����ɹ���ִ������
				while (tasknoempty) {
					(*_func)();
					//���ֹͣ�ź�
					if (_sign)return;
					else tasknoempty = tasks.pop(_func);
				}
				//�����߳�+1
				unique_lock<mutex>lck(this->mx);
				++this->idleThreadNum;
				this->cv.wait(lck, [this, &_func, &_sign, &tasknoempty]() {
					tasknoempty = tasks.pop(_func);
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
	//�����̳߳ش�С���������߳�
	template<class C>
	void resize(int n,C& tasks) {
		//����״̬�²�����Ч
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
	template<class C>
	int GetTaskQueueSize(C& tasks) {
		return tasks.size();
	}
	//����������
	template<class C>
	void clear_allTask(C&tasks) {
		tasks.clear();
	}
	//ֹͣ�̳߳�
	template<class C>
	void stop(C&tasks,bool iswait = false) {
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
	template <class C,typename F, typename... PARAM>
	auto push(C&tasks,F&& f, PARAM&&... params) {
		auto pck = tasks.push(forward<F>(f), forward<PARAM>(params)...);
		//����һ���ȴ��е��߳�
		unique_lock<mutex>lck(mx);
		cv.notify_one();
		return pck;
	}
};
