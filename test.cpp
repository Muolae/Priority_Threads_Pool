#include <iostream>
#include <functional>
#include <Windows.h>
#include "priorityThreadPool.h"
#include "timeouttable.h"
using namespace std;
using namespace ptpl;
int test1() {
	Sleep(2000);
	cout << "helloworld" << endl;
	return 7;
}
void test2(int a) {
	Sleep(2000);
	cout << "prioritytask" <<a<< endl;
}
int main() {
	AutoSuitPool p(5,2,1);
	cout << "运行中的线程数量：" << p.GetRunningThread() << endl;
	for(int i=0;i<10;i++)
	p.push(test1);
	Sleep(50);
	cout << "运行中的线程数量：" << p.GetRunningThread() << endl;
	Sleep(15000);
	cout << "运行中的线程数量：" << p.GetRunningThread() << endl;
	Sleep(50);
	p.push(test1);
	p.push(test1);
	Sleep(50);
	cout << "运行中的线程数量：" << p.GetRunningThread() << endl;
}