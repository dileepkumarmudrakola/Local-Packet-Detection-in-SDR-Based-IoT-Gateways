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
#define DEFAULT_BUF_LENGTH		(16 * 16384)
#define MINIMAL_BUF_LENGTH		512
#define MAXIMAL_BUF_LENGTH		(256 * 16384)

#define RATIO_THRESHOLD 3
#define PREAMBLELENGTH 2048*14
#define THRESHOLDPERCENT 0.7

#define WINSIZE  128
#define READBUF 262144
#define SIGNALLEN 60*2048

#define PORT 8888
#define IP "10.42.0.1"
#define PCKSZ 50000

int windowStart = 0;
int sampleCount=0;
float avgNoise = 0.0;
float avgSignal = 0.0;
int totalCountNoiseSample = 0;
int totalCountSignalSample = 0;
int flag = 0;
int lastSignalDetected = 0;
FILE *op;
size_t bytesToWrite=0;


static int do_exit = 0;
static uint32_t bytes_to_read = 0;
static rtlsdr_dev_t *dev = NULL;

int sfd;
struct sockaddr_in servaddr;

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
	close(sfd);
	fprintf(stderr, "Signal caught, exiting!\n");
	do_exit = 1;
	rtlsdr_cancel_async(dev);
}
#endif





























int findsignal(unsigned char* signal, size_t len){


	float a,b,magnitude;
	int process=0;
	int index=-1;
	int revindex;
	int numSamplesProcessedForThisWindow = 0;
	float thisWindow = 0.0;
	float signalValue[WINSIZE];
	int samplesInThisChunk = 0;
	int returnValue = -1;


	if(len%2!=0){
		fprintf(stderr, "Some issue with signal\n");
		len -=1;
	}

	for (int i = 0; i < len/2; ++i){
		
		a = signal[2*i] - 127.5;
		b = signal[2*i+1] - 127.5;

		magnitude = sqrt(a*a+b*b);
		index = (index+1)%WINSIZE;
		signalValue[index] = magnitude;
		
		sampleCount++;
		numSamplesProcessedForThisWindow++;
		samplesInThisChunk++;

		if(numSamplesProcessedForThisWindow==WINSIZE || i == (int)len/2 - 1){
			revindex = index;
			for(int j=0;j<numSamplesProcessedForThisWindow;j++){
				thisWindow += signalValue[revindex];
				revindex = (revindex + WINSIZE - 1)%WINSIZE;
			}
			thisWindow /= numSamplesProcessedForThisWindow;
			process = 1;
		}

		if(process){

			if(avgSignal == 0){
				if(avgNoise == 0){
					/* The first window */
					avgNoise = thisWindow;
					totalCountNoiseSample = numSamplesProcessedForThisWindow;
					flag = 0;
				} else {
					if(thisWindow/avgNoise > RATIO_THRESHOLD){
						/* The first signal */
						avgSignal = thisWindow;
						totalCountSignalSample = numSamplesProcessedForThisWindow;
						printf("Sample probably starts at %d\n",sampleCount - numSamplesProcessedForThisWindow);
						lastSignalDetected = sampleCount - numSamplesProcessedForThisWindow;
						returnValue = samplesInThisChunk - numSamplesProcessedForThisWindow;
						flag=1;
					} else {
						/* Update noise */
						avgNoise = (avgNoise*totalCountNoiseSample+thisWindow*numSamplesProcessedForThisWindow)/(totalCountNoiseSample+numSamplesProcessedForThisWindow);
						totalCountNoiseSample += numSamplesProcessedForThisWindow;
					}
				}
			} else {

				if(thisWindow > THRESHOLDPERCENT*(avgNoise+avgSignal)){
					avgSignal = (avgSignal*totalCountSignalSample+thisWindow*numSamplesProcessedForThisWindow)/(totalCountSignalSample+numSamplesProcessedForThisWindow);
					totalCountSignalSample += numSamplesProcessedForThisWindow;
					if(flag==0 && sampleCount - numSamplesProcessedForThisWindow - lastSignalDetected >PREAMBLELENGTH){
						printf("Sample probably starts at %d\n",sampleCount - numSamplesProcessedForThisWindow);
						lastSignalDetected = sampleCount - numSamplesProcessedForThisWindow;
						returnValue = samplesInThisChunk - numSamplesProcessedForThisWindow;
						flag = 1;
					}
				} else {
					avgNoise = (avgNoise*totalCountNoiseSample+thisWindow*numSamplesProcessedForThisWindow)/(totalCountNoiseSample+numSamplesProcessedForThisWindow);
					totalCountNoiseSample += numSamplesProcessedForThisWindow;
					if(sampleCount - numSamplesProcessedForThisWindow - lastSignalDetected >PREAMBLELENGTH)
						flag = 0;
				}
			}
			numSamplesProcessedForThisWindow = 0;
			process = 0;
		}
	}
	return returnValue;
}

int noOfSignals = 0;
char* getFileName(){
	char* filename;

	filename = (char*)malloc(20*sizeof(char));

	sprintf(filename, "signal%d.bin",++noOfSignals );

	return filename;
}

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
		sendSignal(buf, bytesToWrite);
		/*if(sendto(sfd, buf, bytesToWrite, 0, (struct sockaddr*) &servaddr, (socklen_t)sizeof(servaddr)) == -1) {
			printf("Send failed\n");
		} else {
			printf("Sent\n");
		}
		recvfrom(sfd, NULL, 0, 0, NULL, NULL);*/
		//fclose(op);
		bytesToWrite = 0;
		process = 0;
	}


	if(process){



		signalStart = findsignal(buf,len);

		if(signalStart == -1)
				process = 0;


		if(process){

			if(len/2-signalStart >= SIGNALLEN){
				/*FULL SIGNAL IN THIS CHUNK*/

				bytesToWrite = 2*SIGNALLEN;
				//op = fopen(getFileName(),"wb");

				/*if (fwrite(&buf[2*signalStart], 1, bytesToWrite, (FILE*)op) != bytesToWrite) {
					fprintf(stderr, "Short write, samples lost, exiting!\n");
				}*/
				sendSignal(&buf[2*signalStart], bytesToWrite);
				/*if(sendto(sfd, &buf[2*signalStart], bytesToWrite, 0, (struct sockaddr*) &servaddr, (socklen_t)sizeof(servaddr)) == -1) {
	printf("Send failed\n");
}  else {
			printf("Sent\n");
		}
				recvfrom(sfd, NULL, 0, 0, NULL, NULL);*/
				//fclose(op);
				bytesToWrite = 0;

			} else {
				/*SIGNAL SPLIT ACROSS TWO CHUNKS*/

				bytesToWrite = len - 2*signalStart;
				//op = fopen(getFileName(),"wb");

				/*if(fwrite(&buf[2*signalStart], 1, bytesToWrite, (FILE*)op) != bytesToWrite){
					fprintf(stderr, "Short write, samples lost, exiting!\n");
				}*/
				sendSignal(&buf[2*signalStart], bytesToWrite);
				/*if(sendto(sfd, &buf[2*signalStart], bytesToWrite, 0, (struct sockaddr*) &servaddr, (socklen_t)sizeof(servaddr)) == -1){
	printf("Send failed\n");
}  else {
			printf("Sent\n");
		}	 
				recvfrom(sfd, NULL, 0, 0, NULL, NULL);*/
				bytesToWrite = 2*SIGNALLEN - bytesToWrite;
			}
		}

	}
	
	if (bytes_to_read > 0)
		bytes_to_read -= len;
	
}

int main(int argc, char **argv)
{
#ifndef _WIN32
	struct sigaction sigact;
#endif
	char *filename = NULL;
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
