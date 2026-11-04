#include <iostream>
#include <thread>
#include <mutex>
#include <queue>

std::queue<int> buffer;
std::mutex mtx;

void producer(){
    for (int i=0;i<5;i++)
    {
        std::lock_guard<std::mutex> lock(mtx);
        buffer.push(i);
        std::cout<<"Produced: "<<i<<std::endl;
    }
}

void consumer(){
    for (int i=0;i<5;i++){
        std::lock_guard<std::mutex> lock(mtx);
        
        if(!buffer.empty()){
            int value = buffer.front();
            buffer.pop();
            std::cout<<"Consumed: "<<value<<std::endl;
        }
    }
}

int main(){
    std::thread t1(producer);
    std::thread t2(consumer);

    t1.join();
    t2.join();
}