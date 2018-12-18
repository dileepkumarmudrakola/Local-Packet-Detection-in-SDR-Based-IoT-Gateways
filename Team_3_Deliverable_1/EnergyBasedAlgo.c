#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#define NUMBYTES 2
#define RATIO_THRESHOLD 3
#define PREAMBLELENGTH 2048*14
#define THRESHOLDPERCENT 0.7

int main(int argc, char** argv){

	if(argc!=4){
		printf("Usage: ./a.out <BIN FILE> <WINDOW SIZE> <STRIDE>\n");
		exit(0);
	}

	int winSize, strideSize;

	winSize = atoi(argv[2]);
	strideSize = atoi(argv[3]);

	if(winSize<0 || strideSize<0 || strideSize>winSize){
		fprintf(stderr,"Incorrect <Window Size> / <Stride Size>\n");
		exit(0);
	}

	float signalValue[winSize];
	int windowStart = 0;
	int index = 0;


	FILE* fp;
	size_t bytes=0;
	unsigned char buf[NUMBYTES];
	float a,b,magnitude;
	int sampleCount=0;

	float thisWindow = 0.0;
	int numSamplesProcessedForThisWindow = 0;
	int numSamplesYetToBeProcessedForThisWindow = winSize;


	float avgNoise = 0.0;
	float avgSignal = 0.0;
	int totalCountNoiseSample = 0;
	int totalCountSignalSample = 0;
	int flag = 0;
	int lastSignalDetected = 0;


	fp = fopen(argv[1],"rb");
	while((bytes = fread(buf, sizeof *buf, NUMBYTES, fp))==NUMBYTES){
		a = buf[0]-127.5;
		b = buf[1]-127.5;
		magnitude = sqrt(a*a+b*b);

		signalValue[index] = magnitude;
		index = (index+1)%winSize;
		sampleCount++;

		numSamplesProcessedForThisWindow++;
		numSamplesYetToBeProcessedForThisWindow--;
		
		if(numSamplesYetToBeProcessedForThisWindow==0){
			for(int i=0;i<winSize;i++){
				thisWindow += signalValue[i];
			}
			thisWindow /= winSize;

			if(avgSignal == 0){
				if(avgNoise == 0){
					/* The first window */
					avgNoise = thisWindow;
					totalCountNoiseSample = winSize;
					flag = 0;
				} else {
					if(thisWindow/avgNoise > RATIO_THRESHOLD){
						/* The first signal */
						avgSignal = thisWindow;
						totalCountSignalSample = winSize;
						printf("Sample probably starts at %d\n",sampleCount - winSize);
						lastSignalDetected = sampleCount-winSize;
						flag=1;
					} else {
						/* Update noise */
						avgNoise = (avgNoise*totalCountNoiseSample+thisWindow*winSize)/(totalCountNoiseSample+winSize);
						totalCountNoiseSample += winSize;
					}
				}
			} else {
				

				if(thisWindow > THRESHOLDPERCENT*(avgNoise+avgSignal)){
					avgSignal = (avgSignal*totalCountSignalSample+thisWindow*winSize)/(totalCountSignalSample+winSize);
					totalCountSignalSample += winSize;
					if(flag==0 && sampleCount - winSize - lastSignalDetected >PREAMBLELENGTH){
						printf("Sample probably starts at %d\n",sampleCount - winSize);
						lastSignalDetected = sampleCount - winSize;
						flag = 1;
					}
				} else {
					avgNoise = (avgNoise*totalCountNoiseSample+thisWindow*winSize)/(totalCountNoiseSample+winSize);
					totalCountNoiseSample += winSize;
					if(sampleCount - winSize - lastSignalDetected >PREAMBLELENGTH)
						flag = 0;
				}



			}
	
			numSamplesProcessedForThisWindow = winSize - strideSize;
			numSamplesYetToBeProcessedForThisWindow = strideSize;
		}
	}


// 	printf("Average Noise = %f\n",avgNoise );
// 	printf("Average Signal = %f\n",avgSignal );
}
