#include <iostream>
#include <thread>
#include <mutex>


class Counter{

    public :
      int value=0;
      std::mutex mtx;

      void increment(){
        mtx.lock();
        value++;
        mtx.unlock();
      }
};


int main(){
    
    Counter counter;

    std::thread t1([&counter](){
        for(int i=0;i<100000;i++){
            counter.increment();
        }
    });

    std::thread t2([&counter](){
        for(int i=0;i<100000;i++){
            counter.increment();
        }
    });

    t1.join();
    t2.join();

    std::cout<<"Final value: "<<counter.value<<std::endl;

    return 0;
}