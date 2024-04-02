
#ifndef _TQUEUE_H_
#define _TQUEUE_H_
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>

/*
    Thread safe Queue.
*/
template <typename T>
class ThreadSafeQueue {
    std::mutex mutex;
    std::condition_variable cond_var;
    std::queue<T> queue;
public:
    void push(const T& item){{
            std::lock_guard lock(mutex);
            queue.push(item);
        }
        cond_var.notify_one();
    }

    bool empty(){
        std::unique_lock lock(mutex);
        return queue.empty();
    }

    T& front(){
        std::lock_guard lock(mutex);

        return queue.front();
    }

    bool pop(T& t){
        std::lock_guard lock(mutex);
        if (!queue.empty()){
            t = queue.front();
            queue.pop();
            return true;
        }
        return false;
    }

    long size(){
        std::lock_guard lock(mutex);
        return queue.size();
    }
};

#endif