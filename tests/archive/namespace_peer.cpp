#include "shared_preferences.h"
#include <cstdlib>
#include <iostream>
#include <string>
int main(int argc,char** argv) {
    if(argc!=2 || !std::getenv("CORAL_TEST_NAMESPACE"))return 2;
    SharedMemoryConfig config;
    if(!config.initialize() || !config.getConfig("step3_scope_marker").empty())return 1;
    const std::string value=argv[1];
    if(!config.setConfig("step3_scope_marker",value))return 1;
    std::cout<<"READY"<<std::endl;
    std::string signal;std::getline(std::cin,signal);
    if(config.getConfig("step3_scope_marker")!=value)return 1;
    std::cout<<"UNCHANGED"<<std::endl;
    return 0;
}
