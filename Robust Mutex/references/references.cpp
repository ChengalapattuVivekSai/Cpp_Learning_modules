#include <iostream>

class Device {
    private:
        int deviceid;
        int status;

    public:
        Device(int id) :deviceid(id),status(0) {}

    int getDeviceId() const {
        return deviceid;
    }

    int getStatus() const {
        return status;
    }

    void setStatus(int s) {
        status = s;
    }

    void updateDevice(Device& device){
        device.setStatus(1);
    }

   
};

int main() {
    Device d1(101);

     std::cout << "Device ID: " << d1.getDeviceId() << '\n';
    std::cout << "Status: " << d1.getStatus() << '\n';

        return 0;

}