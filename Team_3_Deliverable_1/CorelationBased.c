#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <pthread.h>

#define NUM_LEN 40
#define SIG_LEN 10000000
#define PRM_LEN 14*2048
// #define PRM_LEN 14*2048
// #define NEW_PRM_LEN 4*2048
#define NUM_THREADS 50
#define THRESHOLD 2


double compute_correlation(double* rsig, double* isig, double* rpre, double* ipre, int len) {
	double res = 0, s_a=0, s_b=0, p_a=0, p_b=0, x=0, y=0;
	double norm = 0.0;
	double mean_sg_real = 0.0;
	double mean_sg_img = 0.0;


	for (int i = 0; i < len; ++i){
		/* code */
		mean_sg_real += rsig[i];
		mean_sg_img += isig[i];
	}

	mean_sg_real /= len;
	mean_sg_img /= len;

	for(int i=0;i<len;i++){

		rsig[i] -= mean_sg_real;
		isig[i] -= mean_sg_img;

		norm += rsig[i]*rsig[i] + isig[i]*isig[i];
	}
	norm = sqrt(norm);

	for(int i = 0; i < len; i++) {
		s_a=rsig[i]/norm;
		s_b=isig[i]/norm;
		p_a=rpre[i];
		p_b=ipre[i];
		x=(s_a * p_a - s_b * p_b) * (s_a * p_a - s_b * p_b);
		y=(s_a * p_b + s_b * p_a) * (s_a * p_b + s_b * p_a);
		res += (sqrt(x+y));
	}
	return res;
}

void readData(const char* signal_file, const char* preamble_file, double* re_signal, double* re_preamble, double* img_signal, double* img_preamble) {
	FILE* fp1 = fopen(signal_file, "r");
	FILE* fp2 = fopen(preamble_file, "r");

	double real, img;
	char cmp_num[2];
	int ci = 0;

	//signal
	while(fread(cmp_num, sizeof(char), 2, fp1) == 2) {
		real = (unsigned char)cmp_num[0] - 127.5;
		img = (unsigned char)cmp_num[1] - 127.5;
		re_signal[ci] = real;
		img_signal[ci]=img;
//sqrt(real * real + img * img);
		ci++;
	}

	char* cnum = (char*)malloc(NUM_LEN * sizeof(char));
	char* delim = ",";
	char* num;
	ci = 0;
	//preamble
	while(fgets(cnum, NUM_LEN, fp2) != NULL) {
		num = strtok(cnum, delim);
		real = atof(num);
		num = strtok(NULL, delim);
		img = atof(num);
		re_preamble[ci] = real;
	 	img_preamble[ci] = img;
//sqrt(real * real + img * img);
		ci++;
	}

	fclose(fp1);
	fclose(fp2);
}


int main(int argc, char const *argv[])
{
	if(argc < 3) {
		printf("Give the filenames of signal and preamble as cmdline args");
		return 0;
	}


	double* re_signal = (double*)malloc(SIG_LEN * sizeof(double));
	double* re_preamble = (double*)malloc((PRM_LEN) * sizeof(double));
	double* img_signal = (double*)malloc(SIG_LEN * sizeof(double));
	double* img_preamble = (double*)malloc((PRM_LEN) * sizeof(double));
	readData(argv[1], argv[2], re_signal, re_preamble, img_signal, img_preamble);

	int corl_len = (SIG_LEN - PRM_LEN  + 1);

  	double prev_correlation = 0.0;
  	double correlation = 0.0;

  	for (int i = 0; i < PRM_LEN; ++i){
  		/* code */
  		re_preamble[i] = re_signal[2770920+i];
  		img_preamble[i] = img_signal[2770920+i];
  	}

  	double norm = 0.0;
  	double mean_pr_real = 0.0;
  	double mean_pr_img = 0.0;

  	double local_max = 0.0;
  	int local_index = 0;


  	// re_preamble = re_preamble + 12*2048;
  	// img_preamble = img_preamble + 12*2048;

  	for(int i=0;i<PRM_LEN;i++){
  		mean_pr_img += img_preamble[i];
  		mean_pr_real += re_preamble[i];
  		
  	}

  	mean_pr_real /= PRM_LEN;
  	mean_pr_img /= PRM_LEN;
  	
  	for (int i = 0; i < PRM_LEN; ++i){

  		re_preamble[i] = re_preamble[i] - mean_pr_real;
  		img_preamble[i] = img_preamble[i] - mean_pr_img;

  		norm += re_preamble[i]*re_preamble[i] + img_preamble[i]*img_preamble[i];
  	}

  	norm = sqrt(norm);

  	for (int i = 0; i < PRM_LEN; ++i){
  		re_preamble[i] /= norm;
  		img_preamble[i] /= norm;
  	}

  	// printf("preamble to preamble correlation : %f\n", compute_correlation(re_preamble,img_preamble,re_preamble,img_preamble,PRM_LEN) );


  	// printf("%f\n",compute_correlation(re_signal+5537750+10*2048,img_signal+5537750+10*2048,re_preamble,img_preamble,PRM_LEN) );
  	// exit(0);

  	for (int i = 0; i < corl_len; i=i+(PRM_LEN/2)){
  		/* code */
  		correlation = compute_correlation(re_signal+i, img_signal+i, re_preamble, img_preamble ,PRM_LEN);


  		if(correlation>0.96){

  			local_max = correlation;
  			local_index = i;

			prev_correlation = 0.0;

			int start = i- PRM_LEN/2;
			int last = i + 7*(PRM_LEN/2);

			if(start<0){
				start = 0;
			}
			if(last>corl_len){
				last = corl_len;
			}

			int flag = 0;

			for (int j = start; j < last; ++j){
				/* code */
				correlation = compute_correlation(re_signal+j, img_signal+j, re_preamble, img_preamble ,PRM_LEN);

				// printf("%f\n",correlation );

				if(correlation>local_max){
					local_max = correlation;
					local_index = j;
				}

				// if(flag == 0 && correlation<prev_correlation){
				// 	// printf("Signal possibly starts at %d -- %f\n", j-1 - 10*2048 ,correlation );
				// 	flag = 1;
				// }

				prev_correlation = correlation;
			}
			// printf("0\n");

			printf("Maximum correlation : %f, Indexed at : %d \n",local_max,local_index );

			i = last;  			
  		}

	}

	return 0;
}
