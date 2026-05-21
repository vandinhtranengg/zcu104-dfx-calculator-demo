#include <ap_int.h>

void rp_calculator_sub(
    ap_uint<32> a,
    ap_uint<32> b,
    ap_uint<32> *result,
    ap_uint<32> *module_id
) {
#pragma HLS INTERFACE s_axilite port=a         bundle=CTRL
#pragma HLS INTERFACE s_axilite port=b         bundle=CTRL
#pragma HLS INTERFACE s_axilite port=result    bundle=CTRL
#pragma HLS INTERFACE s_axilite port=module_id bundle=CTRL
#pragma HLS INTERFACE s_axilite port=return    bundle=CTRL

    *result = a - b;
    *module_id = 0x00000002;
}
