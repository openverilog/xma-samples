/**********
Copyright (c) 2018, Xilinx, Inc.
All rights reserved.
Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:
1. Redistributions of source code must retain the above copyright notice,
this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice,
this list of conditions and the following disclaimer in the documentation
and/or other materials provided with the distribution.
3. Neither the name of the copyright holder nor the names of its contributors
may be used to endorse or promote products derived from this software
without specific prior written permission.
THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
**********/
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "xlnx_datamover.h"


static int32_t xlnx_datamover_init(XmaEncoderSession *enc_session)
{
    DatamoverContext *ctx = enc_session->base.plugin_data;
    XmaSession xma_session = enc_session->base;
    XmaEncoderProperties *enc_props = &enc_session->encoder_props;

    ctx->in_frame = 0;
    ctx->out_frame = 0;
    ctx->n_frame = 0;
    ctx->width = enc_props->width;
    ctx->height = enc_props->height;
 
    unsigned int input_size = (ctx->width) * (ctx->height);
    unsigned int output_size = (input_size * 3) >> 1;
    /* Reference frame requires atleast 16MB space in DDR */
    unsigned int ref_size = 16*1024*1024;  

    /* Allocate memory for encoder input and output buffers */
    int i=0;
    int32_t return_code = -1;
    for (i = 0; i < NUM_BUFFERS; i++) {
       ctx->encoder.input_y_buffer[i] = xma_plg_buffer_alloc(xma_session, input_size, false, &return_code);
       ctx->encoder.input_u_buffer[i] = xma_plg_buffer_alloc(xma_session, input_size >> 2, false, &return_code);
       ctx->encoder.input_v_buffer[i] = xma_plg_buffer_alloc(xma_session, input_size >> 2, false, &return_code);
       ctx->encoder.output_buffer[i] = xma_plg_buffer_alloc(xma_session, output_size, false, &return_code);
       ctx->encoder.ref_buffer[i] = xma_plg_buffer_alloc(xma_session, ref_size, false, &return_code);
       ctx->encoder.output_len[i] = xma_plg_buffer_alloc(xma_session, sizeof(uint64_t), false, &return_code);
       ctx->encoder.dummy_count[i] = xma_plg_buffer_alloc(xma_session, sizeof(uint64_t), false, &return_code);
    }
    //printf("Init: Buffer allocation complete...\n");
	
    /* Set-up encoder parameters */
    ctx->bitrate = 0;
    ctx->fixed_qp = 0;
    int32_t bitrate = enc_props->bitrate;
    int32_t global_quality = enc_props->qp;

    if (bitrate > 0)
       ctx->bitrate = bitrate;    
    else if (global_quality > 0) 
       ctx->fixed_qp = global_quality;

    int32_t gop_size = enc_props->gop_size;
    if (gop_size > 0)
       ctx->intra_period = gop_size;

    ctx->dummy_outratio = 1;
    ctx->dummy_delay = 2000000;
	
    return 0;
}

static int32_t xlnx_datamover_send_frame(XmaEncoderSession *enc_session, XmaFrame *frame)
{
    DatamoverContext *ctx = enc_session->base.plugin_data;
    XmaSession xma_session = enc_session->base;
    uint32_t nb = 0;
    nb = ctx->n_frame % NUM_BUFFERS;


    memset((void*)ctx->regmap, 0x0, REGMAP_SIZE);
	
    if(frame->data[0].buffer !=NULL) {
       memcpy(ctx->regmap+ADDR_FRAME_WIDTH_DATA, &(ctx->width), sizeof(uint32_t));
       memcpy(ctx->regmap+ADDR_FRAME_HEIGHT_DATA, &(ctx->height), sizeof(uint32_t));
       memcpy(ctx->regmap+ADDR_QP_DATA, &(ctx->fixed_qp), sizeof(uint32_t));
       memcpy(ctx->regmap+ADDR_BITRATE_DATA, &(ctx->bitrate), sizeof(uint32_t));
       memcpy(ctx->regmap+ADDR_INTRA_PERIOD_DATA, &(ctx->intra_period), sizeof(uint32_t));
       memcpy(ctx->regmap+ADDR_DUMMY_OUTRATIO_DATA, &(ctx->dummy_outratio), sizeof(uint32_t));
       memcpy(ctx->regmap+ADDR_DUMMY_DELAY_DATA, &(ctx->dummy_delay), sizeof(uint32_t));
    
       memcpy(ctx->regmap+ADDR_SRCYIMG_Y_DATA, &(ctx->encoder.input_y_buffer[nb].paddr), sizeof(uint64_t));
       memcpy(ctx->regmap+ADDR_SRCUIMG_U_DATA, &(ctx->encoder.input_u_buffer[nb].paddr), sizeof(uint64_t));
       memcpy(ctx->regmap+ADDR_SRCVIMG_V_DATA, &(ctx->encoder.input_v_buffer[nb].paddr), sizeof(uint64_t));

       memcpy(ctx->regmap+ADDR_DSTIMG_V_DATA, &(ctx->encoder.output_buffer[nb].paddr), sizeof(uint64_t));
       memcpy(ctx->regmap+ADDR_DSTREF_V_DATA, &(ctx->encoder.ref_buffer[nb].paddr), sizeof(uint64_t));
       memcpy(ctx->regmap+ADDR_DSTNAL_SIZE_V_DATA, &(ctx->encoder.output_len[nb].paddr), sizeof(uint64_t));
       memcpy(ctx->regmap+ADDR_DSTDUMMY_CNT_V_DATA, &(ctx->encoder.dummy_count[nb].paddr), sizeof(uint64_t));   


       memcpy(ctx->encoder.input_y_buffer[nb].data, frame->data[0].buffer, ctx->encoder.input_y_buffer[nb].size);
       xma_plg_buffer_write(xma_session,
            ctx->encoder.input_y_buffer[nb],
            ctx->encoder.input_y_buffer[nb].size, 0);
			 
       memcpy(ctx->encoder.input_u_buffer[nb].data, frame->data[1].buffer, ctx->encoder.input_u_buffer[nb].size);
       xma_plg_buffer_write(xma_session,
            ctx->encoder.input_u_buffer[nb],
            ctx->encoder.input_u_buffer[nb].size, 0);
	
       memcpy(ctx->encoder.input_v_buffer[nb].data, frame->data[2].buffer, ctx->encoder.input_v_buffer[nb].size);
       xma_plg_buffer_write(xma_session,
            ctx->encoder.input_v_buffer[nb],
            ctx->encoder.input_v_buffer[nb].size, 0);		 
    }
	
    int32_t ret_code = -1;
    XmaCUCmdObj cu_cmd = xma_plg_schedule_work_item(xma_session, (void*)ctx->regmap, REGMAP_SIZE, &ret_code);
    while (ret_code < 0) {
      sleep(1);
      cu_cmd = xma_plg_schedule_work_item(xma_session, (void*)ctx->regmap, REGMAP_SIZE, &ret_code);
    };

    if (ctx->n_frame == 0) {
       ctx->n_frame++;
       ctx->in_frame++;
       return XMA_SEND_MORE_DATA;
    }

    ctx->n_frame++;
    if(frame->data[0].buffer !=NULL)    
	ctx->in_frame++;
    
    return 0;
}

static int32_t xlnx_datamover_recv_data(XmaEncoderSession *enc_session, XmaDataBuffer *data, int32_t *data_size)
{
    DatamoverContext *ctx = enc_session->base.plugin_data;
    XmaSession xma_session = enc_session->base;
    int64_t out_size = 0;
    uint64_t d_cnt = 0;
    uint32_t nb = (ctx->n_frame) % NUM_BUFFERS; 

    while (xma_plg_is_work_item_done(xma_session, 1000) < 0) {
      sleep(1);
    };
	
    /* Read the length of output data */
    xma_plg_buffer_read(xma_session, ctx->encoder.output_len[nb], sizeof(out_size), 0);
       memcpy(&out_size, ctx->encoder.output_len[nb].data, ctx->encoder.output_len[nb].size);
	
    /* Ensure output frame length is valid */
    if((out_size > 0) && (out_size <= data->alloc_size)) {
       xma_plg_buffer_read(xma_session, ctx->encoder.output_buffer[nb], out_size, 0);
       memcpy(data->data.buffer, ctx->encoder.output_buffer[nb].data, out_size);

       xma_plg_buffer_read(xma_session, ctx->encoder.dummy_count[nb], sizeof(d_cnt), 0);
       memcpy(&d_cnt, ctx->encoder.dummy_count[nb].data, sizeof(d_cnt));

       ctx->out_frame++;
       *data_size = out_size;
    }
    else {
       *data_size = 0;
       return -1;
    }
    return 0;
}

static int32_t xlnx_datamover_close(XmaEncoderSession *enc_session)
{
    DatamoverContext *ctx = enc_session->base.plugin_data;
    XmaSession hw_handle = enc_session->base;
	int i=0;
    for (i = 0; i < NUM_BUFFERS; i++) {
       xma_plg_buffer_free(hw_handle, ctx->encoder.input_y_buffer[i]);
       xma_plg_buffer_free(hw_handle, ctx->encoder.input_u_buffer[i]);
       xma_plg_buffer_free(hw_handle, ctx->encoder.input_v_buffer[i]);	
       xma_plg_buffer_free(hw_handle, ctx->encoder.output_buffer[i]);
       xma_plg_buffer_free(hw_handle, ctx->encoder.ref_buffer[i]);
       xma_plg_buffer_free(hw_handle, ctx->encoder.output_len[i]);
       xma_plg_buffer_free(hw_handle, ctx->encoder.dummy_count[i]);
    }

    printf("Released datamover resources!\n");
    return 0;
}

static int32_t xlnx_datamover_xma_version(int32_t *main_version, int32_t *sub_version)
{
    *main_version = 2019;
    *sub_version = 2;

    return 0;
}


XmaEncoderPlugin encoder_plugin = {
    .hwencoder_type    = XMA_COPY_ENCODER_TYPE,
    .hwvendor_string   = "Xilinx",
    .format            = XMA_YUV420_FMT_TYPE,
    .bits_per_pixel    = 8,
    .plugin_data_size  = sizeof(DatamoverContext),
    //.kernel_data_size  = sizeof(HostKernelCtx),
    .init              = xlnx_datamover_init,
    .send_frame        = xlnx_datamover_send_frame,
    .recv_data         = xlnx_datamover_recv_data,
    .close             = xlnx_datamover_close,
    //.alloc_chan        = xlnx_datamover_alloc_chan
    .xma_version             = xlnx_datamover_xma_version
};
