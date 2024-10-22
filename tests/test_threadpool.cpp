#include <iostream>
#include "thread/thread_pool.h"
using namespace std;
using namespace longkit;

// 无参数的测试函数
void func0()
{
    cout << "func0()" << endl;
}

// 带一个整型参数的测试函数
void func1(int a)
{
    cout << "func1 int =" << a << endl; 
}

// 注释掉的字符串参数版本的func1
//void func1(string a)
//{
//    cout << "func1 string =" << a << endl;
//}

// 带两个参数（整型和字符串）的测试函数
void func2(int a, string b)
{
    cout << "func2() a=" << a << ", b=" << b<< endl;
}

// 简单测试线程池的函数
void test1()
{
    ThreadPool threadpool;     // 创建一个线程池对象
    threadpool.init(1);        // 初始化线程池，设置线程数量为1
    threadpool.start();        // 启动线程池，创建线程并开始运行

    // 向线程池提交任务
    //threadpool.exec(1000,func0); // 注释掉的代码，1000是超时时间（毫秒），目前未实现
    threadpool.exec(func1, 10);
    //threadpool.exec((void(*)(int))func1, 10);   // 显式类型转换的方式提交任务
    //threadpool.exec((void(*)(string))func1, "king");
    threadpool.exec(func2, 20, "darren");

    threadpool.waitForAllDone(); // 等待所有任务执行完成
    threadpool.stop();           // 停止线程池
}

// 带返回值的测试函数（整型参数和返回值）
int func1_future(int a)
{
    cout << "func1() a=" << a << endl;
    return a;
}

// 带返回值的测试函数（整型和字符串参数，返回字符串）
string func2_future(int a, string b)
{
    cout << "func1() a=" << a << ", b=" << b<< endl;
    return b;
}

// 测试任务函数返回值的函数
void test2()
{
    ThreadPool threadpool;
    threadpool.init(1);
    threadpool.start();

    // 提交带返回值的任务
    std::future<decltype (func1_future(0))> result1 = threadpool.exec(func1_future, 10);
    std::future<string> result2 = threadpool.exec(func2_future, 20, "darren");
    //auto result2 = threadpool.exec(func2_future, 20, "darren");

    // 获取并打印任务的返回值
    std::cout << "result1: " << result1.get() << std::endl;
    std::cout << "result2: " << result2.get() << std::endl;

    threadpool.waitForAllDone();
    threadpool.stop();
}




// 用于测试成员函数绑定的类
class Test
{
public:
    int test(int i){
        cout << _name << ", i = " << i << endl;
        return i;
    }
    //int test(string str) {
    //    cout << _name << ", str = " << str << endl;
    //}
    void setName(string name){
        _name = name;
    }
    string _name;
};

// 测试类对象成员函数绑定的函数
void test3()
{
    ThreadPool threadpool;
    threadpool.init(1);
    threadpool.start();

    Test t1;
    Test t2;
    t1.setName("Test1");
    t2.setName("Test2");

    // 使用std::bind绑定成员函数并提交任务
    auto f1 = threadpool.exec(std::bind(&Test::test, &t1, std::placeholders::_1), 10);
    auto f2 = threadpool.exec(std::bind(&Test::test, &t2, std::placeholders::_1), 20);

    threadpool.waitForAllDone();

    // 获取并打印任务的返回值
    cout << "t1 " << f1.get() << endl;
    cout << "t2 " << f2.get() << endl;
}

// 重载函数测试 - 整型版本
void func2_1(int a, int b)
{
    cout << "func2_1 a + b = " << a+b << endl;
}

// 重载函数测试 - 字符串版本
int func2_1(string a, string b)
{
    cout << "func2_1 a + b = " << a << b<< endl;
    return 0;
}

// 测试重载函数的提交
void test4()
{
    ThreadPool threadpool;
    threadpool.init(1);
    threadpool.start();

    // 提交重载函数任务，需要显式类型转换
    threadpool.exec((void(*)(int, int))func2_1, 10, 20);
    threadpool.exec((int(*)(string, string))func2_1, "king", " and darren");

    threadpool.waitForAllDone();
    threadpool.stop();
}

int main()
{
   test1(); // 简单测试线程池
   test2(); // 测试任务函数返回值
   test3(); // 测试类对象函数的绑定
   test4(); // 测试重载函数的提交
   cout << "main finish!" << endl;
   return 0;
}
