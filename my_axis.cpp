#include <ap_int.h>
#include <hls_stream.h>

struct ap_axis {
	// now the data stores 4 values, not just 1
	ap_uint<32> data;
	ap_uint<1> last;
};

void my_axis_macc(
	hls::stream<ap_axis> &strm_out,
	hls::stream<ap_axis> &strm_in
	)
{
#pragma HLS INTERFACE ap_ctrl_none port=return
#pragma HLS interface axis port=strm_in
#pragma HLS INTERFACE axis port=strm_out

	struct ap_axis tmp, tmpa;
	static ap_int<64> mult;
	static ap_int <66> acc;
	static ap_uint<32> vect_size;
	static ap_int<32> prod1, prod2, prod3, prod4;
	static ap_int<8> localmem[512];
	int i, i_o,cond ;

	// start by reading the number of elements of the column

    for (i=0; i<512; i++) {
        tmp = strm_in.read();
        localmem[i] = tmp.data;
        if (tmp.last == 1) break ;
    }
	vect_size = i;


	/*
	for(i=0; i < vect_size/4 + 1; i++) {
		tmp = strm_in.read();
		localmem[i*4]   = tmp.data.range(8,0);
		localmem[i*4+1] = tmp.data.range(16,8);
		localmem[i*4+2] = tmp.data.range(24,16);
		localmem[i*4+3] = tmp.data.range(32,24);
	}
	*/


	for(;;) {
		acc = 0;
		/*
		for( i = 0; i < vect_size; i++ ) {
#pragma HLS pipeline
			i_o = i+offset;
			if( (i_o) % 4 == 0 )
				tmp = strm_in.read();
			mult = localmem[i] * tmp.data.range((i_o%4+1)*8,(i_o%4)*8);
			acc += mult;
		}
		*/

		for ( i = 0; i < 512; i+=4 ){
#pragma HLS pipeline
			tmp=strm_in.read();
			prod1 = localmem[i] * tmp.data.range(7,0);
			prod2 = localmem[i+1] * tmp.data.range(15,8);
			prod3 = localmem[i+2] * tmp.data.range(23,16);
			prod4 = localmem[i+3] * tmp.data.range(31,24);
			acc += prod1 + prod2 + prod3 + prod4;
			if( i + 4 >= vect_size) break;
		}		
		tmpa.last = tmp.last;
		tmpa.data = acc;
		strm_out.write(tmpa);
		if(tmp.last==1) break;
	}

}
