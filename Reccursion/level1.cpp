// Print numbers from 1 to N using recursion.
#include <iostream>

void printNumbers (int n) {
    if (n==0){
        return;
    }
    printNumbers(n-1);
    std::cout << n << std::endl;
}

void printNumbersReverse (int n) {
    if (n==0){
        return;
    }
    std::cout << n << std::endl;
    printNumbers(n-1);
}


int main() {
    int n;
    std::cout<<"Enter a number: ";
    std::cin>>n;
    printNumbers(n);
    printNumbersReverse(n);
    return 0;
}