#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <string.h>

#include "Queue.h"
#include "BLE_parser.h"

#define MAX_CLASSIF 10
#define MAX_NAME_LEN 12 


int i, j;
int *state_machine;
int *semaphore;
int pipe_sensor[2];
pid_t pid_setup = 123;
pid_t pid_sensing = 123;
pid_t pid_parsing = 123;
FILE *output;

// Automated BLE Connection
void setupBLE()
{
	// Run bctl_auto.sh
	pid_setup = fork();
	if (pid_setup == 0){
		execv("./bctl_auto.sh", NULL);
	}
	else{
		wait(NULL);
	}
}

// BlueZ Data Collection
void enableSTM()
{
	// Run gattool
	pid_sensing=fork();
	if (pid_sensing == 0)
	{
		// raw hex data stored in motion_data.txt
		close(pipe_sensor[0]);
		dup2(pipe_sensor[1], STDOUT_FILENO);

		const char* suffix[9] =
		{
			"/usr/bin/gatttool",
			"-b",
			"C0:83:26:30:4F:4D",
			"-t",
			"random",
			"--char-write-req",
			"--handle=0x0012",
			"--value=0100",
			"--listen"
		};
		execl("/usr/bin/gatttool", suffix[0], suffix[1], suffix[2], suffix[3], suffix[4], suffix[5], suffix[6], suffix[7], suffix[8], (char *) NULL);
	}
}

// Parsing HEX data
void parseSTM(){
	//Parsing from HEX to decimal
	pid_parsing=fork();
	if (pid_parsing == 0)
	{
		init_parsing();
		char raw[BUFF_MAX];
		close(pipe_sensor[1]);
		dup2(pipe_sensor[0], STDIN_FILENO);

		// Advance line over file header
		fgets(raw, BUFF_MAX, stdin);

		// Read motion data
		for (i = 0; i < QUEUE_MAX; ++i){
			//update Queue on arrival of new data
			if (fgets(raw, BUFF_MAX, stdin)){
				if(stream_parser(raw, enQueue) == 0){ return; } // stream_parser modified for Queues	
			}
			else{ --i; }
		}
		*semaphore = 0; //notify parent process queue of size QUEUE_MAX 

		// Maintain queue size, QUEUE_MAX
		while(1){
			switch(*state_machine){
			case 0:
				if(fgets(raw, BUFF_MAX, stdin)){
					if(stream_parser(raw, denQueue) == 0){ return; }
				}
				break;
			case 1:
				// save current queue to csv file
				output = fopen("output.csv", "w");
				for (i = 0; i < QUEUE_MAX; ++i){
					fprintf(output,"%f,%f,%f,%f,%f,%f,%f,%f,%f\n",getElt(ax,i),getElt(ay,i),getElt(az,i),getElt(gx,i),getElt(gy,i),getElt(gz,i),getElt(mx,i),getElt(my,i),getElt(mz,i));
				}
				fclose(output);
				*semaphore = 0; // notify parent process file safly written
				*state_machine = 0; // return to parsing 
				break;
			default:
				destr_parsing(); //free memory
				return;
			}
		}
	}
}

int main(int argc, char **argv)
{
	if(argc != 2){
		printf("Please enter:\nClassification Number (rec. 3)\n");
		return 0;
	}

	// Initialize values
	// float cutoff_freq = atof(argv[1]); <- deprecated, cutoff hardcoded now
	// Classification values for ML System
	char classif_names[MAX_CLASSIF][MAX_NAME_LEN+1];
	int classif_num = atoi(argv[1]);
	if (classif_num > MAX_CLASSIF){
		printf("Error: Maxmimum number of classifications is 10\n");
		return 0;
	}
	int dataset_size;
	// State Machine to keep track of program progress
	state_machine = mmap(NULL, sizeof(int), PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, -1, 0);
	*state_machine = 0;
	semaphore = mmap(NULL, sizeof(int), PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, -1, 0);
	*semaphore = 1;

	// Enable Bluetooth Low Energy Connection
	setupBLE(); //spawns new process to do this.
	if (pid_setup == 0){ return; }

	// Ask for training motion names (i.e. circle triangle square none)
	printf("Please keep the names of motion unique and below 12 characters.\n");
	for (i = 0; i < classif_num; ++i){
		printf("What is the name of the motion %d/%d?\n",i+1,classif_num);
		fflush(stdin);
		scanf("%s", classif_names[i]);
	}
	printf("Collect how many data samples per motion?\n");
	fflush(stdin);
	scanf("%d", &dataset_size);
	printf("setup complete!\n");

	// Pipe setup
	if (pipe(pipe_sensor) == -1){
		fprintf(stderr, "Pipe failed.");
		return 1;
	}

	// Initiate data collection
	enableSTM(); //spawns new process to do this.
	if (pid_sensing == 0){ return; }

	// Initiate data parsing
	parseSTM(); //spawns new process to do this.
	if (pid_parsing == 0) { return; }
	printf("Collecting buffered data...\n");
	while (*semaphore == 1){ ; } //TODO: change this to actual semaphore

	// clear input buffer
	fflush(stdin);
	getchar();

	// Create the .csv files
	char output_path [MAX_NAME_LEN+20];
	for (i = 0; i < classif_num; ++i){
		for (j = 0; j < dataset_size; ++j){
			printf("Type [Enter] to save files for training sample %d/%d [%s].\n", j+1, dataset_size, classif_names[i]);
			fflush(stdin);
			getchar();

			*semaphore = 1;	
			*state_machine = 1;	
			printf("Creating csv file...\n");
			sprintf(output_path, "./training_set/%s%d.csv", classif_names[i],j);
			while (*semaphore == 1){ ; }
			rename("output.csv",output_path);
		}
	}

	// Exit Program
	printf("Type [Enter] to stop recording.\n");
	fflush(stdin);
	getchar();

	*state_machine = 2;	

	// Wait for child processes to finish
	waitpid(pid_parsing, &i, 0);
	kill(pid_sensing, SIGKILL);

	printf("Program Exit Successful!\n");
}
