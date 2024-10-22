//ThreadPool.h
#ifndef _THREADPOOL_H_
#define _THREADPOOL_H_

// 包含必要的头文件
#include <future>        // 用于std::future
#include <functional>    // 用于std::function
#include <iostream>      // 用于标准输入输出
#include <queue>         // 用于std::queue
#include <mutex>         // 用于std::mutex
#include <memory>        // 用于std::shared_ptr
#include <sys/time.h>    // 用于时间相关操作

using namespace std;

/////////////////////////////////////////////////
/**
 * @file thread_pool.h
 * @brief 线程池类,采用c++11来实现
 * 
 * 使用说明:
 * ThreadPool tpool;
 * tpool.init(5);   // 初始化线程池，创建5个线程
 * tpool.start();   // 启动线程池
 * tpool.exec(testFunction, 10);    // 将任务提交到线程池中执行
 * tpool.waitForAllDone(1000);      // 等待所有任务完成，超时时间为1000ms
 * tpool.stop();    // 停止线程池
 * 
 * 注意:
 * ThreadPool::exec执行任务返回的是个future对象, 可以通过future异步获取结果
 * 
 * 示例:
 * int testInt(int i)
 * {
 *     return i;
 * }
 * auto f = tpool.exec(testInt, 5);
 * cout << f.get() << endl;   // 当testInt在线程池中执行后, f.get()会返回数值5
 *
 * 对于类成员函数，可以使用std::bind:
 * class Test
 * {
 * public:
 *     int test(int i);
 * };
 * Test t;
 * auto f = tpool.exec(std::bind(&Test::test, &t, std::placeholders::_1), 10);
 * cout << f.get() << endl;
 */
namespace longkit {

// 获取当前时间的函数声明
void getNow(timeval *tv);
int64_t getNowMs();

// 定义宏，用于获取当前时间
#define TNOW      getNow()
#define TNOWMS    getNowMs()

class ThreadPool
{
protected:
    // 定义任务结构体
    struct TaskFunc
    {
        TaskFunc(uint64_t expireTime) : _expireTime(expireTime)
        { }

        std::function<void()>   _func;      // 存储任务函数
        int64_t                _expireTime = 0;	// 任务的过期时间（绝对时间）
    };
    typedef shared_ptr<TaskFunc> TaskFuncPtr;  // 使用智能指针管理TaskFunc

public:
    /**
    * @brief 构造函数
    */
    ThreadPool()
        :  threadNum_(1), bTerminate_(false) {
        }

    /**
    * @brief 析构函数, 会停止所有线程
    */
    virtual ~ThreadPool() {
         stop();
    }

    /**
    * @brief 初始化线程池
    *
    * @param num 工作线程个数
    * @return 初始化是否成功
    */
    bool init(size_t num) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (!threads_.empty())
        {
            return false;  // 如果线程池已经初始化，返回false
        }

        threadNum_ = num;  // 设置线程数量
        return true;
    }

    /**
    * @brief 获取线程个数
    *
    * @return size_t 线程个数
    */
    size_t getThreadNum()
    {
        std::unique_lock<std::mutex> lock(mutex_);
        return threads_.size();
    }

    /**
    * @brief 获取当前线程池的任务数
    *
    * @return size_t 线程池的任务数
    */
    size_t getJobNum()
    {
        std::unique_lock<std::mutex> lock(mutex_);
        return tasks_.size();
    }

    /**
    * @brief 停止所有线程, 会等待所有线程结束
    */
    void stop() {
        {
            std::unique_lock<std::mutex> lock(mutex_);  // 加锁
            bTerminate_ = true;     // 触发退出标志
            condition_.notify_all();  // 通知所有等待的线程
        }

        for (size_t i = 0; i < threads_.size(); i++)
        {
            if(threads_[i]->joinable())
            {
                threads_[i]->join();  // 等待线程结束
            }
            delete threads_[i];
            threads_[i] = NULL;
        }

        std::unique_lock<std::mutex> lock(mutex_);
        threads_.clear();  // 清空线程容器
    }

    /**
    * @brief 启动所有线程
    * @return 启动是否成功
    */
    bool start()  {
        std::unique_lock<std::mutex> lock(mutex_);

        if (!threads_.empty())
        {
            return false;  // 如果线程已经启动，返回false
        }

        for (size_t i = 0; i < threadNum_; i++)
        {
            threads_.push_back(new thread(&ThreadPool::run, this));  // 创建并启动线程
        }
        return true;
    }

    /**
    * @brief 用线程池启用任务(F是function, Args是参数)
    *
    * @param f 任务函数
    * @param args 任务函数的参数
    * @return 返回任务的future对象, 可以通过这个对象来获取返回值
    */
    template <class F, class... Args>
    auto exec(F&& f, Args&&... args) -> std::future<decltype(f(args...))>
    {
        return exec(0,f,args...);  // 调用带超时参数的exec函数，超时时间为0表示不设置超时
    }

    /**
    * @brief 用线程池启用任务(F是function, Args是参数)
    *
    * @param timeoutMs 超时时间，单位ms (为0时不做超时控制)；若任务超时，此任务将被丢弃
    * @param f 任务函数
    * @param args 任务函数的参数
    * @return 返回任务的future对象, 可以通过这个对象来获取返回值
    */
    template <class F, class... Args>
    auto exec(int64_t timeoutMs, F&& f, Args&&... args) -> std::future<decltype(f(args...))>
    {
        int64_t expireTime =  (timeoutMs == 0 ? 0 : TNOWMS + timeoutMs);  // 计算任务的过期时间
        using RetType = decltype(f(args...));  // 推导返回值类型
        
        // 创建packaged_task，将函数和参数绑定
        auto task = std::make_shared<std::packaged_task<RetType()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );

        

        // 创建任务指针，设置过期时间
        TaskFuncPtr fPtr = std::make_shared<TaskFunc>(expireTime);
        fPtr->_func = [task]() {  // 设置任务函数
            (*task)();
        };

        std::unique_lock<std::mutex> lock(mutex_);
        tasks_.push(fPtr);              // 将任务插入队列
        condition_.notify_one();        // 唤醒一个等待的线程

        return task->get_future();  // 返回future对象
    }

    /**
    * @brief 等待当前任务队列中所有工作全部结束(队列无任务)
    *
    * @param millsecond 等待的时间(ms), -1表示永远等待
    * @return true: 所有工作都处理完毕, false: 超时退出
    */
    bool waitForAllDone(int millsecond = -1) {
        std::unique_lock<std::mutex> lock(mutex_);

        if (tasks_.empty())
            return true;

        if (millsecond < 0)
        {
            condition_.wait(lock, [this] { return tasks_.empty(); });
            return true;
        }
        else
        {
            return condition_.wait_for(lock, std::chrono::milliseconds(millsecond), [this] { return tasks_.empty(); });
        }
    }

protected:
    /**
    * @brief 获取任务
    *
    * @param task 用于存储获取到的任务
    * @return 是否成功获取任务
    */
    bool get(TaskFuncPtr&task) {
        std::unique_lock<std::mutex> lock(mutex_);

        if (tasks_.empty())
        {
            condition_.wait(lock, [this] { 
                return bTerminate_ || !tasks_.empty();  
            });
        }

        if (bTerminate_)
            return false;

        if (!tasks_.empty())
        {
            task = std::move(tasks_.front());  // 使用移动语义获取任务
            tasks_.pop();  // 移除已获取的任务
            return true;
        }

        return false;
    }

    /**
    * @brief 检查线程池是否需要退出
    */
    bool isTerminate() { return bTerminate_; }

    /**
    * @brief 线程运行函数
    */
    void run() {
        while (!isTerminate())
        {
            TaskFuncPtr task;
            bool ok = get(task);        // 获取任务
            if (ok)
            {
                ++atomic_;  // 增加正在执行的任务计数
                try
                {
                    if (task->_expireTime != 0 && task->_expireTime < TNOWMS)
                    {
                        // 任务已超时，可以在这里添加处理逻辑
                    }
                    else
                    {
                        task->_func();  // 执行任务
                    }
                }
                catch (...)
                {
                    // 捕获所有异常，防止线程意外退出
                }

                --atomic_;  // 减少正在执行的任务计数

                // 检查是否所有任务都执行完毕
                std::unique_lock<std::mutex> lock(mutex_);
                if (atomic_ == 0 && tasks_.empty())
                {
                    condition_.notify_all();  // 通知等待的线程（如waitForAllDone）
                }
            }
        }
    }

    void onlytest();  // 测试用函数，实现未给出

protected:
    queue<TaskFuncPtr> tasks_;  // 任务队列

    std::vector<std::thread*> threads_;  // 工作线程容器

    std::mutex                mutex_;  // 互斥锁，用于保护共享数据

    std::condition_variable   condition_;  // 条件变量，用于线程同步

    size_t                    threadNum_;  // 线程数量

    bool                      bTerminate_;  // 是否终止线程池的标志

    std::atomic<int>          atomic_{ 0 };  // 原子计数器，记录正在执行的任务数
};

}// namespace longkit

#endif // THREADPOOL_H_
