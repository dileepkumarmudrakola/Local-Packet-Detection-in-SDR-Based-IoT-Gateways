/*
 * rtl-sdr, turns your Realtek RTL2832 based DVB dongle into a SDR receiver
 * Copyright (C) 2012 by Steve Markgraf <steve@steve-m.de>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <errno.h>
#include <signal.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#ifndef _WIN32
#include <unistd.h>
#else
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#include "getopt/getopt.h"
#endif

#include "rtl-sdr.h"
#include "convenience/convenience.h"


#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define DEFAULT_SAMPLE_RATE		2048000
#define DEFAULT_BUF_LENGTH		(32 * 16384)
#define MINIMAL_BUF_LENGTH		512
#define MAXIMAL_BUF_LENGTH		(256 * 16384)






#define PRM_LEN 4*2048
#define NUM_LEN 100
#define OBS 14*2048



#define PORT 8888
#define IP "127.0.0.1"
#define SIGNALLEN 60*2048
#define PCKSZ 50000




static int do_exit = 0;
static uint32_t bytes_to_read = 0;
static rtlsdr_dev_t *dev = NULL;


int sfd;
struct sockaddr_in servaddr;



double re_preamble[PRM_LEN];
double img_preamble[PRM_LEN];
size_t bytesToWrite=0;


double compute_correlation(double* rsig, double* isig) {


	double res = 0, s_a=0, s_b=0, p_a=0, p_b=0, x=0, y=0;
	double norm = 0.0;
	double mean_sg_real = 0.0;
	double mean_sg_img = 0.0;

	for (int i = 0; i < PRM_LEN; ++i){
		/* code */
		mean_sg_real += rsig[i];
		mean_sg_img += isig[i];
	}

	mean_sg_real /= PRM_LEN;
	mean_sg_img /= PRM_LEN;

	for(int i=0;i<PRM_LEN;i++){

		rsig[i] -= mean_sg_real;
		isig[i] -= mean_sg_img;

		norm += rsig[i]*rsig[i] + isig[i]*isig[i];
	}
	norm = sqrt(norm);

	for(int i = 0; i < PRM_LEN; i++) {
		s_a=rsig[i]/norm;
		s_b=isig[i]/norm;
		p_a=re_preamble[i];
		p_b=img_preamble[i];
		x=(s_a * p_a - s_b * p_b) * (s_a * p_a - s_b * p_b);
		y=(s_a * p_b + s_b * p_a) * (s_a * p_b + s_b * p_a);
		res += (sqrt(x+y));
	}
	return res;
}


double re_oldbuffer[OBS];
double im_oldbuffer[OBS];
double oldbuffer[2*OBS];
int isOldBufferFilled = 0;


// OBS to be set correctly


int findsignal(unsigned char* signal, size_t len){
	
	int start = 0;
	int last = 0;
	double local_max = 0.0;
	double correlation = 0.0;
	int local_index = 0;

	if(len%2!=0){
		fprintf(stderr, "Some issue with signal\n");
		len -=1;
	}


	double re_sig[len/2];
	double im_sig[len/2];
	double re_temp[PRM_LEN];
	double im_temp[PRM_LEN];

	for (int i = 0; i < len/2; ++i){
		
		re_sig[i] = signal[2*i] - 127.5;
		im_sig[i] = signal[2*i+1] - 127.5;

	}


	if(isOldBufferFilled){

		for(int i=0;i< PRM_LEN/2; i++){
			re_temp[i] = re_oldbuffer[OBS - PRM_LEN/2 + i];
			im_temp[i] = im_oldbuffer[OBS - PRM_LEN/2 + i];

			re_temp[PRM_LEN/2+i] = re_sig[i];
			im_temp[PRM_LEN/2+i] = im_sig[i];
		}

		correlation = compute_correlation(re_temp,im_temp);

		if(correlation>0.98){

			local_max = correlation;
			local_index = OBS - PRM_LEN/2;

			start = OBS - PRM_LEN;
			last = OBS;

			for(int i= start; i<last; i++){

				for(int j=0; j<OBS-i; j++){
					re_temp[j] = re_oldbuffer[i];
					im_temp[j] = im_oldbuffer[i];
				}

				for(int j=OBS-i; j<PRM_LEN; j++){
					re_temp[j] = re_sig[j+i-OBS];
					im_temp[j] = im_sig[j+i-OBS];
				}

				correlation = compute_correlation(re_temp,im_temp);

				if(correlation>local_max){
					local_max = correlation;
					local_index = i;
				}
			}

			return local_index - OBS - 10*2048;
		}


	}

	correlation = compute_correlation(re_sig,im_sig);

	if(correlation > 0.98){

		local_max = correlation;
		local_index = 0;


		if(isOldBufferFilled){

			
			start = OBS - PRM_LEN/2;
			last = OBS;

			for(int i=start;i<last;i++){

				for(int j=0; j<OBS-i; j++){
					re_temp[j] = re_oldbuffer[i];
					im_temp[j] = im_oldbuffer[i];
				}

				for(int j=OBS-i; j<PRM_LEN; j++){
					re_temp[j] = re_sig[j+i-OBS];
					im_temp[j] = im_sig[j+i-OBS];
				}

				correlation = compute_correlation(re_temp,im_temp);

				if(correlation>local_max){
					local_max = correlation;
					local_index = i - OBS;
				}
			}
		}


		start = 0;
		last = PRM_LEN/2;

		for(int i = start; i <last; i++){
			correlation = compute_correlation(re_sig+i,im_sig+i);

			if(correlation>local_max){
				local_max = correlation;
				local_index = i;
			}
		}

		return local_index - 10*2048;
	}

	start = PRM_LEN / 2;
	last = len/2 - PRM_LEN/2;

	for(int i=start; i < last; i += PRM_LEN/2){

		correlation = compute_correlation(re_sig + i,im_sig+i);

		if(correlation>0.98){
			local_max = correlation;
			local_index = i;

			int substart = i - PRM_LEN/2;
			int sublast = i + PRM_LEN/2;

			for(int j= substart; j<sublast; j++){
				correlation = compute_correlation(re_sig+j,im_sig+j);

				if(correlation>local_max){
					local_max = correlation;
					local_index = j;
				}
			}

			return local_index;
		}
	}

	return -OBS - 1;
}


void usage(void)
{
	fprintf(stderr,
		"rtl_sdr, an I/Q recorder for RTL2832 based DVB-T receivers\n\n"
		"Usage:\t -f frequency_to_tune_to [Hz]\n"
		"\t[-s samplerate (default: 2048000 Hz)]\n"
		"\t[-d device_index (default: 0)]\n"
		"\t[-g gain (default: 0 for auto)]\n"
		"\t[-p ppm_error (default: 0)]\n"
		"\t[-b output_block_size (default: 16 * 16384)]\n"
		"\t[-n number of samples to read (default: 0, infinite)]\n"
		"\t[-S force sync output (default: async)]\n"
		"\t-pr <preamble filename>\n"
		"\tfilename (a '-' dumps samples to stdout)\n\n");
	exit(1);
}

#ifdef _WIN32
BOOL WINAPI
sighandler(int signum)
{
	if (CTRL_C_EVENT == signum) {
		fprintf(stderr, "Signal caught, exiting!\n");
		do_exit = 1;
		rtlsdr_cancel_async(dev);
		return TRUE;
	}
	return FALSE;
}
#else
static void sighandler(int signum)
{
	fprintf(stderr, "Signal caught, exiting!\n");
	do_exit = 1;
	rtlsdr_cancel_async(dev);
}
#endif

void sendSignal(unsigned char *buf, uint32_t len) {
	char sz[10];
	sprintf(sz, "%d", (int)len);
	if(sendto(sfd, sz, strlen(sz), 0, (struct sockaddr*) &servaddr, (socklen_t)sizeof(servaddr)) == -1) {
		printf("Send Size failed\n");
	} else {
		printf("Sent Size %d\n", len);
	}
	recvfrom(sfd, NULL, 0, 0, NULL, NULL);
	int chks = (len + PCKSZ - 1)/ PCKSZ;
	for(int i = 0; i < chks - 1; i++) {
		if(sendto(sfd, &buf[i * PCKSZ], PCKSZ, 0, (struct sockaddr*) &servaddr, (socklen_t)sizeof(servaddr)) == -1) {
			printf("Send failed\n");
		} else {
			printf("Sent -- %d bytes\n", PCKSZ);
		}
		recvfrom(sfd, NULL, 0, 0, NULL, NULL);
	}
	if(sendto(sfd, &buf[(chks - 1) * PCKSZ], (len - (chks - 1) * PCKSZ), 0, (struct sockaddr*) &servaddr, (socklen_t)sizeof(servaddr)) == -1) {
		printf("Send failed\n");
	} else {
		printf("Sent -- %d bytes\n", len - (chks - 1) * PCKSZ);
	}
	recvfrom(sfd, NULL, 0, 0, NULL, NULL);
}


static void rtlsdr_callback(unsigned char *buf, uint32_t len, void *ctx)
{
	int process = 1;

	if (ctx) {
		if (do_exit)
			return;

		if ((bytes_to_read > 0) && (bytes_to_read < len)) {
			len = bytes_to_read;
			do_exit = 1;
			rtlsdr_cancel_async(dev);
		}
	}

	int signalStart = -1;
		
	if(bytesToWrite>0){

		/*if (fwrite(buf, 1, bytesToWrite, (FILE*)op) != bytesToWrite) {
			fprintf(stderr, "Short write, samples lost, exiting!\n");
		}*/
		/*if (sendto(sfd, buf, bytesToWrite, 0, (struct sockaddr*) &servaddr, (socklen_t)sizeof(servaddr)) != bytesToWrite) {
			fprintf(stderr, "Short write, samples lost, exiting!\n");
		}*/
		if(len>=bytesToWrite){
			/*printf("Sending to server - %d bytes\n", bytesToWrite );
			sendto(sfd, buf, bytesToWrite, 0, (struct sockaddr*) &servaddr, (socklen_t)sizeof(servaddr));*/
			sendSignal(buf, bytesToWrite);
			bytesToWrite = 0;	
		} else {
			/*printf("Sending to server - %d bytes\n", len );
			sendto(sfd, buf, len, 0, (struct sockaddr*) &servaddr, (socklen_t)sizeof(servaddr));*/
			sendSignal(buf, len);
			bytesToWrite -= len;
		}	
		process = 0;
	}


	if(process){


		signalStart = findsignal(buf,len);

		
		if(signalStart == -1 - OBS)
				process = 0;


		if(process){

			printf("Signal Start is -- %d\n",signalStart );

			bytesToWrite = 2*SIGNALLEN;
		

			if(signalStart<0){

				// number of bytes in old buffer = (-signalStart)
				// signal starts at oldbuffer[OBS+signalStart]

				/*printf("Sending to server - %d bytes\n", -2*signalStart );
				sendto(sfd, &oldbuffer[OBS+2*signalStart], -2*signalStart, 0, (struct sockaddr*) &servaddr, (socklen_t)sizeof(servaddr));*/
				sendSignal(&oldbuffer[2*OBS+2*signalStart], -2*signalStart);
				bytesToWrite += 2*signalStart;
				// Since signalStart is going to be a negative quantity
			

				if(len>=bytesToWrite){
					/*printf("Sending to server - %d bytes\n", bytesToWrite );
					sendto(sfd, buf, bytesToWrite, 0, (struct sockaddr*) &servaddr, (socklen_t)sizeof(servaddr));*/
					sendSignal(buf, bytesToWrite);
					bytesToWrite = 0;	
				} else {
					/*printf("Sending to server - %d bytes\n", len );
					sendto(sfd, buf, len, 0, (struct sockaddr*) &servaddr, (socklen_t)sizeof(servaddr));*/
					sendSignal(buf, len);
					bytesToWrite -= len;
				}	
			} else {

				if(len - 2*signalStart >= bytesToWrite){
					/*printf("Sending to server - %d bytes\n", bytesToWrite );
					sendto(sfd, buf+2*signalStart, bytesToWrite, 0, (struct sockaddr*) &servaddr, (socklen_t)sizeof(servaddr));*/
					sendSignal(buf+2*signalStart, bytesToWrite);
					bytesToWrite = 0;
				} else{
					/*printf("Sending to server - %d bytes\n", len - 2*signalStart);
					sendto(sfd, buf+2*signalStart, len-2*signalStart, 0, (struct sockaddr*) &servaddr, (socklen_t)sizeof(servaddr));*/
					sendSignal(buf+2*signalStart, len-2*signalStart);
					bytesToWrite  = bytesToWrite - len + 2*signalStart;
				}
			}
		}

	}

	for(int i=0; i<2*OBS;i++){
		oldbuffer[i] = buf[len - 2*OBS + i];
	}

	for(int i=0;i<OBS;i++){
		re_oldbuffer[i] = oldbuffer[2*i]-127.5;
		im_oldbuffer[i] = oldbuffer[2*i+1]-127.5;
	}
	isOldBufferFilled = 1;
	
	if (bytes_to_read > 0)
		bytes_to_read -= len;
	
}


void readPreamble(char* filename){

	// TODO
	/*
	This functions fills the arrays, re_preamble, img_preamble with the values of preamb
	*/

	double norm = 0.0;
  	double mean_pr_real = 0.0;
  	double mean_pr_img = 0.0;


	FILE* fp = fopen(filename, "r");

	if(!fp){
		fprintf(stderr, "Failed to open preamble file");
		exit(1);	
	}



	double real, img;
	int ci = 0;

	char* cnum = (char*)malloc(NUM_LEN * sizeof(char));
	char* delim = ",";
	char* num;
	ci = 0;
	//preamble
	while(fgets(cnum, NUM_LEN, fp) != NULL) {
		num = strtok(cnum, delim);
		real = atof(num);
		num = strtok(NULL, delim);
		img = atof(num);
		
		if(ci<10*2048){
			ci++;
			continue;
		}
		re_preamble[ci-10*2048] = real;
	 	img_preamble[ci-10*2048] = img;
		ci++;
	}





	fclose(fp);

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

}




int main(int argc, char **argv)
{
#ifndef _WIN32
	struct sigaction sigact;
#endif
	char *filename = NULL;
	char *prfilename = NULL;
	int n_read;
	int r, opt;
	int gain = 0;
	int ppm_error = 0;
	int sync_mode = 0;
	FILE *file;
	uint8_t *buffer;
	int dev_index = 0;
	int dev_given = 0;
	uint32_t frequency = 100000000;
	uint32_t samp_rate = DEFAULT_SAMPLE_RATE;
	uint32_t out_block_size = DEFAULT_BUF_LENGTH;

	while ((opt = getopt(argc, argv, "d:f:g:s:b:n:p:S")) != -1) {
		switch (opt) {
		case 'd':
			dev_index = verbose_device_search(optarg);
			dev_given = 1;
			break;
		case 'f':
			frequency = (uint32_t)atofs(optarg);
			break;
		case 'g':
			gain = (int)(atof(optarg) * 10); /* tenths of a dB */
			break;
		case 's':
			samp_rate = (uint32_t)atofs(optarg);
			break;
		case 'p':
			ppm_error = atoi(optarg);
			break;
		case 'b':
			out_block_size = (uint32_t)atof(optarg);
			break;
		case 'n':
			bytes_to_read = (uint32_t)atof(optarg) * 2;
			break;
		case 'S':
			sync_mode = 1;
			break;
		default:
			usage();
			break;
		}
	}


	
	if (argc <= optind) {
		usage();
	} else {
		filename = argv[optind];
	}

	readPreamble("/home/dileepkumar/wcn/preamble.csv");

	if(out_block_size < MINIMAL_BUF_LENGTH ||
	   out_block_size > MAXIMAL_BUF_LENGTH ){
		fprintf(stderr,
			"Output block size wrong value, falling back to default\n");
		fprintf(stderr,
			"Minimal length: %u\n", MINIMAL_BUF_LENGTH);
		fprintf(stderr,
			"Maximal length: %u\n", MAXIMAL_BUF_LENGTH);
		out_block_size = DEFAULT_BUF_LENGTH;
	}

	if((sfd = socket(AF_INET, SOCK_DGRAM, 0)) == 0) {
		printf("Socket creation failed\n");
		exit(1);
	}
	memset((char*)&servaddr, 0, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_port = htons(PORT);
	inet_pton(AF_INET, IP, &servaddr.sin_addr);
 	//servaddr.sin_addr.s_addr = INADDR_ANY;

	buffer = malloc(out_block_size * sizeof(uint8_t));

	if (!dev_given) {
		dev_index = verbose_device_search("0");
	}
	if (dev_index < 0) {
		exit(1);
	}

	r = rtlsdr_open(&dev, (uint32_t)dev_index);
	if (r < 0) {
		fprintf(stderr, "Failed to open rtlsdr device #%d.\n", dev_index);
		exit(1);
	}
#ifndef _WIN32
	sigact.sa_handler = sighandler;
	sigemptyset(&sigact.sa_mask);
	sigact.sa_flags = 0;
	sigaction(SIGINT, &sigact, NULL);
	sigaction(SIGTERM, &sigact, NULL);
	sigaction(SIGQUIT, &sigact, NULL);
	sigaction(SIGPIPE, &sigact, NULL);
#else
	SetConsoleCtrlHandler( (PHANDLER_ROUTINE) sighandler, TRUE );
#endif
	/* Set the sample rate */
	verbose_set_sample_rate(dev, samp_rate);

	/* Set the frequency */
	verbose_set_frequency(dev, frequency);

	if (0 == gain) {
		 /* Enable automatic gain */
		verbose_auto_gain(dev);
	} else {
		/* Enable manual gain */
		gain = nearest_gain(dev, gain);
		verbose_gain_set(dev, gain);
	}

	verbose_ppm_set(dev, ppm_error);

	if(strcmp(filename, "-") == 0) { /* Write samples to stdout */
		file = stdout;
#ifdef _WIN32
		_setmode(_fileno(stdin), _O_BINARY);
#endif
	} else {
		file = fopen(filename, "wb");
		if (!file) {
			fprintf(stderr, "Failed to open %s\n", filename);
			goto out;
		}
	}

	/* Reset endpoint before we start reading from it (mandatory) */
	verbose_reset_buffer(dev);

	if (sync_mode) {
		fprintf(stderr, "Reading samples in sync mode...\n");
		while (!do_exit) {
			r = rtlsdr_read_sync(dev, buffer, out_block_size, &n_read);
			if (r < 0) {
				fprintf(stderr, "WARNING: sync read failed.\n");
				break;
			}

			if ((bytes_to_read > 0) && (bytes_to_read < (uint32_t)n_read)) {
				n_read = bytes_to_read;
				do_exit = 1;
			}

			if (fwrite(buffer, 1, n_read, file) != (size_t)n_read) {
				fprintf(stderr, "Short write, samples lost, exiting!\n");
				break;
			}

			if ((uint32_t)n_read < out_block_size) {
				fprintf(stderr, "Short read, samples lost, exiting!\n");
				break;
			}

			if (bytes_to_read > 0)
				bytes_to_read -= n_read;
		}
	} else {
		fprintf(stderr, "Reading samples in async mode...\n");
		r = rtlsdr_read_async(dev, rtlsdr_callback, (void *)file,
				      0, out_block_size);
	}

	if (do_exit)
		fprintf(stderr, "\nUser cancel, exiting...\n");
	else
		fprintf(stderr, "\nLibrary error %d, exiting...\n", r);

	if (file != stdout)
		fclose(file);

	rtlsdr_close(dev);
	free (buffer);
out:
	return r >= 0 ? r : -r;
}
