#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <stdbool.h>

int doCal(int inDataV, int testVar, bool sel) {
    int temp;
    if (sel == false) {
        temp = testVar + 3 * inDataV;
        return temp;
    } else {
        temp = 3 * testVar + inDataV;
        return temp;
    }
}

int doCal2(int inDataV, int testVar) { return 3 * testVar + 3 * inDataV; }

void main(_Bool done, int inDataA, int inDataB, volatile int __out_outData,
          volatile int __out_test) {

    bool sel = true;
    while (done != true) {
        if (sel == true) {
            int temp;
            temp = doCal(inDataA, inDataB, false);
            __out_outData = temp;
            __out_test = 2 * inDataA * 2 * inDataB;
            __out_test = doCal(inDataA, temp, true);
            sel = false;
        } else {
            int temp;
            temp = doCal2(inDataA, inDataB);
            __out_outData = temp;
            __out_test = inDataA * inDataB;
            __out_test = doCal2(temp, inDataA);
            sel = true;
        }
    }
    return;
}
