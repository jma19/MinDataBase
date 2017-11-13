#include <cstdlib>
#include <cstring>
#include <iostream>
#include "pfm.h"

using namespace std;

int main() {
    void *data = malloc(1000);
    unsigned short value = 1;
    cout << &value << endl;
    //data 以字节递增 + 1表示增加一个字节
    *(char *) data = 'a';
    *((char *) data + 1) = 'b';
    *((char *) data + 2) = 'c';
    cout << *((char *) data) << endl;

    //以2 bytes 递增地址
    *(short *) ((char *) data + 1) = 1;

    cout <<  *(short *) ((char *) data + 1) << endl;
}
