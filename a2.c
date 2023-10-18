#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <mpi.h>
#include <time.h>
#include <math.h>
#include <unistd.h>
#include <stdio.h>
#include <math.h>
#include <time.h>
#include <pthread.h>
#include <stddef.h>


//constants would not change after initialisation
#define randomUB 50
#define K 5
#define NODES 5
#define PORT_NUM 5
#define CYCLE 1
#define TERMINATE -1
#define THRESHOLD 1
#define NDIMS 2
#define ADJ 4
#define OFFSET 20
#define REQTAG 100
#define RESTAG 90
#define ALETAG 80
#define NEATAG 70
#define TEMTAG 60
#define TEMPTTAG 50
#define SLATAG 40


//need to update on function prototype

//MAKEFILE for posix
// mpicc -lpthread a2.c -o a2.o -lm
// mpirun -oversubscribe -np 5 a2.o 2 2


// structure for node data
typedef struct {
	int rank; 
	//node coordinate
	int x; 
	int y; 
	//all the ports in the process
	int ports[K]; 
	//all the threads in the process
	pthread_t threads[K+1]; 
	//each process to apply mute for all the threads it has
	pthread_mutex_t mutex; 
} node_t;

// structure for port data
typedef struct {
	int id; 
	//pointer to struct node_t
	node_t* node; // Change to pointer to node_t
	int available; //(0 or 1)
} port_t;

//structure for alert message
typedef struct {
	//size of the buffer
	int size;
	//neighbours
	int nodes [NODES];
	char alert_msg[256]; //of alert_msg[] mean flexible size would need malloc to the struct
	//use stack array 
} alert_t;

typedef struct {
	node_t* node; // Change to pointer to node_t
	int term; //term to terminate 
	MPI_Comm world;
	MPI_Comm comm2D;
} nodefunc_t;


//Function Prototype
//At top level
//remember to update the signature when there is change in arguments
int master_io(MPI_Comm world_comm, MPI_Comm comm);
int slave_io(MPI_Comm world_comm, MPI_Comm comm, int nrows, int ncols);
void *port_func(void *arg);
void assign_string_to_struct(alert_t *a, char *str);
void* ProcessFunc(void *pArg);
void *node_func(void *args);
void create_MPI_alert_t(MPI_Datatype* mpi_type);

pthread_mutex_t g_Mutex = PTHREAD_MUTEX_INITIALIZER;
int g_nslaves = 0;


int main(int argc, char **argv)
{   
    int ndims=2;
    int size, rank, provided, nrows, ncols;
	int dims[ndims];
    MPI_Comm new_comm;

    // initialize MPI with thread support
	MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);
	if (provided != MPI_THREAD_MULTIPLE) {
 		fprintf(stderr, "This MPI implementation does not support MPI_THREAD_MULTIPLE.\n");
 		MPI_Abort(MPI_COMM_WORLD, 1);
 	}
	
	// get the size and rank of MPI_COMM_WORLD
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);//size = -np 5
									//return true or false, two colour (0 or 1)
    g_nslaves = size - 1;
    /* get the nrows and ncols */
	if (argc == 3) {
		nrows = atoi (argv[1]);
		ncols = atoi (argv[2]);
		dims[0] = nrows; /* number of rows */
		dims[1] = ncols; /* number of columns */
		if( (nrows*ncols) != (size - 1)) {
			if(rank == 0) printf("ERROR: nrows*ncols)=%d * %d = %d != %d\n", nrows, ncols, nrows*ncols,size-1);
			MPI_Finalize(); //master io get 1 process, so need 1 and rest of 4 give to slave io
			return 0;		//which can cause MPI_Cart_create problem
			
		}
	} else {
		nrows=ncols=(int)sqrt(size);
		dims[0]=dims[1]=0;
	}

    MPI_Comm_split( MPI_COMM_WORLD,rank == size-1, 0, &new_comm); // color will either be 0 or 1 
    //after this code, the processes in two communicators run separately

    //collectively working for all processor before this code
	//after following code, divide the rank into corresponding subfunction
    if (rank == size-1)            //only the root rank with last index call master_io with its own new_comm attribute

		//master to control MPI_COMM_WORLD = main function
		master_io(MPI_COMM_WORLD, new_comm);
    else
		//slave yo control new_comm
		slave_io( MPI_COMM_WORLD, new_comm, nrows, ncols);
	//MPI_Comm_free()
    MPI_Finalize();
    return 0;
}

/* This is the master */
int master_io(MPI_Comm world_comm, MPI_Comm comm)
{	
	pthread_t tid;
	pthread_mutex_init(&g_Mutex, NULL);
	pthread_create(&tid, 0, ProcessFunc, NULL);
	
	int worldSize;
	MPI_Comm_size(MPI_COMM_WORLD, &worldSize);
	
	int term = -100;
	MPI_Request req;

	//send termination message after 100 seconds
	for(int i=0; i<2; i++)
	{
		sleep(1);
	}

	fflush(stdout);
	for(int i=0; i<worldSize-1; i++)
	{
		//MPI_Send(nearest, 3, MPI_INT, status.MPI_SOURCE, NEATAG, MPI_COMM_WORLD);
		//MPI_Isend(coord, 2, MPI_INT, worldSize-1, node.rank, MPI_COMM_WORLD, &req[5]);
		MPI_Send(&term, 1, MPI_INT, i, TEMTAG, MPI_COMM_WORLD);
		// MPI_Wait(&req, MPI_STATUS_IGNORE);
		fflush(stdout);
	}

	pthread_join(tid, NULL);
				
}


void* ProcessFunc(void *pArg){
	//MPI
	int worldSize, rank;
	alert_t a;
	//MPI_Datatype alert_type = create_MPI_alert_t(a);
	 // get the size and rank of MPI_COMM_WORLD
 	MPI_Comm_size(MPI_COMM_WORLD, &worldSize);
 	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	
	//Receive the coordinates buffer from all processes
	//int **coords_2d_buffer = calloc(((worldSize - 1)), sizeof(int*));
	MPI_Status status;
	MPI_Request req;
	int current_tag = 0;
	MPI_Datatype MPI_ALERT_T;
				//create corresponding MPI_Type for MPI_Send
	create_MPI_alert_t(&MPI_ALERT_T);

	//recv_array = (int*) malloc((worldSize-1)*2*sizeof(int))
	//MPI_Gather() only work when all the processes in the same function
	
	//while loop to receive all the coordinates
	//Initialise coords for all processes
	//2D buffer

	int coords[(worldSize-1)][2]; 
	//alert message count
	
	while(1){
		//MPI_Probe(MPI_ANY_SOURCE, current_tag, MPI_COMM_WORLD, &status);
		//receive message based on the tag
		//with auto null terminator
		MPI_Recv(coords[current_tag], 2, MPI_INT, current_tag, current_tag, MPI_COMM_WORLD,&status);
		//copy the memory
		//memcpy(coords[current_tag], buf, sizeof(int)*2);
		current_tag++;
		
		if (current_tag==(worldSize-1)){
			break;
		}
	}

	//initialise termination meeesge and available nodes for all the processes;
	//Self-initialise to be -1, do not use constant otherwise error
	int term = -1;
	int avail[worldSize-1];
		for (int i = 0; i < worldSize; i++){
		avail[i] = 1;
	}
	
	int alert_count=0;
	int iteration = 0;
	int slaCount = 0;
	int slaFlag = 0;
	//communicate with charging nodes
	while(slaCount < (worldSize-1)){
		iteration++;

		//time for communication 
		double start1, start2, end1, end2;
		int adjCount=0;
		int msgCount=0;
		int nearbyCount=0;
		int flag;
		time_t alert_time = time(NULL);
		char* alert_time_str = ctime(&alert_time);

		start1 = MPI_Wtime();
		MPI_Iprobe(MPI_ANY_SOURCE, ALETAG, MPI_COMM_WORLD, &flag, &status);
		int temp;
		if(flag)
		{	
			MPI_Recv(&a, 1, MPI_ALERT_T, MPI_ANY_SOURCE, ALETAG, MPI_COMM_WORLD, &status);
			end1 = MPI_Wtime();
			//without fflush can not print correctly
			fflush(stdout);
			msgCount++;

			//get the buf that store the busy ranks
			for (int i=0; i<NODES;i++){
				if(a.nodes[i] != MPI_PROC_NULL){
					//mark the index (=rank) value = 0
					avail[a.nodes[i]] = 0;
					adjCount++;
				}
			}
			
			
			int nearbyDisp = 2;
			int nearest[3];
			int disp = 100; 
			int repo_rank = a.nodes[NODES-1];
			//report rank coordinate
			int repo_coord[2];
			repo_coord[0] = coords[repo_rank][0];
			repo_coord[1] = coords[repo_rank][1];
			//filter out the vacant adjacent nodes of the neighbour nodes
			for (int j=0; j<g_nslaves; j++)
			{
				//if busy nodes not the reporting node 
				if(avail[j]==1){
					//get the adjacent node of this current neighbour
					//report rank coordinate
					int temp_disp;
					int temp_coord[2];
					//j is the index of the available rank in avail array
					temp_coord[0] = coords[j][0];
					temp_coord[1] = coords[j][1];

					temp_disp = abs(temp_coord[0] - repo_coord[0]) + abs(temp_coord[1] - repo_coord[1]);
					if (temp_disp < disp){
						repo_rank = j;
						repo_coord[0] = temp_coord[0];
						repo_coord[1] = temp_coord[1];
					}

					//Nearby nodes count the rank that has distance <=2
					if (temp_disp <= nearbyDisp){
						nearbyCount++;
					}
				}
			}
			nearest[0] = repo_rank;
			nearest[1] = repo_coord[0];
			nearest[2] = repo_coord[1];
			

			//if there is no vacant node ,send the nearest rank to be node which send alert msg
			// if(count==g_nslaves){
			// 	nearest[0]=a.nodes[NODES-1];
			// }
			//MPI_Isend(coord, 2, MPI_INT, worldSize-1, rank, MPI_COMM_WORLD, &request);
			
			fflush(stdout);
			//send buffer
			nearest[1] = coords[nearest[0]][0];
			nearest[2] = coords[nearest[0]][1];
			start2 = MPI_Wtime();
			MPI_Send(nearest, 3, MPI_INT, status.MPI_SOURCE, NEATAG, MPI_COMM_WORLD);
			end2 = MPI_Wtime();
			msgCount++;
			alert_count++;
			//remove itself from the adjacent count
			adjCount--;

			//Log into file
			//have to remove the logfile to get the correct print results
			//prinf into log file
			time_t t = time(NULL);
			char* time_str = ctime(&t);
			//make it a-append so it can append not overwrite and create a logFile if is not
			FILE *logFile = fopen("log.txt", "a");  //s come with newline
			
			//total communication time
			double t1 = end1 - start1;
			double t2 = end2 - start2;
			double tt = t1+t2;

			fprintf(logFile,"\nIteration: %d\nLogged time: %sAlert reported time: %sNumber of adjacent node: %d\nAvailability to be considered full: %d\nReporting Node\t\tCoord\t\tPort Value\t\tAvailable Port: \n%d\t\t\t\t\t(%d,%d)\t\t%d\t\t\t\t%d",iteration, time_str,alert_time_str, adjCount, 1, a.nodes[NODES-1], coords[a.nodes[NODES-1]][0], coords[a.nodes[NODES-1]][1], 5, 0);
			for(int i=0; i<ADJ; i++){
				if(a.nodes[i]!=-2){
					fprintf(logFile, "\nAdjacent Nodes\t\tCoord\t\tPort Value\t\tAvailable Port: \n%d\t\t\t\t\t(%d,%d)\t\t%d\t\t\t\t%d", a.nodes[i], coords[a.nodes[i]][0], coords[a.nodes[i]][1],5,0);
				}
			}
			fprintf(logFile, "\nTotal number of messages sent between reporting nodes and base station: %d", msgCount);
			fprintf(logFile, "\nNumber of available station nearby: %d", nearbyCount);
			fprintf(logFile, "\nCommunication Time (seconds): %f",tt);
			fprintf(logFile, "\n------------------------------------------------------------------------------");
			fclose(logFile);
			sleep(10);
		}

		//terminate base station if all slavo processes ended -> the alert are all processed
		MPI_Iprobe(slaCount, SLATAG, MPI_COMM_WORLD, &slaFlag, &status);
		if(slaFlag)
		{	
			int sla;
			MPI_Recv(&sla, 1, MPI_INT, slaCount, SLATAG, MPI_COMM_WORLD,&status);
			slaCount++;
		}

	}
	return 0;
}


/* This is the slave */ //superset communicator passed in
int slave_io(MPI_Comm world_comm, MPI_Comm comm, int nrows, int ncols)
{	
	//for MPI
	//put here to be globale variables for all processes
	int ndims=2, size, my_rank, reorder, my_cart_rank, ierr, worldSize, rank;
    int dims[ndims];
    dims[0] = nrows;
    dims[1] = ncols;
	int coord[ndims];
	int wrap_around[ndims];
	
	//neightbours
	int left, right, up, down; //ranks for neightous
	//random generator variables
	unsigned int randomSeed; // random number generator seed
	int randVal;

	//random seed with time for processes
	randomSeed = rank * time(NULL);
    
    	MPI_Comm_size(world_comm, &worldSize); // size of the world communicator
  	MPI_Comm_size(comm, &size); // size of the slave communicator
	MPI_Comm_rank(world_comm, &rank);  // rank of the slave communicator
	MPI_Comm_rank(comm, &my_rank);
	//dims[0]=dims[1]=0;

	//MPI_Status contain all the info -- receive data type, sender
	//MPI_Status status[size];

	//timestamp for all processes
	time_t curtime;

	//availability buffer for ports
	int avail[5];
	
	MPI_Dims_create(size, ndims, dims);

	if(my_rank==0)
	printf("Slave Rank: %d. Comm Size: %d: Grid Dimension = [%d x %d] \n",my_rank,size,dims[0],dims[1]);

    /* create cartesian mapping */
	MPI_Comm comm2D;
	wrap_around[0] = 0;
	wrap_around[1] = 0; /* periodic shift is .false. */
	reorder = 0;//no reorder, so the my_cart_rank = rank
	ierr =0; //create virtual topology within slave communicator
	//processes arranged in grid-like structure (just virtual)

	//for each process node create its own struct to store info
	node_t node;
	node.rank = rank;

	ierr = MPI_Cart_create(comm, ndims, dims, wrap_around, reorder, &comm2D);//output argument 
																	//create cart with comm2D variable															
	if(ierr != 0) printf("ERROR[%d] creating CART\n",ierr);
	
	/* find my coordinates in the cartesian communicator group */
	MPI_Cart_coords(comm2D, my_rank, ndims, coord); // coordinated is returned into the coord array
	/* cartesian coordinates to find rank of current process in cartesian group*/
	MPI_Cart_rank(comm2D, coord, &my_cart_rank);

	//assign coordinates from virtual topology
	node.x = coord[0];
	node.y = coord[1];
	
	// initialize the mutex for each process
	//accessing ports array that info of port
	pthread_mutex_init(&node.mutex, NULL); 

	//node_func struct to pass in node_func
	nodefunc_t* n_func = malloc(sizeof(nodefunc_t)); // Dynamically allocate memory for n_func
	n_func->node = &node;
	n_func->world = MPI_COMM_WORLD;
	n_func->comm2D = comm2D;
	n_func->term = 1; //for while loop to run

	for (int i = 0; i < (K + 1); i++) 
	{	
		if(i<K){
			port_t* port = malloc(sizeof(port_t)); // Dynamically allocate memory for port
			//initialise 5 ports for all the nodes
			//assign each with id
			port->id = i;
			port->node = &node; // Assign the address of node

			//available from the begining
			port->available = 1; 
			// only create 5 threads run parallely for each process
			// create a port thread with port data as argument
			pthread_create(&node.threads[i], NULL, port_func, (void *)port); 
		}
		
		//replace the port_func with node_func on pthread id = K
		if(i==K){
			
			int result = pthread_create(&node.threads[i], NULL, node_func, (void *)n_func); 
			if(result == 0){
				printf("Thread created successfully.");
			}
		}	
	}

//variables for the communication on MPI Level to 
//send request to neighbours
//communicate with base station
	int itself; //,up, down, left, right;
	int data[ADJ];
	// MPI_Comm world_comm, comm2D;
	MPI_Status status[NODES];
	MPI_Request req[NODES];

	// nodefunc_t *node_func = (nodefunc_t *)arg;
	// world_comm = node_func -> world;
	// comm2D = node_func -> comm2D;
	// node_t* node = node_func -> node; // Change to pointer to node_t

	printf("\n node rank: %d \n", node.rank); // Access members via pointer
	// MPI_Comm_size(world_comm, &worldSize);

	//printf("\n rank: %d coordinates: [%d, %d]\n",node.rank, coord[0], coord[1]);

	//send coordinates to base stations
	MPI_Isend(coord, 2, MPI_INT, worldSize-1, node.rank, MPI_COMM_WORLD, &req[5]); // Access members via pointer
	printf("\n rank: %d [%d,%d] send coordinates to %d",node.rank, node.x, node.y, worldSize-1); // Access members via pointer
	//wait until and send finish
	MPI_Wait(&req[5], MPI_STATUS_IGNORE);
	

	MPI_Cart_shift(comm2D, 0, 1, &data[0], &data[1]);
	MPI_Cart_shift(comm2D, 1, 1, &data[2], &data[3]); 

	printf("\n all directions: up: %d, down: %d, left: %d, right: %d \n", data[0], data[1], data[2], data[3]);

	//count for while loop
	int count = 0;
	int termFlag;
	int msg;
	//while loop to communicate with threads and base station
	int whileFlag = 1;
	while(1)
	{
		//check termination message from base stations
		//break immediately to avoid sending msg


		// calculate the occupancy
		int occupied = 0;
		for (int i =0; i < K; i++)
		{
			if (node.ports[i] == 0)
			{ 
				occupied++;
			}
		}

		//count for itself or it's neighbours that whether they are vacant
		int vacancies = 0;
		
		double rate = (double) occupied / K;

		if (rate >=  THRESHOLD)
		{
			//THRESHOLD = 1, 
			//if fully utilised
			//bind neighbours into array
			//return MPI_PROC_NULL if certain neighbour does not exist
			// data[0] = up; data[1] = down; data[2] = left; data[3] = right;
			
			//alert message structure
			alert_t a;
			a.size = 0;
			//since itself is always here
			
			//free(a) to free the a struct
			int vacant_data[4];
			int vacancies=0;
			//Send request to neighbour nodes
			for (int i = 0; i < 4; i++)
			{
				//MPI_PROC_NULL  = -2
				if (data[i] != MPI_PROC_NULL )
				{
					
					
					//do following Recv for the Send if need to get immediate reponse
					MPI_Send(&node.rank, 1, MPI_INT, data[i], REQTAG, comm2D); // Access members via pointer
					printf("\n %d send request to: %d \n", node.rank, data[i]);
					fflush(stdout);
					MPI_Recv(&vacant_data[i], 1, MPI_INT, data[i], RESTAG, comm2D, &status[data[i]]);
					printf("\n %d receive response from: %d \n", node.rank, data[i]);
					//printf("\n rank: %d %s to %d \n", node->rank, "sent request", data[i]); 
					fflush(stdout);

					//increament vacancies accordingly;
					vacancies+=vacant_data[i];
					//Put into alert struct which has array to indicate which neighbour node exist
					a.nodes[a.size] = data[i];
					a.size++;
				}
				else
				{
					//printf("\n rank: %d not sent request to %d \n", node->rank, data[i]);
					//since rely on the a.size to put, must increment accordingly
					//to avoid access wired value
					a.nodes[a.size] = MPI_PROC_NULL;
					a.size++;
				}
				/*  0 - 1
					| - |
					2 - 3*/
				//printf("\n rank: %d, node: %d\n", node.rank, a.nodes[i]);
			}
			
			printf("check vacancies: %d",vacancies);
			fflush(stdout);

			char inMsg[256];
			time_t t = time(NULL);
			char* time_str = ctime(&t);
			sprintf(inMsg, "\n On %s Vacant Neighbour Nodes: ", time_str);
			//if there is vacant neigoubour show it
			if (vacancies > 0)
			{
				for(int i=0; i< 4; i++)
				{
					if (data[i] != MPI_PROC_NULL && vacant_data[i] > 0 ){
						char str[256];
						sprintf(str, " %d ", data[i]);
						strcat(inMsg, str);

					}
				}
				printf("%s", inMsg);
				fflush(stdout);
			}

			//if itself and neighbours are not vancant alert base station
			if (vacancies <= 0)
			{
				MPI_Datatype MPI_ALERT_T;
				//create corresponding MPI_Type for MPI_Send
				create_MPI_alert_t(&MPI_ALERT_T);
				//time right now
				time_t t = time(NULL);
				char* time_str = ctime(&t);
				//store the formated string into the alert_msg of the struct
				//strcpy(buffer, "hello");  automatically add null terminator
				sprintf(a.alert_msg, "\n On %s, utilised nodes: \n", time_str);//time_Str has following \n by default

				//concate neighbour ranks and itself
				//printf("node rank: %d", node.rank);

				//add itself to alert array 
				//since itself is always here
				a.nodes[NODES-1] = node.rank;
				a.size++;
				char str[NODES];
				for (int i=0; i < NODES; i++)
				{
					if(a.nodes[i] != MPI_PROC_NULL)
					{	//printf("node about to put: %d", a.nodes[i]);
						sprintf(str, " %d ", a.nodes[i]);
						strcat(a.alert_msg, str);
					}
				}

				printf("\n rank: %d sent alert message  %s\n", node.rank, a.alert_msg);
				fflush(stdout);
				//send the struct     
				int test = 1;                            //output argument that are refreshed
				MPI_Send(&a, 1, MPI_ALERT_T, worldSize-1, ALETAG, MPI_COMM_WORLD);
				//MPI_Wait(&req[5], MPI_STATUS_IGNORE);
				//MPI_Isend(&a, 1, alert_type, worldSize-1, 0, world_comm, &req);
				//printf("\n%s\n", "sent alert");
				
				//Nearst rank
				int nearest[3];
				//receive information about the nearest node
				//so need to wait for the base station
				printf("about to MPI_RECV");
				MPI_Recv(nearest, 3, MPI_INT, worldSize-1, NEATAG, MPI_COMM_WORLD,&status[4]);
				// int ne_coord[2];
				// MPI_Cart_coords(comm2D, nearest, 2, ne_coord); 
				printf("Nearest Node with rank: %d, at [%d,%d]", nearest[0], nearest[1],nearest[2]);
				fflush(stdout);
			}
			
		}
		MPI_Iprobe(worldSize - 1, TEMTAG, MPI_COMM_WORLD, &termFlag, &status[0]);
		if(termFlag)
		{
			whileFlag=0;
			MPI_Recv(&msg, 1, MPI_INT, worldSize-1, TEMTAG, MPI_COMM_WORLD, &status[0]);
			//terminate msg received: should print once for every process, 4 for 4 slave processors
			printf("rank: %d, terminate msg received: %d", node.rank, msg);
			fflush(stdout);
			break;
				//terminate
		}
		count++;
		sleep(10);
	}

	//to terminate while loop in node_func
	//wait 20 seconds so can respond all the request before close
	//change from 1 to 0
	for(int i=0; i<2; i++){
		sleep(10);
	}

	//since the attribute is passed with pointer
	//change the value of the address after sleeping time
	n_func -> term = 0;
	
	for (int i = 0; i < (K+1); i++) 
	{	// wait for all port threads to finish
		pthread_join(node.threads[i], NULL); 
	}
	pthread_mutex_destroy(&node.mutex); // destroy the mutex

	//Notify base station that the slave processes are all shutdown
	//MPI_Isend(&vacancies, 1, MPI_INT, source, RESTAG, comm2D, &req);
	int slaveTerm = -10;
	MPI_Isend(&slaveTerm, 1, MPI_INT, worldSize-1, SLATAG, MPI_COMM_WORLD, &req[node.rank]);
	MPI_Wait(&req[node.rank], MPI_STATUS_IGNORE);
}	


void *node_func(void *arg){
	int flag;
	int worldSize, size;
	int coord[2];
	MPI_Comm world_comm, comm2D;
	
	MPI_Status status;
	MPI_Request req;

	nodefunc_t *node_func = (nodefunc_t *)arg;
	world_comm = node_func -> world;
	comm2D = node_func -> comm2D;
	node_t* node = node_func -> node; // Change to pointer to node_t

	//printf("\n node rank: %d \n", node->rank); // Access members via pointer
	MPI_Comm_size(world_comm, &worldSize);

	int msg;
	int whileFlag = 1;
	while(node_func->term)
	{	
		// if(msg==TERMINATE)
		// {
		// 	break;
		// }
		//at most receive 4 request since every node can only be sounded by 4 neighoubours
		//MPI_Iprobe(MPI_ANY_SOURCE, MPI_ANY_TAG, comm2D, &flag, &status[0]);
		MPI_Iprobe(MPI_ANY_SOURCE, REQTAG, comm2D, &flag, &status);
		if(flag)
		{
			int source;						//source from the status
			//store the request process to the source
			//receive the requestion from other processews
			MPI_Recv(&source, 1, MPI_INT, status.MPI_SOURCE, REQTAG, comm2D, &status);
			printf("\n rank: %d Inside received request from %d \n", node->rank, source);
			fflush(stdout);
					
			//calculate its own vacancy from the its ports
			int vacancies = 0;
			for (int i=0; i<K; i++)
			{
				if(node->ports[i] ==1)
				{ 
					vacancies++;
				}
			}	
			//send back the reponse to the source of request
			MPI_Isend(&vacancies, 1, MPI_INT, source, RESTAG, comm2D, &req);
			printf("rank: %d send back response to %d", node->rank, source);
			MPI_Wait(&req, MPI_STATUS_IGNORE);
		}
		
	}
	return NULL;
}

void assign_string_to_struct(alert_t *a, char *str){
	//for heap array
	//a.alert_msg = malloc(strlen(str)+1); //+1 for null terminator
	//copy string to attribute
	//need to pointer to attribute if a 
	strcpy(a->alert_msg, str);

}

void *port_func(void *arg)
{
    // Cast the argument to a pointer to a port_t struct
    port_t *port = (port_t *)arg;

    // Use the port pointer
    srand(port->id + port->node->rank);

    // Set availability to 0
    port->available = 0;

    // Update with mutex lock for each port with the mutex in their node
    pthread_mutex_lock(&port->node->mutex);

    // Update on the port array with the availability
    for(int i= 0; i < sizeof(port->node->ports)/sizeof(port->node->ports[0]); i++)
	{
		// // //Randomly set availability to 0 or 1, but mostly 0
        port->node->ports[i] = (rand() % 10 < 9) ? 0 : 1; 
		
	}
    pthread_mutex_unlock(&port->node->mutex);

    // Sleep for 10 seconds
    sleep(10);

    return NULL;
}




//create corresponding MPI_Type for the alert_t just for send and receive
void create_MPI_alert_t(MPI_Datatype* mpi_type){
	// Define element data type
	MPI_Datatype types[3] = {MPI_INT, MPI_INT, MPI_CHAR };
	// Blocklength for each element
	int blocklengths[3] = {1,NODES,256 };
	// Array to store displacement in address for each block datatype
	MPI_Aint offsets[3];

	offsets[0] = offsetof(alert_t, size); 
	offsets[1] = offsetof(alert_t, nodes);
	offsets[2] = offsetof(alert_t, alert_msg);

	// Create the MPI data type struct
	MPI_Type_create_struct(3, blocklengths, offsets, types, mpi_type);
	MPI_Type_commit(mpi_type);
}


	
	
