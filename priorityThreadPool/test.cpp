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