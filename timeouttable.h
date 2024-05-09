#pragma once
#include <ctime>
#include <list>
#include <unordered_map>
#include <mutex>
using namespace std;
namespace tot {
	//线程安全的等待线程队列
	struct outThread {
		int id;
		long long outtime;
		outThread(int id, long long ut) :id(id), outtime(ut) {};
	};
	class TimeOutTable {
	private:
		std::list<outThread>timeoutque;
		std::unordered_map<int, std::list<outThread>::iterator>addr;
		std::mutex mx;
	public:
		bool empty() {
			return timeoutque.empty();
		}
		void push(int id) {
			long long t;
			time(&t);
			unique_lock<mutex>lck(mx);
			timeoutque.push_front(outThread(id, t));
			addr[id] = timeoutque.begin();
		}
		//t赋值为最早的时间戳，返回该等待线程id
		int pop() {
			//unique_lock<mutex>lck(mx);
			if (timeoutque.empty())return -1;
			auto& temp = timeoutque.back();
			timeoutque.pop_back();
			addr.erase(temp.id);
			return temp.id;
		}
		void remove(int id) {
			unique_lock<mutex>lck(mx);
			if (addr.count(id)) {
				timeoutque.erase(addr[id]);
				addr.erase(id);
			}
		}
		long long GetTopTime() {
			//unique_lock<mutex>lck(mx);
			return timeoutque.back().outtime;
		}
		//锁定队列
		void lockque() {
			mx.lock();
		}
		void unlockque() {
			mx.unlock();
		}
	};

}