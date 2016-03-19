#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <stdbool.h>

int doCal(int inDataV, int testVar, int sel) {
    int temp;
    if (sel < 1) {
        temp = testVar + 3 * inDataV;
        return temp;
    } else {
        temp = 3 * testVar + inDataV;
        return temp;
    }
}

int doCal2(int inDataV, int testVar) { return 3 * testVar + 3 * inDataV; }

char main(int done, int inDataA, int inDataB, volatile int __out_outData,
          volatile int __out_test) {
    //__out_outData = 0;
    //__out_test = 0;
    int sel = 1;
    while (done != 1) {
        if (sel == 1) {
            __out_outData = doCal(inDataA, inDataB, 0);
            __out_test = 2 * inDataA * 2 * inDataB;
            __out_test = doCal(inDataA, __out_outData, 1);
            sel = 0;
        } else {
            __out_outData = doCal2(inDataA, inDataB);
            __out_test = inDataA * inDataB;
            __out_test = doCal2(inDataA, __out_outData);
            sel = 1;
        }
    }
    return (char)inDataA;
}
