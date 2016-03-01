#include <stdio.h>

//#define abs(a) ( ((a) < 0) ? -(a) : (a) )
//#define max( a, b ) ( ((a) > (b)) ? (a) : (b) )
//#define min( a, b ) ( ((a) < (b)) ? (a) : (b) )
/*struct test{
  int x : 3;
  int y;
  int sqrt;
};
*/
/*
typedef struct Input_integer {
  int x;
  int dummy;
} Iint;

typedef struct Output_integer {
  int dummy;
  int x;
} Oint;*/
/*
typedef struct Int3_struct {
  int x : 3;
} Int3_type;

int Int3(a) {
  Int3_type t = {.x = a};
  //t.x = a;
  return t.x;
}
*/

// int inData[] = {52, 84};

// square-root approximation:
int main(int inDataA, int inDataB, int __out_outData, int __out_test) {
    // Int3_type g;
    // int r = Int3(inDataC);
    // g.x = inDataC;
    // struct test r;
    // r.x = inDataC;
    // int b : 3;
    // int b = r;//g.x;
    // sqrt(52^2 + 84^2) = 98.79
    // This should be approximated as 100
    // int a = inData[0];
    // int a = inDataB;
    // unsigned int b : 3;
    // int b = inDataC.x;

    // int x = max(abs(a), abs(b));
    // int y = min(abs(a), abs(b));
    // int sqrt = max(x, x-(x>>3)+(y>>1));
    __out_outData = inDataA + inDataB;
    __out_test = inDataA * inDataB;
    __out_test = inDataA + 3 * inDataB;
    return inDataA;
    // r.x = a;
    // r.y = b;
    // r.sqrt = sqrt;

    // return outData.x;///*r.*/sqrt;
}
