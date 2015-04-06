#include <stdlib.h>
#include <math.h>

#include "linalg.h"

#include "backprop.h"

static const uint LCA_A = 1103515245;
static const uint LCA_B = 12345;

/*
static real sqr(real a)
{
	return a*a;
}

static real std_dev(const uint s, const real *a1, const real *a2)
{
	uint j;
	real sum = 0.0;
	for(j = 0; j < s; ++j)
	{
		sum += sqr(a1[j] - a2[j]);
	}
	return sum;
}
*/

void BP_randomize(Network *network, uint seed)
{
	uint i, ix, iy;
	for(i = 0; i < network->depth; ++i)
	{
		uint sx = network->connection[i]->input_size;
		uint sy = network->connection[i]->output_size;
		for(iy = 0; iy < sy; ++iy)
		{
			network->connection[i]->bias[iy] = (real)(seed = seed*LCA_A + LCA_B)/0xffffffff - 0.5;
			for(ix = 0; ix < sx; ++ix)
			{
				network->connection[i]->weight[iy*sx + ix] = (real)(seed = seed*LCA_A + LCA_B)/0xffffffff - 0.5;
			}
		}
	}
}

void BP_shuffle(uint length, void **array, uint seed)
{
	uint i;
	for (i = 0; i < length - 1; i++) 
	{
		uint j = i + (seed = seed*LCA_A + LCA_B)%(length - i);
		void *t = array[j];
		array[j] = array[i];
		array[i] = t;
	}
}

BP_Buffer *BP_createBuffer(const Network *network)
{
	uint i;
	BP_Buffer *buffer = (BP_Buffer*) malloc(sizeof(BP_Buffer));
	buffer->depth = network->depth;
	buffer->error = (BP_Error*) malloc(sizeof(BP_Error)*network->depth);
	buffer->gradient = (BP_Gradient*) malloc(sizeof(BP_Gradient)*network->depth);
	for(i = 0; i < network->depth; ++i)
	{
		buffer->error[i].size = network->layer[i]->size;
		buffer->error[i].error = (real*) malloc(sizeof(real)*network->layer[i]->size);
		buffer->error[i].buffer = (real*) malloc(sizeof(real)*network->layer[i]->size);
		
		buffer->gradient[i].input_size = network->connection[i]->input_size;
		buffer->gradient[i].output_size = network->connection[i]->output_size;
		buffer->gradient[i].grad_weight = (real*) malloc(sizeof(real)*(network->connection[i]->input_size*network->connection[i]->output_size));
		buffer->gradient[i].grad_bias = (real*) malloc(sizeof(real)*network->connection[i]->output_size);
		buffer->gradient[i].buffer = (real*) malloc(sizeof(real)*(network->connection[i]->input_size*network->connection[i]->output_size));
	}
	return buffer;
}

void BP_destroyBuffer(BP_Buffer *buffer)
{
	uint i;
	for(i = 0; i < buffer->depth; ++i)
	{
		free(buffer->error[i].error);
		free(buffer->error[i].buffer);
		free(buffer->gradient[i].grad_weight);
		free(buffer->gradient[i].grad_bias);
		free(buffer->gradient[i].buffer);
	}
	free(buffer->error);
	free(buffer->gradient);
	free(buffer);
}

real BP_computeCost(const Network *network, const real *result)
{
	uint i;
	real sum = 0.0;
	for(i = 0; i < network->layer[network->depth]->size; ++i)
	{
		real y = result[i];
		real a = network->layer[network->depth]->activation[i];
		sum += y*log(a) + (1.0 - y)*log(1.0 - a);
	}
	sum /= network->layer[network->depth]->size;
	return -sum;
}

void BP_computeError(const Network *network, BP_Buffer *buffer, const real *result)
{
	uint i;
	dif_array(
	    network->layer[network->depth]->size,
	    network->layer[network->depth]->activation,
	    result,
	    buffer->error[network->depth-1].error
	);
	for(i = network->depth - 1; i > 0; --i)
	{
		product_mat_t_vec(
		    network->connection[i]->output_size,
		    network->connection[i]->input_size,
		    network->connection[i]->weight,
		    buffer->error[i].error,
		    buffer->error[i-1].error
		);
		scal_array(
		    network->layer[i]->size,
		    1.0,
		    buffer->error[i-1].buffer
		);
		dif_array(
		    network->layer[i]->size,
		    buffer->error[i-1].buffer,
		    network->layer[i]->activation,
		    buffer->error[i-1].buffer
		);
		product_array(
		    network->layer[i]->size,
		    buffer->error[i-1].error,
		    network->layer[i]->activation,
		    buffer->error[i-1].error
		);
		product_array(
		    network->layer[i]->size,
		    buffer->error[i-1].error,
		    buffer->error[i-1].buffer,
		    buffer->error[i-1].error
		);
	}
}

void BP_addGradient(const Network *network, BP_Buffer *buffer)
{
	uint i;
	for(i = 0; i < network->depth; ++i)
	{
		product_tensor(
		    network->connection[i]->input_size,
		    network->connection[i]->output_size,
		    network->layer[i]->activation,
		    buffer->error[i].error,
		    buffer->gradient[i].buffer
		);
		sum_array(
		    network->connection[i]->input_size*network->connection[i]->output_size,
		    buffer->gradient[i].grad_weight,
		    buffer->gradient[i].buffer,
		    buffer->gradient[i].grad_weight
		);
		sum_array(
		    network->connection[i]->output_size,
		    buffer->gradient[i].grad_bias,
		    buffer->error[i].error,
		    buffer->gradient[i].grad_bias
		);
	}
}

void BP_normalizeGradient(BP_Buffer *buffer, uint sample_length)
{
	uint i;
	for(i = 0; i < buffer->depth; ++i)
	{
		product_array_scal(
		    buffer->gradient[i].input_size*buffer->gradient[i].output_size,
		    buffer->gradient[i].grad_weight,
		    1.0/((real) sample_length),
		    buffer->gradient[i].grad_weight
		);
		product_array_scal(
		    buffer->gradient[i].output_size,
		    buffer->gradient[i].grad_bias,
		    1.0/((real) sample_length),
		    buffer->gradient[i].grad_bias
		);
	}
}

void BP_clearGradient(BP_Buffer *buffer)
{
	uint i;
	for(i = 0; i < buffer->depth; ++i)
	{
		scal_array(
		    buffer->gradient[i].input_size*buffer->gradient[i].output_size,
		    0.0,
		    buffer->gradient[i].grad_weight
		);
		scal_array(
		    buffer->gradient[i].output_size,
		    0.0,
		    buffer->gradient[i].grad_bias
		);
	}
}

void BP_performDescent(Network *network, BP_Buffer *buffer, const real rate)
{
	uint i;
	for(i = 0; i < network->depth; ++i)
	{
		product_array_scal(
		    network->connection[i]->output_size,
		    buffer->gradient[i].grad_bias,
		    rate,
		    buffer->gradient[i].buffer
		);
		dif_array(
		    network->connection[i]->output_size,
		    network->connection[i]->bias,
		    buffer->gradient[i].buffer,
		    network->connection[i]->bias
		);
		
		product_array_scal(
		    network->connection[i]->input_size*network->connection[i]->output_size,
		    buffer->gradient[i].grad_weight,
		    rate,
		    buffer->gradient[i].buffer
		);
		dif_array(
		    network->connection[i]->input_size*network->connection[i]->output_size,
		    network->connection[i]->weight,
		    buffer->gradient[i].buffer,
		    network->connection[i]->weight
		);
	}
}