#include <iostream>
#include <ap_int.h>

void rp_calculator_sub(
    ap_uint<32> a,
    ap_uint<32> b,
    ap_uint<32> *result,
    ap_uint<32> *module_id
);

int main() {
    ap_uint<32> a = 20;
    ap_uint<32> b = 7;
    ap_uint<32> result = 0;
    ap_uint<32> module_id = 0;

    rp_calculator_sub(a, b, &result, &module_id);

    std::cout << "A         = " << a << std::endl;
    std::cout << "B         = " << b << std::endl;
    std::cout << "RESULT    = " << result << std::endl;
    std::cout << "MODULE ID = " << std::hex << module_id << std::endl;

    return 0;
}
